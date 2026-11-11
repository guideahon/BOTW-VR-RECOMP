#!/usr/bin/env python3
"""Head-only turning in first person, against fake_xr and the game.

  python tools/vr/test_vr_head_turn.py --save <scratch save> [--out build/vr/head_turn]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

R = '/user/hand/right/input/thumbstick'
CYCLE = 40


def script():
    lines = []
    for k in range(50):
        t = k * CYCLE
        lines += ['%d head 0 1.7 0 0 0 0' % t, '%d stick %s 0 0' % (t, R),
                  '%d stick %s 1 0' % (t + 12, R), '%d stick %s 0 0' % (t + 16, R),
                  '%d head 0 1.7 0 30 0 0' % (t + 24), '%d head 0 1.7 0 0 0 0' % (t + 32)]
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/head_turn')
    a = ap.parse_args()
    game, proc, log = re_run.boot(a.save, a.out, {'SAVAGEWIND_VR_STEREO': '0'}, script())
    try:
        game.cmd('set', 'vr_snap_turn', '2')
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90  # noqa: E731
        base = (int(now() / CYCLE) + 1) * CYCLE

        def wait(offset):
            while now() < base + offset:
                time.sleep(0.1)
            return T.camera(game)

        before = wait(8)
        stick = wait(19)
        head = wait(27)
        after = wait(36)
    finally:
        perf_routes.stop_game(game, proc, log)
    if not all((before, stick, head, after)):
        print('FAIL: the camera could not be read')
        return 1
    ds = T.angle_diff(stick['yaw'], before['yaw'])
    dh = T.angle_diff(head['yaw'], stick['yaw'])
    dr = T.angle_diff(after['yaw'], before['yaw'])
    ok = abs(ds) < 3 and abs(dh - 30) < 3 and abs(dr) < 3
    print('stick {:.1f} deg; head {:.1f} deg; after return {:.1f} deg: {}'.format(
        ds, dh, dr, 'PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
