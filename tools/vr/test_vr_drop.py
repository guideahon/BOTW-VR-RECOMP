#!/usr/bin/env python3
"""The drop gesture (docs/VR.md), against fake_xr: first person, the right shoulder grip takes the melee weapon out,
then the right grip is held at the right waist. Passes when the quick selector's native drop (runtime/quick_drop*.c,
driven by the gesture) reports the equipped weapon dropped and completed, and the weapon's model then lies away from
Link's hand, lower than it was.

  python tools/vr/test_vr_drop.py --save <scratch save> [--out build/vr/drop]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

G = '/user/hand/right/input/squeeze/value'
WAIST = '0.25 1.0 0.05'


def script():
    lines = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.25 1.0 -0.35 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.2f hand right 0.25 1.45 -0.45 0 0 0' % t, '%.2f value %s 0' % (t, G),
                  '%.2f hand right 0.25 1.45 -0.45 0 0 0' % (t + 4), '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.2f value %s 1' % (t + 5, G), '%.2f value %s 0' % (t + 5.2, G),          # the weapon out
                  '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 6), '%.2f hand right 0.25 1.3 -0.4 0 0 0' % (t + 7),
                  '%.2f hand right 0.25 1.3 -0.4 0 0 0' % (t + 9), '%.2f hand right %s 0 0 0' % (t + 9.5, WAIST),
                  '%.2f value %s 0' % (t + 10, G), '%.2f value %s 1' % (t + 10.1, G),          # held at the waist
                  '%.2f value %s 0' % (t + 12, G), '%.2f hand right %s 0 0 0' % (t + 12.5, WAIST),
                  '%.2f hand right 0.25 1.3 -0.4 0 0 0' % (t + 13)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/drop')
    a = ap.parse_args()
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_LOG_MODELS': 'Weapon_', 'FAKE_XR_CAPTURE_EVERY': '0',
           'SAVAGEWIND_VR_TRACE_DROP': '1'}
    game, proc, log = re_run.boot(a.save, a.out, env, script())
    logpath = os.path.join(a.out, 'game.log')
    try:
        # (each round of the script drops the weapon it took out: the first one with a weapon in hand counts)
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        pos = game.cmd('pos', ok=False)
        base = (now() // 40 + 1) * 40
        while now() < base + 16:
            time.sleep(0.1)
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(logpath, 'rb').read().decode('utf-8', 'replace')
    first = re.search(r'\[quick-drop\] equipped native drop pending', text)
    mark = first.start() if first else len(text)
    before, after = text[:mark], text[mark:]
    held = re.findall(r'right hand holds (Weapon_\S+) \(1\)', before)
    name = held[-1] if held else None
    pending = re.findall(r'\[quick-drop\] equipped native drop pending name=(\S+)', after)
    done = re.findall(r'\[quick-drop\] equipped native drop completed name=(\S+)', after)
    rejected = re.findall(r'\[quick-drop\].*', after)
    print('held:', name, '| drop pending:', pending, '| completed:', done)
    if not done:
        print('quick-drop log:', rejected[:6])
    ok = bool(name and name in pending and name in done)
    if name:
        b = re.findall(r'\[vr-model\] \d+ %s \S+ (\S+) (\S+) (\S+)' % re.escape(name), before)
        f = re.findall(r'\[vr-model\] \d+ %s \S+ (\S+) (\S+) (\S+)' % re.escape(name), after)
        if b and f:
            yb, yf = float(b[-1][1]), float(f[-1][1])
            print('the weapon: y {:.2f} in hand -> {:.2f} at the end (Link at y {:.2f})'.format(yb, yf, pos.get('y', 0)))
            ok = ok and yf < yb - 0.3
        else:
            print('the weapon model was not seen after the drop (a world item may draw another model)')
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
