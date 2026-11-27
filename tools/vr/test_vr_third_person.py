#!/usr/bin/env python3
"""Third person in Stereo: the view is the game's camera turned by the head, in every camera update and both eyes
(SAVAGEWIND_VR_LOG_CAMERA's [vr-cam] lines), against fake_xr.

Two bugs it guards against:
- 25.9's tester: orbiting with the right stick, the left eye kept the last frame's heading - its view 0.4 deg (up to
  1.6) off the game's camera while the right one was 1.6 (3.1); fixed in 25.10 (vr_camera.c, the pair's heading kept in
  first person only). Checked: orbiting, the left eye's view against its game camera.
- 26.2's tester ("the rotation is broken"): with the head turned, the game handed back our own camera with its at point
  at another distance, which got the head's turn again - the view swung between the game's way plus the head's turn and
  plus twice it, frame to frame and eye to eye (22% of the updates). Checked: the head held at 30, 60, 90 and 120 deg,
  and turned while orbiting: view = game + head within --tol degrees in every update.

  python tools/vr/test_vr_third_person.py --save <scratch save> [--exe build/savagewind] [--mode stereo|multiview|mono]
        [--tol 1.0] [--out build/vr/third_person]
"""
import argparse
import os
import re
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

R = '/user/hand/right/input/thumbstick'
MODES = {'stereo': {'SAVAGEWIND_VR_RENDER': 'stereo'},
         'multiview': {'SAVAGEWIND_VR_RENDER': 'multiview', 'SAVAGEWIND_VR_EXPERIMENTAL': '1'},
         'mono': {'SAVAGEWIND_VR_RENDER': 'mono'}}
ROW = re.compile(r'^\[vr-cam\] eye (-?\d) head \S+ \S+ \S+ heading (\S+) \| game eye \S+ \S+ \S+ yaw (\S+) \| view yaw (\S+)',
                 re.M)


def wrap(a):
    return (a + 180) % 360 - 180


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/third_person')
    ap.add_argument('--mode', choices=sorted(MODES), default='stereo')
    ap.add_argument('--tol', type=float, default=1.0)
    a = ap.parse_args()
    T.EXE = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    live = os.path.join(out, 'fake_xr_live.txt')
    for p in (live, live + '.ack'):
        if os.path.exists(p):
            os.unlink(p)
    env = {'FAKE_XR_LIVE': live, 'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_LOG_CAMERA': '1', 'SAVAGEWIND_VR_VIEW': 'third',
           'SAVAGEWIND_VR_ROOMSCALE': '0'}
    env.update(MODES[a.mode])
    game, proc, log = re_run.boot(a.save, out, env, ['0 head 0 1.7 0 0 0 0'])
    seq = [0]
    gl = os.path.join(out, 'game.log')
    marks = []

    def send(*lines):
        seq[0] += 1
        with open(live, 'w') as f:
            f.write('seq {}\n{}\n'.format(seq[0], '\n'.join(lines)))
        time.sleep(0.2)

    def mark(name):
        marks.append((name, len(ROW.findall(open(gl, encoding='utf-8', errors='replace').read()))))

    try:
        time.sleep(2)
        mark('orbit')
        send('stick {} 0.6 0'.format(R))
        time.sleep(4)
        send('stick {} 0 0'.format(R))
        time.sleep(1)
        for yaw in (30, 60, 90, 120):
            send('head 0 1.7 0 {} 0 0'.format(yaw))
            time.sleep(0.5)
            mark('head%d' % yaw)
            time.sleep(2.5)
        mark('orbit_head120')
        send('stick {} 0.6 0'.format(R))
        time.sleep(4)
        send('stick {} 0 0'.format(R))
        mark('end')
        time.sleep(1)
    finally:
        perf_routes.stop_game(game, proc, log)
    rows = ROW.findall(open(gl, encoding='utf-8', errors='replace').read())
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    bounds = {name: (marks[i][1], marks[i + 1][1]) for i, (name, _) in enumerate(marks[:-1])}
    # orbiting, the head ahead: each eye's view against its own game camera
    lo, hi = bounds['orbit']
    seg = rows[lo:hi]
    moving, prev = {0: [], 1: [], -1: []}, None
    for e, h, g, v in seg:
        g = float(g)
        if prev is not None and abs(wrap(g - prev)) > 0.05:
            moving[int(e)].append(abs(wrap(float(v) - g)))
        prev = g
    left = moving[0] or moving[-1]
    check(len(left) > 20, 'orbiting: the view followed the game camera in {} updates'.format(len(left)))
    if left:
        check(statistics.mean(left) < 0.2,
              'orbiting: the left eye\'s view on its own game camera (mean {:.2f} deg, max {:.2f}; 25.9: 0.40 / 1.60)'.format(
                  statistics.mean(left), max(left)))
    if moving[1]:
        print('  --   the right eye: mean {:.2f} deg, max {:.2f} (Stereo draws it from the pair\'s left anchor, on '
              'purpose)'.format(statistics.mean(moving[1]), max(moving[1])))
    # the head held turned, and turned while orbiting: view = game + head in every update
    for name in ('head30', 'head60', 'head90', 'head120', 'orbit_head120'):
        lo, hi = bounds[name]
        off = [abs(wrap(float(v) - float(g) - float(h))) for e, h, g, v in rows[lo:hi]]
        bad = sum(1 for o in off if o > a.tol)
        check(off and bad == 0, '{}: view = game camera + head in {} of {} updates (worst {:.1f} deg; 26.2: 22% off)'.format(
            name, len(off) - bad, len(off), max(off) if off else 0))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
