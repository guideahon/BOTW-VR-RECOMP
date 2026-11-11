#!/usr/bin/env python3
"""(reverse engineering, docs/vr/re-notes.md) The held weapon's attack sensor state: boots into play in first person,
takes the melee weapon out (as test_vr_game.py's `weapon`), finds its actor (the one main+0x1525f3c updates whose
matrix, actor + 0x398, is at the weapon model), then reads the actor's memory idle and during a game attack (`press y`)
and lists the 32-bit words that hold one value whenever idle and another all through the attack (the sensor's mode
among them). With --watch <offset> it then traps the writes to that word through another attack.

  python tools/vr/re_attack.py --save <scratch save> [--watch 0x8b0]
"""
import argparse
import collections
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import re_weapon  # noqa: E402

SPAN = 0x1400


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re_attack')
    ap.add_argument('--watch', default=None)
    a = ap.parse_args()
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_LOG_MODELS': 'Weapon_', 'FAKE_XR_CAPTURE_EVERY': '0',
           'SAVAGEWIND_FNPROF': '0:9999999', 'SAVAGEWIND_FNPROF_ARGS': '7001525f3c', 'SAVAGEWIND_FNPROF_ARGS_MAX': '400000'}
    if a.watch:
        env['SAVAGEWIND_WATCH_WRITES'] = '60'
    game, proc, log = re_run.boot(a.save, a.out, env, re_weapon.script())
    logpath = os.path.join(a.out, 'game.log')

    def mem(addr, n):
        out = b''
        for off in range(0, n, 4096):
            k = min(4096, n - off)
            d = game.cmd('mem', 'read', '{:#x}'.format(addr + off), str(k), 'u64', ok=False)
            out += b''.join(struct.pack('<Q', int(d.get('value.%d' % i, '0'), 0)) for i in range(k // 8))
        return out

    start = None
    try:
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        base = (now() // 40 + 1) * 40
        while now() < base + 8:
            time.sleep(0.1)
        text = open(logpath, errors='replace').read()
        name = re.findall(r'right hand holds (Weapon_\S+) \(1\)', text)[-1]
        l = re.findall(r'\[vr-model\] \d+ %s (\S+) (\S+) (\S+) (\S+)' % re.escape(name), text)[-1]
        x, y, z = float(l[1]), float(l[2]), float(l[3])
        best = []
        for a_ in set(re.findall(r'\[fnprof-args\] 7001525f3c from \S+: (\S+)', text)):
            act = int(a_, 16)
            m = struct.unpack('<12f', mem(act + 0x398, 48))
            best.append((((m[3] - x) ** 2 + (m[7] - y) ** 2 + (m[11] - z) ** 2) ** 0.5, act))
        best.sort()
        actor = best[0][1]
        print('{} actor {:#x} ({:.3f} m from its model)'.format(name, actor, best[0][0]))
        idle = [mem(actor, SPAN) for _ in range(3)]
        game.cmd('press', 'y', '6', ok=False)
        attack = []
        t0 = time.time()
        while time.time() - t0 < 1.6:
            attack.append(mem(actor, SPAN))
        print(len(attack), 'snapshots in {:.2f} s'.format(time.time() - t0))
        time.sleep(2.5)
        idle += [mem(actor, SPAN) for _ in range(2)]
        steady = sum(1 for off in range(0, SPAN, 4) if len(set(struct.unpack_from('<I', x, off)[0] for x in idle)) == 1)
        print('{} words steady idle of {}; nonzero bytes {}; attack snapshots differing from idle[0]: {}'.format(
            steady, SPAN // 4, sum(1 for c in idle[0] if c), sum(1 for x in attack if x != idle[0])))
        found = []
        for off in range(0, SPAN, 4):
            iv = set(struct.unpack_from('<I', s, off)[0] for s in idle)
            av = set(struct.unpack_from('<I', s, off)[0] for s in attack)
            if len(iv) == 1 and av - iv:
                found.append((off, iv.pop(), [struct.unpack_from('<I', s, off)[0] for s in attack]))
        print('words steady idle that change in the attack:')
        for off, i, vs in found:
            print('  +{:#05x}: {:#x} -> {}'.format(off, i, ' '.join('{:#x}'.format(v) for v in vs)))
        on = [x for x in attack if struct.unpack_from('<i', x, 0xc30)[0] == 2]
        if on:
            print('around +0xc30 (idle | attack):')
            for off in range(0xc20, 0xd40, 4):
                i, v = struct.unpack_from('<I', idle[0], off)[0], struct.unpack_from('<I', on[0], off)[0]
                fi, fv = struct.unpack_from('<f', idle[0], off)[0], struct.unpack_from('<f', on[0], off)[0]
                print('  +{:#05x} {:#010x} {:#010x}  {:g} {:g}{}'.format(off, i, v, fi, fv, '  *' if i != v else ''))
        if a.watch:
            start = os.path.getsize(logpath)
            game.cmd('mem', 'watch', '{:#x}'.format(actor + int(a.watch, 16)), '4', ok=False)
            for _ in range(3):
                time.sleep(0.8)
                game.cmd('press', 'y', '6', ok=False)
                time.sleep(1.2)
                print('mode now', game.cmd('mem', 'read', '{:#x}'.format(actor + int(a.watch, 16)), '4', 'u32', ok=False).get('value.0'))
    finally:
        perf_routes.stop_game(game, proc, log)
    if start is not None:
        text = open(logpath, 'rb').read()[start:].decode('utf-8', 'replace')
        for k, v in collections.Counter(re.findall(r'guest lr (\S+) chain ((?:\S+ ){5})', text)).most_common(8):
            print(v, k)


if __name__ == '__main__':
    main()
