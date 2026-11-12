#!/usr/bin/env python3
"""The BetterVR rune trigger path, in the game against fake_xr.

The left grip selects the rune from the left waist. The right trigger then has
to reach the game as R (rune activation), not ZR (bow). The test captures the
view before and after activation for a visual check.

  python tools/vr/test_vr_runes.py --save <scratch save> [--out build/vr/runes]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

SQUEEZE_L = '/user/hand/left/input/squeeze/value'
TRIGGER_R = '/user/hand/right/input/trigger/value'


def script():
    lines = ['0 head 0 1.7 0 0 0 0',
             '0 hand left -0.25 1.2 -0.3 0 0 0',
             '0 hand right 0.25 1.3 -0.3 0 0 0']
    # The game takes a while to reach the plateau. Repeat the input sequence so
    # each test run has a future window after it finishes loading.
    for k in range(1, 40):
        t = 40.0 * k
        lines += [
            '%.2f hand left -0.15 1.2 0.12 0 0 0' % (t + 3.0),  # left waist
            '%.2f value %s 1' % (t + 3.5, SQUEEZE_L),
            '%.2f value %s 0' % (t + 3.75, SQUEEZE_L),          # select rune
            '%.2f value %s 1' % (t + 6.0, TRIGGER_R),           # activate rune with A
            '%.2f value %s 0' % (t + 6.9, TRIGGER_R),
            '%.2f hand left -0.25 1.2 -0.3 0 0 0' % (t + 7.2),
        ]
    return sorted(lines, key=lambda line: float(line.split()[0]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/runes')
    a = ap.parse_args()
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_MOTION': '0'}
    game, proc, log = re_run.boot(a.save, a.out, env, script())
    try:
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        base = (int(now() // 40) + 1) * 40
        out_abs = os.path.abspath(a.out)
        capture_dir = os.path.relpath(out_abs, re_run.T.TREE)
        if capture_dir == '..' or capture_dir.startswith('..' + os.sep):
            raise ValueError('--out must be inside the repository so the game can write screenshots safely')
        os.makedirs(out_abs, exist_ok=True)
        before_rel = os.path.join(capture_dir, 'rune-before.png').replace(os.sep, '/')
        after_rel = os.path.join(capture_dir, 'rune-after.png').replace(os.sep, '/')
        before = os.path.join(re_run.T.TREE, before_rel)
        after = os.path.join(re_run.T.TREE, after_rel)
        while now() < base + 5.0:
            time.sleep(0.1)
        before_result = game.cmd('shot', before_rel, ok=False)
        while now() < base + 7.5:
            time.sleep(0.1)
        after_result = game.cmd('shot', after_rel, ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)

    text = open(os.path.join(a.out, 'game.log'), encoding='utf-8', errors='replace').read()
    rune_selected = '[vr] gesture: in hand 0 -> 3' in text
    lines = text.splitlines()
    rune_triggers = [i for i, line in enumerate(lines) if '[vr] right trigger: in hand 3' in line]
    trigger_buttons = []
    for i in rune_triggers:
        for line in lines[i + 1:i + 5]:
            match = re.search(r'\[input\] id 0 buttons (0x[0-9a-fA-F]+)', line)
            if match:
                trigger_buttons.append(int(match.group(1), 16))
    trigger_used_rune = bool(rune_triggers)
    activated_as_a = any(v & (1 << 0) for v in trigger_buttons)
    activated_as_r = any(v & (1 << 7) for v in trigger_buttons)
    activated_as_zr = any(v & (1 << 9) for v in trigger_buttons)
    captures = (os.path.isfile(before) and os.path.isfile(after) and
                int(before_result.get('bytes', 0)) > 0 and int(after_result.get('bytes', 0)) > 0)
    print('rune selected by left-waist gesture:', rune_selected)
    print('right trigger saw rune in hand:', trigger_used_rune)
    print('game input included A (0x1):', activated_as_a)
    print('game input included R (0x80):', activated_as_r)
    print('game input included ZR (0x200):', activated_as_zr)
    print('before/after captures:', before, after, captures)
    ok = (rune_selected and trigger_used_rune and activated_as_a and not activated_as_r and
          not activated_as_zr and captures)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
