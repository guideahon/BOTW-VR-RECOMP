#!/usr/bin/env python3
"""(reverse engineering) The strings a resource points at: reads <len> bytes at <addr> as u64, and for each that points
into guest memory prints the text there (bfres strings: a u16 length, then the text). Boots the game first.

  python tools/vr/re_names.py --save <scratch save> <addr hex> <len>
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    ap.add_argument('addr')
    ap.add_argument('length', type=int)
    a = ap.parse_args()
    game, proc, log = re_run.boot(a.save, a.out)
    try:
        base = int(a.addr, 16)
        vals = []
        for off in range(0, a.length, 4096):
            n = min(4096, a.length - off)
            d = game.cmd('mem', 'read', '{:#x}'.format(base + off), str(n), 'u64', ok=False)
            vals += [int(d.get('value.%d' % i, '0'), 0) for i in range(n // 8)]
        for i, v in enumerate(vals):
            if not (0x7000000000 <= v < 0x7400000000):
                continue
            r = game.cmd('mem', 'read', '{:#x}'.format(v), '40', 'u8', ok=False)
            try:
                b = bytes(int(r['value.%d' % k], 0) for k in range(40))
            except Exception:
                continue
            for skip in (2, 0):
                s = b[skip:].split(b'\0')[0]
                if len(s) >= 2 and all(32 <= c < 127 for c in s):
                    print('{:#x} (+{:#x}) -> {:#x} "{}"'.format(base + 8 * i, 8 * i, v, s.decode()))
                    break
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
