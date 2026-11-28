#!/usr/bin/env python3
"""Climbing in first person: the eye at Link's eyes, off the wall (P10), against fake_xr. By a tree at the Great Plateau's
start (TREE, the one in a player's screenshots, 2026-11-26: "climbing, the head is still very far from the body"): Link
is put at START and walked into the rock face west of it until he climbs; then the `vr` command's eye_above_head and
eye_from_head_flat (the eye against his head bone) are read while he hangs and while he climbs up. Before: 0.38 m over
his head and 0.32 m back; standing it is ~0.1 / ~0.2.

Walking into it is steered: the same head yaw does not walk Link the same way after each `goto` (his heading drifts by
tens of degrees over a run, by a different amount each run). The test used to put him at TREE and try eight
fixed yaws: they missed the face in whole runs, more often with Options > VR > Performance on, which was taken for the
tree missing at Potato draw distance (2026-11-27; what he grips there was always rock, and the pictures and the climbs
are the same with Performance on and off). Every STEER s the yaw is now turned by how far his last steps were off the
line to WALL. From TREE that line crossed a rock ~2 m high, which he got on top of before the "climbing up" sample.

  python tools/vr/test_vr_climb.py --save <scratch save> [--exe build/savagewind] [--out build/vr/climb]
"""
import argparse
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

L = '/user/hand/left/input/thumbstick'
TREE = ('-954.7', '226.5', '1878.4')
START = ('-958.5', '233', '1885.0')   # (6 m east of the rock face's tall part; he drops to the ground at ~231)
WALL = (-965.0, 1884.5)               # (x, z: a point of that face, ~20 m high: he grips it at ~-964.6 1882.8)
STEER = 0.4               # (s: walk this long, then correct the yaw)
CLIMBING = 1 << 7


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/climb')
    ap.add_argument('--above', type=float, default=0.18, help='the eye at most this much over his head bone (m)')
    ap.add_argument('--back', type=float, default=0.22, help='... and this far from it along the ground (m)')
    a = ap.parse_args()
    T.EXE = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    live = os.path.join(out, 'fake_xr_live.txt')
    for p in (live, live + '.ack'):
        if os.path.exists(p):
            os.unlink(p)
    env = {'FAKE_XR_LIVE': live, 'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_RENDER': 'stereo',
           'SAVAGEWIND_VR_ROOMSCALE': '0'}
    game, proc, log = re_run.boot(a.save, out, env, ['0 head 0 1.7 0 0 0 0'])
    seq = [0]

    def send(*lines):
        seq[0] += 1
        with open(live, 'w') as f:
            f.write('seq {}\n{}\n'.format(seq[0], '\n'.join(lines)))
        time.sleep(0.2)

    def state():
        v = game.cmd('vr', ok=False, timeout=30)
        mf = v.get('move_flags', -1)
        return (int(mf) if mf not in (None, -1) else 0), v.get('eye_above_head'), v.get('eye_from_head_flat')

    def pos():
        p = game.cmd('pos', ok=False)
        return float(p['x']), float(p['z'])

    def angle(dx, dz):   # (deg about +y: 0 along +z, 90 along +x; a head yaw turns the walk the same way)
        return math.degrees(math.atan2(dx, dz))

    ok, samples, side = True, [], None
    try:
        for attempt in range(4):
            game.cmd('goto', *START, ok=False, timeout=60)
            time.sleep(4)
            yaw = 0.0
            send('head 0 1.7 0 {:.1f} 0 0'.format(yaw), 'stick {} 0 1'.format(L))
            last, t_end = pos(), time.time() + 6
            while time.time() < t_end and side is None:
                time.sleep(STEER)
                if state()[0] & CLIMBING:
                    side = attempt
                    break
                now = pos()
                dx, dz = now[0] - last[0], now[1] - last[1]
                if dx * dx + dz * dz > 0.04:   # (moved: turn by how far off the line his steps were)
                    err = (angle(WALL[0] - now[0], WALL[1] - now[1]) - angle(dx, dz) + 180) % 360 - 180
                    yaw = (yaw + err + 180) % 360 - 180
                    send('head 0 1.7 0 {:.1f} 0 0'.format(yaw), 'stick {} 0 1'.format(L))
                last = now
            if side is not None:
                break
            send('stick {} 0 0'.format(L))
        if side is not None:
            send('stick {} 0 0'.format(L))
            time.sleep(1.5)
            for k in range(4):
                samples.append(('hanging',) + state())
                time.sleep(0.7)
            send('stick {} 0 1'.format(L))
            time.sleep(1)
            samples.append(('climbing up',) + state())
            send('stick {} 0 0'.format(L))
    finally:
        perf_routes.stop_game(game, proc, log)

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    check(side is not None, 'Link climbs the rock face (walking into it, attempt {})'.format(side))
    for what, mf, above, flat in samples:
        if not mf & CLIMBING:
            print('  --   {}: not climbing any more (flags {:#x})'.format(what, mf))
            continue
        check(above is not None and abs(above) <= a.above and flat is not None and flat <= a.back,
              '{}: the eye {:.2f} m over his head, {:.2f} m from it along the ground (at most {} / {})'.format(
                  what, above if above is not None else -1, flat if flat is not None else -1, a.above, a.back))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
