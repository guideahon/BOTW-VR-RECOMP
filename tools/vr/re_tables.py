#!/usr/bin/env python3
"""(reverse engineering) Prints guest tables of string pointers: for each address, 64 u64 slots around it, and the
text each slot points at. Boots the game into play first (as re_bones.py).

  python tools/vr/re_tables.py --save <scratch save> 7001e14228 7001db1e30 ...
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402


def read(game, addr, n, typ='u64'):
    d = game.cmd('mem', 'read', '{:#x}'.format(addr), str(n), typ, ok=False)
    vals = d.get('values') if isinstance(d, dict) else None
    if vals is None and isinstance(d, dict):
        vals = [d[k] for k in sorted((k for k in d if k.startswith('value.')), key=lambda k: int(k.split('.')[1]))]
    return vals or [], d


def cstr(game, addr):
    vals, d = read(game, addr, 48, 'u8')
    try:
        bs = bytes(int(v, 0) if isinstance(v, str) else int(v) for v in vals)
    except Exception:
        return None
    s = bs.split(b'\0')[0]
    return s.decode('ascii') if s and all(32 <= c < 127 for c in s) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    ap.add_argument('--before', type=int, default=16)
    ap.add_argument('--slots', type=int, default=48)
    ap.add_argument('addrs', nargs='+')
    args = ap.parse_args()
    targs = argparse.Namespace(save=args.save, out=args.out, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                               runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792,
                               place='plateau', boot=180.0, settle=8.0, boot_estimate=60.0, capture_every=0,
                               menu_rounds=0)
    os.makedirs(args.out, exist_ok=True)
    game, proc, log = T.launch(targs, args.out, ['0 head 0 1.7 0 0 0 0'], frame_rate=0,
                               extra_env={'SAVAGEWIND_VR_STEREO': '0'})
    try:
        T.ready(targs, game, proc)
        first = True
        for a in args.addrs:
            base = int(a, 16) - 8 * args.before
            vals, d = read(game, base, 8 * args.slots)
            if first:
                print('(mem read reply keys: {})'.format(list(d)[:6]))
                first = False
            print('\n== {:#x}'.format(int(a, 16)))
            for i, v in enumerate(vals):
                v = int(v, 0) if isinstance(v, str) else int(v)
                txt = cstr(game, v) if 0x7000000000 <= v < 0x7400000000 else None
                print('  {:#x}: {:#018x} {}'.format(base + 8 * i, v, txt or ''))
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
