#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) Link's bone matrices and who writes them: boots into play, finds the
dense run of 3x4 matrices around Link (`mem find mtx:`), prints its layout, then traps writes to its first matrix
(`mem watch`, SAVAGEWIND_WATCH_WRITES) and prints the writers from the log (swgame.dll offsets -> f_<guest addr>).

  python tools/vr/re_bonewatch.py --save <scratch save>
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402


def hits(d):
    return [int(d['hit.%d' % i], 16) for i in range(int(d.get('found', 0)))]


def runs(hs):
    """[(start, count, stride)] of evenly spaced hits"""
    out, i = [], 0
    while i < len(hs):
        j = i + 1
        stride = hs[j] - hs[i] if j < len(hs) else 0
        while j < len(hs) and hs[j] - hs[j - 1] == stride:
            j += 1
        out.append((hs[i], j - i, stride))
        i = j
    return out


def floats(game, a, n):
    d = game.cmd('mem', 'read', '{:#x}'.format(a), str(4 * n), 'f32', ok=False)
    return [float(d.get('value.%d' % i, 'nan')) for i in range(n)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    ap.add_argument('--radius', type=float, default=1.6)
    ap.add_argument('--target', default=None, help='watch this address instead of the first run found')
    a = ap.parse_args()
    game, proc, log = re_run.boot(a.save, a.out, {'SAVAGEWIND_WATCH_WRITES': '40'})
    try:
        p = game.cmd('pos', ok=False)
        x, y, z = p['x'], p['y'], p['z']
        print('Link at {:.2f} {:.2f} {:.2f}'.format(x, y, z))
        d = game.cmd('mem', 'find', 'mtx:{},{},{},{}'.format(x, y + 1, z, a.radius), '500', ok=False, timeout=300)
        hs = hits(d)
        rr = sorted(runs(hs), key=lambda r: -r[1])
        print('{} matrices; longest evenly spaced runs:'.format(len(hs)))
        for start, cnt, stride in rr[:8]:
            print('  {:#x}  x{}  stride {:#x}'.format(start, cnt, stride))
        target = int(a.target, 16) if a.target else rr[0][0]
        m = floats(game, target - 0x40, 64)
        print('around {:#x} (from -0x40):'.format(target))
        for r in range(0, 64, 4):
            print('  {:+#06x} '.format(4 * r - 0x40) + ' '.join('{:9.3f}'.format(v) for v in m[r:r + 4]))
        print(game.cmd('mem', 'watch', '{:#x}'.format(target), '48', ok=False))
        time.sleep(4)
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(os.path.join(a.out, 'game.log'), errors='replace').read()
    offs = collections.Counter(re.findall(r'\[watch\] [\d.]+ write head\+0x[0-9a-f]+ by swgame\.dll\+(0x[0-9a-f]+)', text))
    others = collections.Counter(re.findall(r'\[watch\] [\d.]+ write head\+0x[0-9a-f]+ by (\S+)', text))
    print('writers:', dict(others.most_common(8)))
    if offs:
        nm = subprocess.run(['nm', os.path.join(T.TREE, 'build', 'swgame.dll')], capture_output=True, text=True).stdout
        syms = sorted((int(l.split()[0], 16), l.split()[2]) for l in nm.splitlines()
                      if len(l.split()) == 3 and l.split()[2].startswith('f_'))
        import bisect
        keys = [s[0] for s in syms]
        for off, n in offs.most_common(10):
            va = 0x180000000 + int(off, 16)
            k = bisect.bisect_right(keys, va) - 1
            print('  swgame.dll+{} ({}x): {}+{:#x}'.format(off, n, syms[k][1] if k >= 0 else '?', va - keys[k] if k >= 0 else 0))
    chain = re.findall(r'\[watch\].*\n((?:.*lr.*\n)+)', text)
    if chain:
        print('first chain:\n' + chain[0][:1500])


if __name__ == '__main__':
    main()
