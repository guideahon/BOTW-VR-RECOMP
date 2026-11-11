#!/usr/bin/env python3
"""Roomscale (docs/VR.md), against fake_xr, first person: the head steps 50 cm to the right in the room and stays there.
With roomscale on, Link walks under it: he moves about as far, and ends with the drawn eye (the camera, read from the
TAA matrix) above him again (within 25 cm: the eye sits 18 cm ahead of his centre) while the eye itself stayed where the head put it. With it off (a second step,
`set vr_roomscale 1`), Link does not move and the eye moves away from him by the step.

  python tools/vr/test_vr_roomscale.py --save <scratch save> [--out build/vr/roomscale]
"""
import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/roomscale')
    a = ap.parse_args()
    # one step right at T (roomscale on), back at T + 20 s, and a step right again at T + 30 s (roomscale turned off
    # before); T well after the game is up
    T = 160
    script = ['0 head 0 1.7 0 0 0 0', '%d head 0 1.7 0 0 0 0' % T, '%.1f head 0.5 1.7 0 0 0 0' % (T + 0.5),
              '%d head 0.5 1.7 0 0 0 0' % (T + 20), '%.1f head 0 1.7 0 0 0 0' % (T + 20.5),
              '%d head 0 1.7 0 0 0 0' % (T + 30), '%.1f head 0.5 1.7 0 0 0 0' % (T + 30.5)]
    game, proc, log = re_run.boot(a.save, a.out, {'FAKE_XR_CAPTURE_EVERY': '0'}, script)
    ok = True

    def check(cond, what):
        nonlocal ok
        ok &= bool(cond)
        print(('  ok   ' if cond else '  FAIL ') + what)

    def at(t):
        while game.cmd('vr', ok=False).get('frames', 0) / 90.0 < t:
            time.sleep(0.05)
        p, c = game.cmd('pos', ok=False), game.cmd('camera', ok=False)
        return (p['x'], p['z']), (c['x'], c['z'])

    def dist(u, v):
        return math.hypot(u[0] - v[0], u[1] - v[1])

    try:
        link0, eye0 = at(T - 1)
        f0 = game.cmd('vr', ok=False).get('roomscale_frames', 0)
        link1, eye1 = at(T + 1)          # right after the step: the eye moved, Link not yet much
        link2, eye2 = at(T + 6)          # settled
        f1 = game.cmd('vr', ok=False).get('roomscale_frames', 0)
        print('Link moved {:.2f} m, the eye {:.2f} m (then {:.2f} m more while he caught up); eye to Link at the end '
              '{:.2f} m; roomscale on {} cameras'.format(dist(link0, link2), dist(eye0, eye1), dist(eye1, eye2),
                                                          dist(eye2, link2), f1 - f0))
        check(0.3 < dist(link0, link2) < 0.8, 'roomscale on: Link walked about the step')
        check(dist(eye1, eye2) < 0.15, 'the eye stayed where the head put it while he walked')
        check(dist(eye2, link2) < 0.25, 'the eye ended above Link again (its 0.18 m ahead of his centre)')
        check(f1 > f0, 'roomscale walked him')
        game.cmd('set', 'vr_roomscale', '1', ok=False)
        link3, eye3 = at(T + 29)
        link4, eye4 = at(T + 34)
        print('roomscale off: Link moved {:.2f} m, the eye {:.2f} m'.format(dist(link3, link4), dist(eye3, eye4)))
        check(dist(link3, link4) < 0.1 and dist(eye3, eye4) > 0.35, 'roomscale off: the eye moves alone')
    finally:
        perf_routes.stop_game(game, proc, log)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
