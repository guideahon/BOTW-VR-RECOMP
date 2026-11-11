#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) Where Link's skeleton is in memory: boots the game into play (as
tools/vr/test_vr_game.py does, against fake_xr) and runs `mem find` for bone names and the pointers to them, then
prints what is around them. Nothing is written to the game.

  python tools/vr/re_bones.py --save <scratch save> --out build/vr/re [--names Clavicle_R,Head,Arm_1_R]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402


def hits(d):
    return [int(d['hit.%d' % i], 16) if isinstance(d.get('hit.%d' % i), str) else int(d['hit.%d' % i])
            for i in range(int(d.get('found', 0)))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    ap.add_argument('--names', default='Clavicle_R,Head,Arm_1_R,Weapon_R')
    ap.add_argument('--game-exe', default=None)
    args, rest = ap.parse_known_args()
    targs = argparse.Namespace(save=args.save, out=args.out, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                               runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792,
                               place='plateau', boot=180.0, settle=8.0, boot_estimate=60.0, capture_every=0,
                               menu_rounds=0)
    os.makedirs(args.out, exist_ok=True)
    game, proc, log = T.launch(targs, args.out, ['0 head 0 1.7 0 0 0 0'], frame_rate=0,
                               extra_env={'SAVAGEWIND_VR_STEREO': '0'})
    try:
        T.ready(targs, game, proc)
        pos = game.cmd('pos', ok=False)
        print('Link at', {k: pos.get(k) for k in ('x', 'y', 'z')})
        for name in args.names.split(','):
            d = game.cmd('mem', 'find', name, '40', ok=False, timeout=120)
            hs = hits(d) if d.get('found') else []
            print('\n{}: {} string(s)'.format(name, len(hs)), ' '.join(hex(h) for h in hs[:12]))
            for h in hs[:6]:
                for at, label in ((h, 'ptr to it'), (h - 2, 'ptr to its length prefix')):
                    p = game.cmd('mem', 'find', 'ptr:{:x}'.format(at), '20', ok=False, timeout=120)
                    ph = hits(p) if p.get('found') else []
                    if ph:
                        print('   {} {:#x}: {}'.format(label, at, ' '.join(hex(x) for x in ph[:10])))
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
