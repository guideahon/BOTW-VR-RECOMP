#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) Which model's skeleton is Link's: the models main+0xf978e8 calculates
(logged by SAVAGEWIND_FNPROF_ARGS), their nn::g3d ModelObj (x0 + 0xe0) and SkeletonObj, matched against the run of
world matrices around Link (`mem find mtx:`). Prints the matching model and its SkeletonObj, field by field.

  python tools/vr/re_model.py --save <scratch save>
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
from re_bonewatch import hits, runs  # noqa: E402
from re_skel import u64s, text  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    a = ap.parse_args()
    env = {'SAVAGEWIND_FNPROF': '0:9999999', 'SAVAGEWIND_FNPROF_ARGS': '7000f978e8', 'SAVAGEWIND_FNPROF_ARGS_MAX': '200000', 'SAVAGEWIND_FNPROF_DEREF': '0:0'}
    game, proc, log = re_run.boot(a.save, a.out, env)
    try:
        time.sleep(3)
        p = game.cmd('pos', ok=False)
        x, y, z = p['x'], p['y'], p['z']
        d = game.cmd('mem', 'find', 'mtx:{},{},{},1.6'.format(x, y + 1, z), '500', ok=False, timeout=300)
        rr = sorted(runs(hits(d)), key=lambda r: -r[1])
        w0, nb, _ = rr[0]
        print('world run {:#x} x{}'.format(w0, nb))
        logtext = open(os.path.join(a.out, 'game.log'), errors='replace').read()
        models = sorted(set(int(m, 16) for m in re.findall(r'\[fnprof-args\] 7000f978e8 from \S+: (\S+)', logtext)))
        print('{} models'.format(len(models)))
        for m in models:
            mo = m + 0xe0
            fields = u64s(game, mo, 24)
            res = fields[0]
            for k, v in enumerate(fields[1:12], 1):   # (a SkeletonObj among the ModelObj's pointers)
                if not (0x7000000000 <= v < 0x7400000000):
                    continue
                sk = u64s(game, v, 32)
                if any(w0 - 0x30 * 130 <= s <= w0 for s in sk):
                    print('\nLink: model {:#x}, ModelObj +0xe0, ResModel {:#x}, SkeletonObj at ModelObj+{:#x} = {:#x}'.format(
                        m, res, 8 * k, v))
                    for i, s in enumerate(sk):
                        print('   skel+{:#05x} {:#018x} {}'.format(8 * i, s, text(game, s) if 0x7000000000 <= s < 0x7400000000 else ''))
                    print('  ModelObj:')
                    for i, s in enumerate(fields):
                        print('   mobj+{:#05x} {:#018x}'.format(8 * i, s))
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
