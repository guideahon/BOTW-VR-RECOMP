#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) Link's skeleton in one run: his model (named "Link" by its ResModel,
among the ones main+0xf978e8 calculates), its SkeletonObj (model + 0x118), the ResSkeleton (FSKL) and the bone names
it points at, in the order they are found, and the local array's entries for the bones asked for.

  python tools/vr/re_link.py --save <scratch save>
"""
import argparse
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
from re_model import fmdl_name, raw  # noqa: E402
from re_skel import u64s  # noqa: E402


def read_u64s(game, a, n):
    out = []
    for off in range(0, 8 * n, 4096):
        k = min(4096, 8 * n - off)
        d = game.cmd('mem', 'read', '{:#x}'.format(a + off), str(k), 'u64', ok=False)
        out += [int(d.get('value.%d' % i, '0'), 0) for i in range(k // 8)]
    return out


def string_at(game, p):
    b = raw(game, p, 40)
    for skip in (2, 0):
        s = b[skip:].split(b'\0')[0]
        if len(s) >= 2 and all(32 <= c < 127 for c in s):
            return s.decode()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    a = ap.parse_args()
    env = {'SAVAGEWIND_FNPROF': '0:9999999', 'SAVAGEWIND_FNPROF_ARGS': '7000f978e8',
           'SAVAGEWIND_FNPROF_ARGS_MAX': '200000', 'SAVAGEWIND_FNPROF_DEREF': '0:0'}
    game, proc, log = re_run.boot(a.save, a.out, env)
    try:
        time.sleep(2)
        logtext = open(os.path.join(a.out, 'game.log'), errors='replace').read()
        models = sorted(set(int(m, 16) for m in re.findall(r'\[fnprof-args\] 7000f978e8 from \S+: (\S+)', logtext)))
        link = [m for m in models if fmdl_name(game, u64s(game, m + 0xe0, 1)[0]) == 'Link']
        print('Link models:', [hex(m) for m in link])
        m = link[0]
        skel = u64s(game, m + 0x118, 1)[0]
        f = read_u64s(game, skel, 12)
        print('SkeletonObj {:#x}: '.format(skel) + ' '.join('{:#x}'.format(v) for v in f))
        fskl, local = f[0], f[3]
        nbones = f[10] & 0xffff
        print('FSKL {:#x} magic {}, local array {:#x}, bones field {:#x}'.format(fskl, raw(game, fskl, 4), local, f[10]))
        hdr = read_u64s(game, fskl, 16)
        print('FSKL header: ' + ' '.join('{:#x}'.format(v) for v in hdr))
        # every pointer in the FSKL's first 0x80 bytes: follow it, and list the names reachable from there
        for k, v in enumerate(hdr):
            if not (0x7000000000 <= v < 0x7400000000):
                continue
            arr = read_u64s(game, v, 64)
            names = []
            for j, q in enumerate(arr):
                if 0x7000000000 <= q < 0x7400000000:
                    s = string_at(game, q)
                    if s:
                        names.append((j, s))
            if names:
                print('  FSKL+{:#x} -> {:#x}: {}'.format(8 * k, v, ', '.join('[{}]{}'.format(j, s) for j, s in names[:40])))
        # the bone array: entries with a name pointer at a fixed stride
        print('local array entries (0x60 each): scale | translation')
        for i in range(min(nbones or 121, 121)):
            b = raw(game, local + 0x60 * i, 0x60)
            if len(b) < 0x60:
                break
            fl = struct.unpack('<24f', b)
            if i < 8 or i % 10 == 0:
                print('  [{:3}] scale {:.2f} {:.2f} {:.2f}  t {:.3f} {:.3f} {:.3f}'.format(i, *fl[4:7], *fl[20:23]))
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
