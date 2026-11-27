#!/usr/bin/env python3
"""Climbing in first person: the eye at Link's eyes, off the wall (P10), against fake_xr. A tree by the Great Plateau's
start (the one in a player's screenshots, 2026-11-26: "climbing, the head is still very far from the body"): Link is
put beside it and walked into it from several sides until he climbs; then the `vr` command's eye_above_head and
eye_from_head_flat (the eye against his head bone) are read while he hangs and while he climbs up. Before: 0.38 m over
his head and 0.32 m back; standing it is ~0.1 / ~0.2.

  python tools/vr/test_vr_climb.py --save <scratch save> [--exe build/savagewind] [--out build/vr/climb]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

L = '/user/hand/left/input/thumbstick'
TREE = ('-954.7', '226.5', '1878.4')
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

    ok, samples, side = True, [], None
    try:
        for yaw in (225, 180, 270, 135, 315, 0, 45, 90):
            game.cmd('goto', *TREE, ok=False, timeout=60)
            time.sleep(4)
            send('head 0 1.7 0 {} 0 0'.format(yaw), 'stick {} 0 1'.format(L))
            time.sleep(3.5)
            if state()[0] & CLIMBING:
                side = yaw
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

    check(side is not None, 'Link climbs the tree (walking into it at yaw {})'.format(side))
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
