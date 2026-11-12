#!/usr/bin/env python3
"""The comfort vignette (Options > VR > Comfort vignette, docs/VR.md), against fake_xr: a 12 s cycle, the left stick
pushed for 4 s (each cycle the other way: Link stays about where he is), then the right stick for 2 s, then 6 s with
the sticks at rest while the head walks half a metre ahead and back (roomscale: the player's own steps). `vr` is read
every quarter second. With the vignette on High:
  - while a stick is pushed the vignette closes in (level > 0.8), and its quad is in the frame: in the head's space,
    1 m ahead, 4 m across (fake_xr's quad log);
  - 1.5 s after the sticks are let go it is gone (level 0), roomscale walking or not;
  - roomscale did walk Link in that time (its frames counted), so the rest phase did test it.
With it Off (the default) no frame has it.

  python tools/vr/test_vr_vignette.py --save <scratch save> [--out build/vr/vignette]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402

CYCLE = 12.0
T0 = 150.0   # (the cycles start here, script time: sticks pushed on the title screen move its menu)
R, L = '/user/hand/right/input/thumbstick', '/user/hand/left/input/thumbstick'


def script():
    lines = ['0 head 0 1.7 0 0 0 0', '0 stick {} 0 0'.format(L), '0 stick {} 0 0'.format(R)]
    for k in range(40):
        t, s = T0 + k * CYCLE, 1 if k % 2 == 0 else -1
        lines += ['{} stick {} 0 {}'.format(t, L, s), '{} stick {} 0 0'.format(t + 4, L),
                  '{} stick {} {} 0'.format(t + 4, R, s), '{} stick {} 0 0'.format(t + 6, R)]
        for i in range(31):   # (6.5 .. 11.5 s: 0.5 m ahead and back)
            u = i / 30.0
            z = -0.5 * (1 - abs(2 * u - 1))
            lines.append('{:.2f} head 0 1.7 {:.3f} 0 0 0'.format(t + 6.5 + 5 * u, z))
    return lines


def run(save, out, level, seconds):
    env = {'FAKE_XR_LOG_QUADS': '1'}
    if level:
        env['SAVAGEWIND_VR_VIGNETTE'] = level
    # (90 fps: fake_xr's script time, the game's frames, goes as the clock does)
    targs = argparse.Namespace(save=save, out=out, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                               runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792,
                               place='plateau', boot=180.0, settle=8.0, boot_estimate=60.0, capture_every=0)
    os.makedirs(out, exist_ok=True)
    env['SAVAGEWIND_VR_STEREO'] = '0'
    game, proc, log = T.launch(targs, out, script(), frame_rate=90, extra_env=env)
    samples = []
    try:
        T.ready(targs, game, proc)
        while T.vr_state(game).get('frames', 0) / 90.0 < T0:
            time.sleep(0.5)
        v0 = T.vr_state(game)
        end = time.time() + seconds
        while time.time() < end:
            v = T.vr_state(game)
            samples.append((v.get('stick_motion', 0), v.get('vignette_level', 0)))
            time.sleep(0.25)
        v1 = T.vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    quads = re.findall(r'quad \d+ space (\S+) at (\S+) (\S+) (\S+) size (\S+) (\S+)',
                       open(os.path.join(out, 'fake_xr.log'), errors='replace').read())
    return samples, v0, v1, quads


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/vignette')
    ap.add_argument('--seconds', type=float, default=40)
    a = ap.parse_args()
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    print('== High')
    s, v0, v1, quads = run(a.save, os.path.abspath(a.out + '_high'), 'high', a.seconds)
    pushed = [lv for m, lv in s if m > 0.9]
    rest, quiet = [], 0
    for m, lv in s:
        quiet = quiet + 1 if m == 0 else 0
        if quiet >= 6:
            rest.append(lv)
    print('    {} samples: {} with a stick pushed (level max {:.2f}), {} at rest 1.5 s+ (level max {:.2f})'.format(
        len(s), len(pushed), max(pushed or [0]), len(rest), max(rest or [0])))
    check(pushed and max(pushed) > 0.8, 'a pushed stick closes the vignette in (level {:.2f})'.format(max(pushed or [0])))
    check(rest and max(rest) < 0.02, 'gone 1.5 s after the sticks are let go (level {:.2f})'.format(max(rest or [1])))
    frames = v1.get('vignette_frames', 0) - v0.get('vignette_frames', 0)
    check(frames > 50, '{} headset frames with the vignette'.format(frames))
    view = [q for q in quads if q[0] == 'VIEW' or abs(float(q[4]) - 4) < 0.01]
    check(any(abs(float(q[4]) - 4) < 0.01 and abs(float(q[5]) - 4) < 0.01 for q in view),
          'its quad: 4 m across in the head\'s space ({} quads logged, e.g. {})'.format(len(view), view[:1]))
    rs = v1.get('roomscale_frames', 0) - v0.get('roomscale_frames', 0)
    check(rs > 0, 'roomscale walked Link meanwhile ({} frames), without the vignette'.format(rs))

    print('== Off (the default)')
    s, v0, v1, quads = run(a.save, os.path.abspath(a.out + '_off'), None, 20)
    frames = v1.get('vignette_frames', 0) - v0.get('vignette_frames', 0)
    check(any(m > 0.9 for m, _ in s), 'the sticks were pushed')
    check(frames == 0, 'no frame with the vignette ({})'.format(frames))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
