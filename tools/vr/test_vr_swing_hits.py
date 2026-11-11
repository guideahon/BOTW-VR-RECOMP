#!/usr/bin/env python3
"""The held weapon hits whenever it is swung (docs/VR.md), against fake_xr: first person, the right shoulder grip takes
the melee weapon out, then the right controller makes a fast cut (120 deg in 0.2 s) and goes back slowly, with
SAVAGEWIND_VR_SWING_ATTACK=0 (the swing does not press the game's attack, so only the swing can turn the sensor on).
Passes when the weapon's attack sensor (read from its actor: swing_sensor) is on during the cut and off before and
after it, and with SAVAGEWIND_VR_SWING_HITS=0 (a second run) it never turns on.

  python tools/vr/test_vr_swing_hits.py --save <scratch save> [--out build/vr/swing_hits]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

G = '/user/hand/right/input/squeeze/value'
GUARD = '0.25 1.2 -0.35'


def script():
    lines = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.25 1.0 -0.1 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.2f hand right 0.25 1.45 -0.45 0 0 0' % t, '%.2f value %s 0' % (t, G),
                  '%.2f hand right 0.25 1.45 -0.45 0 0 0' % (t + 4), '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.2f value %s 1' % (t + 5, G), '%.2f value %s 0' % (t + 5.2, G),          # the weapon out
                  '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 6), '%.2f hand right %s 0 0 0' % (t + 7, GUARD),
                  '%.2f hand right %s 0 0 0' % (t + 10, GUARD),                                 # a fast cut
                  '%.2f hand right %s 0 -120 0' % (t + 10.2, GUARD),
                  '%.2f hand right %s 0 -120 0' % (t + 12, GUARD),                              # back slowly
                  '%.2f hand right %s 0 0 0' % (t + 14, GUARD)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def run(save, out, hits_on):
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'FAKE_XR_CAPTURE_EVERY': '0', 'SAVAGEWIND_VR_SWING_ATTACK': '0',
           'SAVAGEWIND_VR_SWING_HITS': '1' if hits_on else '0'}
    game, proc, log = re_run.boot(save, out, env, script())
    try:
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        base = (now() // 40 + 1) * 40
        samples = {'before': [], 'cut': [], 'after': []}
        while now() < base + 15:
            t = now() - base
            st = game.cmd('vr', ok=False)
            phase = 'before' if 8 <= t < 9.9 else 'cut' if 10.05 <= t < 10.6 else 'after' if 12.5 <= t < 15 else None
            if phase:
                samples[phase].append(st.get('swing_sensor', -1))
            time.sleep(0.02)
        st = game.cmd('vr', ok=False)
        return {'swing_hits': st.get('swing_hits', 0), 'attacks': st.get('motion_attacks', st.get('attacks')),
                'before': samples['before'], 'cut': samples['cut'], 'after': samples['after']}
    finally:
        perf_routes.stop_game(game, proc, log)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/swing_hits')
    a = ap.parse_args()
    on = run(a.save, a.out + '_on', True)
    print('hits on : swing_hits {} | sensor before {} | during the cut {} | after {}'.format(
        on['swing_hits'], sorted(set(on['before'])), on['cut'], sorted(set(on['after']))))
    off = run(a.save, a.out + '_off', False)
    print('hits off: swing_hits {} | sensor before {} | during the cut {} | after {}'.format(
        off['swing_hits'], sorted(set(off['before'])), off['cut'], sorted(set(off['after']))))
    ok = (on['swing_hits'] >= 1 and 1 in on['cut'] and on['before'] and 1 not in on['before'] and
          on['after'] and 1 not in on['after'] and -1 not in on['before'] and
          off['swing_hits'] == 0 and 1 not in off['cut'] + off['before'] + off['after'])
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
