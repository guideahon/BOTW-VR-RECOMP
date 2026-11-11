#!/usr/bin/env python3
"""The VR options apply at once (docs/VR.md), against fake_xr: in play, with no SAVAGEWIND_VR_* variable, the options
the Options screen's VR page sets (control `set vr_<key> <value>`, the same fields) change what the runtime does:
  vr_view 1 / 0          the eye leaves Link's head and comes back (vr status first_person)
  vr_rumble 1 / 0        a rumble (`vr rumble r 0.8`) reaches the controller only when on (rumble_pulses)
  vr_snap_turn 1         the right stick pushed right turns the view one step (the game camera's yaw jumps ~45 deg)
  vr_height_offset 20    the eye 20 cm higher
  vr_hud_distance / _size / _follows_gaze   the HUD layer's distance, width, and whether it turns with the head
                        (fake_xr's FAKE_XR_LOG_QUADS lines)

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
    script = ['0 head 0 1.7 0 0 0 0', '70 stick %s 1 0' % RS, '70.6 stick %s 0 0' % RS,
              '96 head 0 1.7 0 0 0 0', '96.5 head 0 1.7 0 30 0 0']   # (then the head turned 30 deg left)
    game, proc, log = re_run.boot(a.save, a.out, {'FAKE_XR_CAPTURE_EVERY': '0', 'FAKE_XR_LOG_QUADS': '1'}, script)
    xrlog = os.path.join(a.out, 'fake_xr.log')

    def hud():   # the last HUD quad: its space, centre (room) and width
        q = [l.split() for l in open(xrlog, errors='replace') if l.startswith('quad ')][-1]
        return q[3], float(q[5]), float(q[6]), float(q[7]), float(q[9])
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
        game.cmd('set', 'vr_snap_turn', '0', ok=False)
        # the eye's height
        y0 = game.cmd('camera', ok=False).get('y', 0)
        game.cmd('set', 'vr_height_offset', '20', ok=False)
        time.sleep(1)
        y1 = game.cmd('camera', ok=False).get('y', 0)
        game.cmd('set', 'vr_height_offset', '0', ok=False)
        check(0.17 < y1 - y0 < 0.23, 'vr_height_offset 20: the eye 20 cm higher ({:.3f} m)'.format(y1 - y0))
        # the HUD: its distance and size, then following the gaze or not (the head turns 30 deg at 96.5 s)
        sp, x, y, z, w = hud()
        check(sp == 'view' and abs(abs(z) - 1.4) < 0.05 and abs(w - 1.6) < 0.01,
              'the HUD by default: with the gaze, 1.4 m ahead, 1.6 m wide ({} {:.2f} m, {:.2f} m)'.format(sp, abs(z), w))
        game.cmd('set', 'vr_hud_distance', '200', ok=False)
        game.cmd('set', 'vr_hud_size', '150', ok=False)
        time.sleep(1)
        sp, x, y, z, w = hud()
        check(abs(abs(z) - 2.0) < 0.05 and abs(w - 2.4) < 0.01,
              'vr_hud_distance 200, vr_hud_size 150: 2 m ahead, 2.4 m wide ({:.2f} m, {:.2f} m)'.format(abs(z), w))
        while now() < 98:
            time.sleep(0.1)
        sp, x, y, z, w = hud()
        check(sp == 'view' and abs(x) > 0.7, 'the head turned 30 deg: the HUD went with it (x {:.2f} m)'.format(x))
        game.cmd('set', 'vr_hud_follows_gaze', '1', ok=False)
        time.sleep(1)
        sp, x, y, z, w = hud()
        check(sp == 'local' and abs(x) < 0.15 and z < -1.5,
              'vr_hud_follows_gaze off: the HUD stays ahead of the body ({} x {:.2f} z {:.2f} m)'.format(sp, x, z))
    finally:
        perf_routes.stop_game(game, proc, log)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
