#!/usr/bin/env python3
"""Moving hands at one place in both eyes (26.2's tester, Stereo, first person: "the left and right hand rendering when
moving gives a crosseyed effect"), against fake_xr with the eyes 0 m apart: both pictures must be one, so Link's hands
must sit at the same place in both while the controllers sweep.

A real runtime's prediction for one display time moves on as new tracking comes: a hand located again later in the
same headset frame is somewhere else while it moves. fake_xr gives the same pose for the same time, which hid it;
FAKE_XR_HAND_DRIFT (--drift ns) makes each hand location since the last xrWaitFrame that much later, as a runtime
would. In Stereo the right eye's frame is drawn later than the left one's: located again, its hands were elsewhere.

  python tools/vr/test_vr_hands_moving.py --save <scratch save> [--exe build/savagewind] [--mode stereo|multiview|mono]
        [--drift 2000000] [--out build/vr/hands_moving]
Passes when every capture has the hands at dx 0 between the eyes (and, in Stereo, right frames used their left
frame's hands: the `vr` command's pair_hands_kept).
"""
import argparse
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402
from test_vr_stereo_sync import hands_shift  # noqa: E402

MODES = {'stereo': {'SAVAGEWIND_VR_RENDER': 'stereo'},
         'multiview': {'SAVAGEWIND_VR_RENDER': 'multiview', 'SAVAGEWIND_VR_EXPERIMENTAL': '1'},
         'mono': {'SAVAGEWIND_VR_RENDER': 'mono'}}


def script(seconds=900, step=0.05, period=0.61):
    """the hands ahead, sweeping 20 cm left and right without stopping (a sine every `period` s, keyframes `step` apart:
    fake_xr interpolates between them; the period is not a multiple of a frame, so a capture never catches them still)"""
    lines = ['0 head 0 1.7 0 0 0 0']
    for k in range(int(seconds / step)):
        t = k * step
        x = 0.10 * math.sin(2 * math.pi * t / period)
        lines.append('%.2f hand right %.3f 1.58 -0.40 0 0 0' % (t, 0.12 + x))
        lines.append('%.2f hand left %.3f 1.58 -0.40 0 0 0' % (t, -0.12 + x))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/hands_moving')
    ap.add_argument('--mode', choices=sorted(MODES), default='stereo')
    ap.add_argument('--drift', type=int, default=2000000)
    ap.add_argument('--shots', type=int, default=6)
    a = ap.parse_args()
    T.EXE = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    live = os.path.join(out, 'fake_xr_live.txt')
    for p in (live, live + '.ack'):
        if os.path.exists(p):
            os.unlink(p)
    env = {'FAKE_XR_LIVE': live, 'FAKE_XR_IPD': '0', 'FAKE_XR_CAPTURE_EVERY': '0', 'SAVAGEWIND_VR_STEREO': '1',
           'SAVAGEWIND_VR_ROOMSCALE': '0', 'FAKE_XR_HAND_DRIFT': str(a.drift)}
    env.update(MODES[a.mode])
    game, proc, log = re_run.boot(a.save, out, env, script())
    seq = [0]

    def send(*lines):
        seq[0] += 1
        with open(live, 'w') as f:
            f.write('seq {}\n{}\n'.format(seq[0], '\n'.join(lines)))
        time.sleep(0.3)

    def flog():
        return T.fake_log(out)

    tags = []
    try:
        v0 = T.vr_state(game)
        for k in range(a.shots):
            tag = 'move%d' % k
            send('shot %s' % tag)
            t0 = time.monotonic()
            while 'capture %s_' % tag not in flog() and time.monotonic() - t0 < 90:
                time.sleep(0.3)
            tags.append(tag)
            time.sleep(0.7)
        v1 = T.vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    fov = dict((t, [float(x) for x in f]) for t, *f in re.findall(r'capture (\S+) fov (\S+) (\S+) (\S+) (\S+)', flog()))
    ok = True
    for tag in tags:
        lt = next((t for t in sorted(fov) if re.fullmatch(re.escape(tag) + r'(_layer\d+)?_view0', t)), None)
        if not lt:
            print('  FAIL %s: no capture' % tag)
            ok = False
            continue
        A = T.read_ppm_angles(os.path.join(out, 'cap', lt + '.ppm'), fov[lt])
        B = T.read_ppm_angles(os.path.join(out, 'cap', lt[:-1] + '1.ppm'), fov[lt[:-1] + '1'])
        dx, at, unshifted = hands_shift(A, B)
        good = abs(dx) <= 1
        ok &= good
        print('  %s %s: the hands, right eye against left: dx %d (0.005-tangent samples), difference %.1f there, %.1f '
              'unshifted' % ('ok  ' if good else 'FAIL', tag, dx, at, unshifted))
    kept = v1.get('pair_hands_kept', 0) - v0.get('pair_hands_kept', 0)
    pairs = v1.get('stereo_pairs', 0) - v0.get('stereo_pairs', 0)
    print('  --   right frames drawn with their left frame\'s hands: %d (pairs %d)' % (kept, pairs))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
