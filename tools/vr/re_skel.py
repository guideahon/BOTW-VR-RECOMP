#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) Link's nn::g3d skeleton object: finds the world-matrix run around Link
(`mem find mtx:`), the pointers to it (the SkeletonObj's world-matrix field), and dumps the object around each, with the
text behind every pointer that reaches a string.

  python tools/vr/re_skel.py --save <scratch save>
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
from re_bonewatch import hits, runs  # noqa: E402


def u64s(game, a, n):
    d = game.cmd('mem', 'read', '{:#x}'.format(a), str(8 * n), 'u64', ok=False)
    return [int(d.get('value.%d' % i, '0'), 0) for i in range(n)]


def text(game, a):
    d = game.cmd('mem', 'read', '{:#x}'.format(a), '32', 'u8', ok=False)
    try:
        bs = bytes(int(d['value.%d' % i], 0) for i in range(32))
    except Exception:
        return ''
    s = bs.split(b'\0')[0]
    return s.decode() if len(s) >= 3 and all(32 <= c < 127 for c in s) else ''


def dump(game, a, before=0x80, n=40):
    base = a - before
    for i, v in enumerate(u64s(game, base, n)):
        t = text(game, v) if 0x7000000000 <= v < 0x7400000000 else ''
        mark = ' <==' if base + 8 * i == a else ''
        print('    {:+#06x} {:#018x} {}{}'.format(8 * i - before, v, t, mark))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    a = ap.parse_args()
    game, proc, log = re_run.boot(a.save, a.out)
    try:
        p = game.cmd('pos', ok=False)
        x, y, z = p['x'], p['y'], p['z']
        d = game.cmd('mem', 'find', 'mtx:{},{},{},1.6'.format(x, y + 1, z), '500', ok=False, timeout=300)
        rr = sorted(runs(hits(d)), key=lambda r: -r[1])
        world, nb, stride = rr[0]
        print('world matrices {:#x} x{} stride {:#x}'.format(world, nb, stride))
        lo, hi = world - 0x30 * 128, world + 0x30
        ph = hits(game.cmd('mem', 'find', 'ptr:{:x}-{:x}'.format(lo, hi), '40', ok=False, timeout=300))
        print('\npointers into [{:#x}, {:#x}): {}'.format(lo, hi, ' '.join(hex(h) for h in ph)))
        for h in ph[:6]:
            print('\n  at {:#x} -> {:#x}'.format(h, u64s(game, h, 1)[0]))
            dump(game, h)
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
