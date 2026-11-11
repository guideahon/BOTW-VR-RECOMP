#!/usr/bin/env python3
"""The drop gesture (docs/VR.md), against fake_xr: a shoulder grip equips a weapon or bow, then that hand's grip is
held at the right waist. Passes when the quick selector's native drop (runtime/quick_drop*.c) reports the equipped
item dropped and completed, and its model then lies away from Link's hand, lower than it was.

  python tools/vr/test_vr_drop.py --save <scratch save> [--item melee|bow] [--out build/vr/drop]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

WAIST = '0.25 1.0 0.05'


def script(item):
    hand = 'left' if item == 'bow' else 'right'
    grip = '/user/hand/%s/input/squeeze/value' % hand
    shoulder = '-0.15 1.65 0.12' if item == 'bow' else '0.15 1.65 0.12'
    rest = '-0.25 1.3 -0.4' if item == 'bow' else '0.25 1.3 -0.4'
    lines = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.25 1.0 -0.35 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.2f hand %s %s 0 0 0' % (t, hand, rest), '%.2f value %s 0' % (t, grip),
                  '%.2f hand %s %s 0 0 0' % (t + 4, hand, rest),
                  '%.2f hand %s %s 0 0 0' % (t + 4.5, hand, shoulder),
                  '%.2f value %s 1' % (t + 5, grip), '%.2f value %s 0' % (t + 5.2, grip),
                  '%.2f hand %s %s 0 0 0' % (t + 6, hand, shoulder),
                  '%.2f hand %s %s 0 0 0' % (t + 7, hand, rest),
                  '%.2f hand %s %s 0 0 0' % (t + 9, hand, rest),
                  '%.2f hand %s %s 0 0 0' % (t + 9.5, hand, WAIST),
                  '%.2f value %s 0' % (t + 10, grip), '%.2f value %s 1' % (t + 10.1, grip),
                  '%.2f value %s 0' % (t + 12, grip),
                  '%.2f hand %s %s 0 0 0' % (t + 12.5, hand, WAIST),
                  '%.2f hand %s %s 0 0 0' % (t + 13, hand, rest)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--item', choices=('melee', 'bow'), default='melee')
    ap.add_argument('--out', default='build/vr/drop')
    a = ap.parse_args()
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_LOG_MODELS': 'Weapon_', 'FAKE_XR_CAPTURE_EVERY': '0',
           'SAVAGEWIND_VR_TRACE_DROP': '1'}
    logpath = os.path.join(a.out, 'game.log')
    game, proc, log = re_run.boot(a.save, a.out, env, script(a.item))
    try:
        # The first scripted cycle can finish during boot. Stop after its native drop instead of waiting for another
        # cycle, which could act on different equipment left in the copied save.
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            text = open(logpath, 'rb').read().decode('utf-8', 'replace')
            if re.search(r'\[quick-drop\] equipped native drop completed name=Weapon_', text):
                time.sleep(1)  # let the dropped actor settle on the ground
                break
            if proc.poll() is not None:
                break
            time.sleep(0.2)
        pos = game.cmd('pos', ok=False) if proc.poll() is None else {}
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(logpath, 'rb').read().decode('utf-8', 'replace')
    first = re.search(r'\[quick-drop\] equipped native drop pending', text)
    mark = first.start() if first else len(text)
    before, after = text[:mark], text[mark:]
    held = re.findall(r'%s hand holds (Weapon_\S+) \(%d\)' % ('left' if a.item == 'bow' else 'right',
                                                                2 if a.item == 'bow' else 1), before)
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
