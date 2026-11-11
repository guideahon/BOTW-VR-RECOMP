#!/usr/bin/env python3
"""The VR options apply at once (docs/VR.md), against fake_xr: in play, with no SAVAGEWIND_VR_* variable, the options
the Options screen's VR page sets (control `set vr_<key> <value>`, the same fields) change what the runtime does:
  vr_view 1 / 0          the eye leaves Link's head and comes back (vr status first_person)
  vr_rumble 1 / 0        a rumble (`vr rumble r 0.8`) reaches the controller only when on (rumble_pulses)
  vr_snap_turn 1         the right stick pushed right turns the view one step (the game camera's yaw jumps ~45 deg)

  python tools/vr/test_vr_options.py --save <scratch save> [--out build/vr/options]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

RS = '/user/hand/right/input/thumbstick'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/options')
    a = ap.parse_args()
    # the right stick pushed right once, at 70 s of fake_xr time (well after the checks before it)
    script = ['0 head 0 1.7 0 0 0 0', '70 stick %s 1 0' % RS, '70.6 stick %s 0 0' % RS]
    game, proc, log = re_run.boot(a.save, a.out, {'FAKE_XR_CAPTURE_EVERY': '0'}, script)
    ok = True

    def check(cond, what):
        nonlocal ok
        ok &= bool(cond)
        print(('  ok   ' if cond else '  FAIL ') + what)

    try:
        st = lambda: game.cmd('vr', ok=False)  # noqa: E731
        check(st().get('first_person') is True, 'first person by default')
        game.cmd('set', 'vr_view', '1', ok=False)
        time.sleep(1.5)
        check(st().get('first_person') is False, 'vr_view 1: third person at once')
        game.cmd('set', 'vr_view', '0', ok=False)
        time.sleep(1.5)
        check(st().get('first_person') is True, 'vr_view 0: first person again')
        p0 = st().get('rumble_pulses', 0)
        game.cmd('vr', 'rumble', 'r', '0.8', ok=False)
        time.sleep(0.6)
        game.cmd('vr', 'rumble', 'r', '0', ok=False)
        p1 = st().get('rumble_pulses', 0)
        check(p1 > p0, 'vibration on: a rumble reaches the controller ({} pulses)'.format(p1 - p0))
        game.cmd('set', 'vr_rumble', '1', ok=False)
        time.sleep(0.3)
        game.cmd('vr', 'rumble', 'r', '0.8', ok=False)
        time.sleep(0.6)
        game.cmd('vr', 'rumble', 'r', '0', ok=False)
        p2 = st().get('rumble_pulses', 0)
        check(p2 == p1, 'vr_rumble 1 (off): none ({} pulses)'.format(p2 - p1))
        game.cmd('set', 'vr_snap_turn', '1', ok=False)
        now = lambda: st().get('frames', 0) / 90.0  # noqa: E731
        while now() < 69:
            time.sleep(0.1)
        y0 = game.cmd('camera', ok=False).get('yaw')
        while now() < 72:
            time.sleep(0.1)
        y1 = game.cmd('camera', ok=False).get('yaw')
        turn = None if y0 is None or y1 is None else ((y1 - y0 + 180) % 360) - 180
        check(turn is not None and 35 < abs(turn) < 55, 'vr_snap_turn 1: one push turns one step ({} deg)'.format(
            None if turn is None else round(turn, 1)))
    finally:
        perf_routes.stop_game(game, proc, log)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
