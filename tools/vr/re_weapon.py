#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) The held weapon's actor matrix and who writes it: boots into play in first
person, takes the melee weapon out with the right shoulder grip (as test_vr_game.py's `weapon`), finds the 3x4
matrices at the weapon model's root (`mem find mtx:`), then traps writes to each in turn (`mem watch`,
SAVAGEWIND_WATCH_WRITES) and names the writers (swgame.dll offsets -> f_<guest addr>).

  python tools/vr/re_weapon.py --save <scratch save>
"""
import argparse
import bisect
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

G = '/user/hand/right/input/squeeze/value'


def script():
    lines = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.25 1.3 -0.4 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.1f hand right 0.25 1.45 -0.45 0 0 0' % t, '%.1f value %s 0' % (t, G),
                  '%.1f hand right 0.25 1.45 -0.45 0 0 0' % (t + 4), '%.1f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.1f value %s 1' % (t + 5, G), '%.1f value %s 0' % (t + 5.2, G),
                  '%.1f hand right 0.15 1.65 0.12 0 0 0' % (t + 6), '%.1f hand right 0.25 1.45 -0.45 0 0 0' % (t + 6.5)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def floats(game, a, n):
    d = game.cmd('mem', 'read', '{:#x}'.format(a), str(4 * n), 'f32', ok=False)
    return [float(d.get('value.%d' % i, 'nan')) for i in range(n)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re_weapon')
    ap.add_argument('--radius', type=float, default=0.02)
    ap.add_argument('--watch', type=int, default=6, help='how many of the matrices found to watch')
    a = ap.parse_args()
    env = {'SAVAGEWIND_WATCH_WRITES': '60', 'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_LOG_MODELS': 'Weapon_',
           'FAKE_XR_CAPTURE_EVERY': '0'}
    game, proc, log = re_run.boot(a.save, a.out, env, script())
    logpath = os.path.join(a.out, 'game.log')
    marks = []
    try:
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        base = (now() // 40 + 1) * 40
        while now() < base + 8:
            time.sleep(0.1)
        text = open(logpath, errors='replace').read()
        held = re.findall(r'right hand holds (Weapon_\S+) \(1\)', text)
        print('right hand holds:', held[-1:] or 'nothing')
        if not held:
            return
        lines = re.findall(r'\[vr-model\] \d+ %s (\S+) (\S+) (\S+) (\S+)' % re.escape(held[-1]), text)
        model, x, y, z = lines[-1][0], float(lines[-1][1]), float(lines[-1][2]), float(lines[-1][3])
        print('its model {} at {:.3f} {:.3f} {:.3f}'.format(model, x, y, z))
        d = game.cmd('mem', 'find', 'mtx:{},{},{},{}'.format(x, y, z, a.radius), '100', ok=False, timeout=300)
        hits = [int(d['hit.%d' % i], 16) for i in range(int(d.get('found', 0)))]
        print('{} matrices there: {}'.format(len(hits), ' '.join('{:#x}'.format(h) for h in hits)))
        for h in hits[:a.watch]:
            m = floats(game, h, 12)
            print('  {:#x}: '.format(h) + ' '.join('{:.3f}'.format(v) for v in m))
        for h in hits[:a.watch]:
            start = os.path.getsize(logpath)
            game.cmd('mem', 'watch', '{:#x}'.format(h), '48', ok=False)
            time.sleep(1.5)
            marks.append((h, start, os.path.getsize(logpath)))
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(logpath, 'rb').read().decode('utf-8', 'replace')
    nm = subprocess.run(['nm', os.path.join(T.TREE, 'build', 'swgame.dll')], capture_output=True, text=True).stdout
    syms = sorted((int(l.split()[0], 16), l.split()[2]) for l in nm.splitlines()
                  if len(l.split()) == 3 and l.split()[2].startswith('f_'))
    keys = [s[0] for s in syms]
    for h, s0, s1 in marks:
        part = text[s0:s1]
        offs = collections.Counter(re.findall(r'\[watch\] [\d.]+ write head\+0x[0-9a-f]+ by swgame\.dll\+(0x[0-9a-f]+)', part))
        print('writers of {:#x}:'.format(h))
        for off, n in offs.most_common(6):
            va = 0x180000000 + int(off, 16)
            k = bisect.bisect_right(keys, va) - 1
            print('  swgame.dll+{} ({}x): {}+{:#x}'.format(off, n, syms[k][1] if k >= 0 else '?', va - keys[k] if k >= 0 else 0))


if __name__ == '__main__':
    main()
