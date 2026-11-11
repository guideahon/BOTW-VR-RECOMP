#!/usr/bin/env python3
"""Both hands on a large sword (docs/VR.md), against fake_xr: first person, the right shoulder grip takes the large
sword out (the scratch save's Weapon_Lsword_031), the right hand is held ahead, the left one goes along the sword's line
(read from the runtime's trace) and its grip is pressed, then the left hand moves while gripping. Passes when the grip
took hold (two_hands), the weapon was turned by it, the weapon model's blade (what the game drew) points at the left
hand within a few degrees after it moved, and a grip far from the sword does not take hold.

  python tools/vr/test_vr_two_hands.py --save <scratch save> [--out build/vr/two_hands]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

GR = '/user/hand/right/input/squeeze/value'
GL = '/user/hand/left/input/squeeze/value'
RIGHT = (0.20, 1.30, -0.35)


def script(left_on):
    """left_on: the left hand's place on the sword's line (tracking space), or None for the first, exploring run"""
    lines = ['0 head 0 1.7 0 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.2f hand right 0.25 1.45 -0.45 0 0 0' % t, '%.2f value %s 0' % (t, GR), '%.2f value %s 0' % (t, GL),
                  '%.2f hand left -0.25 1.0 -0.35 0 0 0' % t,
                  '%.2f hand right 0.25 1.45 -0.45 0 0 0' % (t + 4), '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.2f value %s 1' % (t + 5, GR), '%.2f value %s 0' % (t + 5.2, GR),          # the sword out
                  '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 6),
                  '%.2f hand right %.2f %.2f %.2f 0 0 0' % ((t + 7,) + RIGHT)]
        if left_on:
            far = (left_on[0] - 0.35, left_on[1] - 0.25, left_on[2] + 0.2)
            moved = (left_on[0] + 0.15, left_on[1] + 0.15, left_on[2])
            lines += ['%.2f hand left -0.25 1.0 -0.35 0 0 0' % (t + 8),
                      '%.2f hand left %.3f %.3f %.3f 0 0 0' % ((t + 8.5,) + far),               # a grip far from it
                      '%.2f value %s 1' % (t + 9, GL), '%.2f value %s 0' % (t + 9.5, GL),
                      '%.2f hand left %.3f %.3f %.3f 0 0 0' % ((t + 10,) + left_on),            # on its line: grip
                      '%.2f value %s 1' % (t + 11, GL),
                      '%.2f hand left %.3f %.3f %.3f 0 0 0' % ((t + 12,) + left_on),
                      '%.2f hand left %.3f %.3f %.3f 0 0 0' % ((t + 13,) + moved),              # moved, gripping
                      '%.2f hand left %.3f %.3f %.3f 0 0 0' % ((t + 15,) + moved),
                      '%.2f value %s 0' % (t + 15.5, GL)]
        else:
            lines += ['%.2f hand left -0.10 1.30 -0.55 0 0 0' % (t + 8)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def run(save, out, left_on, until):
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_TRACE_TWO_HANDS': '1', 'FAKE_XR_CAPTURE_EVERY': '0'}
    game, proc, log = re_run.boot(save, out, env, script(left_on))
    marks = {}
    try:
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        base = (now() // 40 + 1) * 40
        for name, at in until:
            while now() < base + at:
                time.sleep(0.05)
            st = game.cmd('vr', ok=False)
            marks[name] = (os.path.getsize(os.path.join(out, 'game.log')), st.get('two_hands', 0), st.get('two_hand_frames', 0))
    finally:
        perf_routes.stop_game(game, proc, log)
    return open(os.path.join(out, 'game.log'), 'rb').read().decode('utf-8', 'replace'), marks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/two_hands')
    a = ap.parse_args()
    # 1. where the sword's line is: the trace's blade direction (game space) with the left hand at a known place
    text, _ = run(a.save, a.out + '_look', None, [('look', 10)])
    held = re.findall(r'right hand holds (Weapon_\S+) \(1\)', text)
    lines = re.findall(r'blade (\S+) (\S+) (\S+) from (\S+) (\S+) (\S+) \(tracking\)', text)
    if not held or not lines:
        print('no large sword in hand or no trace:', held[-1:], len(lines))
        print('FAIL')
        sys.exit(1)
    bx, by, bz, ox, oy, oz = map(float, lines[-1])
    print('in hand: {} | its line (tracking): from {:.2f} {:.2f} {:.2f} along {:.2f} {:.2f} {:.2f}'.format(
        held[-1], ox, oy, oz, bx, by, bz))
    # 2. the left hand 0.4 m along the line, on the side ahead of the player (the one it can reach)
    cands = [(ox + 0.4 * sg * bx, oy + 0.4 * sg * by, oz + 0.4 * sg * bz) for sg in (1, -1)]
    on = min(cands, key=lambda c: c[2])
    text, marks = run(a.save, a.out, on, [('far', 9.8), ('took', 12.5), ('moved', 15.2)])
    far, took, moved = marks['far'], marks['took'], marks['moved']
    ang = [float(x) for x in re.findall(r'model blade (\S+) deg from the other hand', text[took[0]:moved[0]])]
    print('left at {:.2f} {:.2f} {:.2f} | a grip far from it held: {} | took hold: {} | turned on {} weapon updates | '
          'the model blade off the line to the left hand after it moved (deg): {}'.format(
              *on, far[1], took[1], moved[2], ang[-3:]))
    ok = far[1] == 0 and took[1] == 1 and moved[2] > 0 and ang and max(ang[-3:]) < 8
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
