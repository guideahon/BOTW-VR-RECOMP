#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) Which model is Link's: the models main+0xf978e8 calculates (logged by
SAVAGEWIND_FNPROF_ARGS), named from their ResModel ("FMDL", the first field of the nn::g3d ModelObj at x0 + 0xe0);
for the ones named like Link's, the ModelObj and every pointer in it that reaches a ResSkeleton ("FSKL") holder.

  python tools/vr/re_model.py --save <scratch save> [--match Link]
"""
import argparse
import collections
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
from re_skel import u64s, text  # noqa: E402


def raw(game, a, n):
    d = game.cmd('mem', 'read', '{:#x}'.format(a), str(n), 'u8', ok=False)
    try:
        return bytes(int(d['value.%d' % i], 0) for i in range(n))
    except Exception:
        return b''


def fmdl_name(game, res):
    head = raw(game, res, 0x20)
    if head[:4] != b'FMDL':
        return '?'
    for off in range(8, 0x20, 8):   # (a pointer to the name, possibly length-prefixed)
        p = int.from_bytes(head[off:off + 8], 'little')
        if 0x7000000000 <= p < 0x7400000000:
            for skip in (0, 2):
                t = text(game, p + skip)
                if t:
                    return t
    return '?'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    ap.add_argument('--match', default='Link')
    a = ap.parse_args()
    env = {'SAVAGEWIND_FNPROF': '0:9999999', 'SAVAGEWIND_FNPROF_ARGS': '7000f978e8',
           'SAVAGEWIND_FNPROF_ARGS_MAX': '200000', 'SAVAGEWIND_FNPROF_DEREF': '0:0'}
    game, proc, log = re_run.boot(a.save, a.out, env)
    try:
        time.sleep(3)
        logtext = open(os.path.join(a.out, 'game.log'), errors='replace').read()
        models = sorted(set(int(m, 16) for m in re.findall(r'\[fnprof-args\] 7000f978e8 from \S+: (\S+)', logtext)))
        names = collections.OrderedDict()
        for m in models:
            names[m] = fmdl_name(game, u64s(game, m + 0xe0, 1)[0])
        print('{} models: {}'.format(len(models), ', '.join(sorted(set(names.values())))))
        for m, n in names.items():
            if a.match.lower() not in n.lower():
                continue
            print('\n== model {:#x} "{}"'.format(m, n))
            f = u64s(game, m + 0xe0, 32)
            for i, v in enumerate(f):
                tag = ''
                if 0x7000000000 <= v < 0x7400000000:
                    h = raw(game, v, 8)
                    tag = h[:4].decode('latin-1') if h[:4].isalnum() else ''
                    inner = u64s(game, v, 4)
                    tag += ' -> ' + ' '.join('{:#x}'.format(q) for q in inner)
                print('   mobj+{:#05x} {:#018x} {}'.format(8 * i, v, tag))
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
