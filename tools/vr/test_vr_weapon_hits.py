#!/usr/bin/env python3
"""The held weapon's hits where the hand is (docs/VR.md), against fake_xr: first person, the right shoulder grip takes
the melee weapon out, then the right controller is held far out to the right of where the animation holds it. Passes
when the runtime moved the weapon's actor (weapons_moved), the weapon is still known to be in the right hand, and the
3x4 matrices at the weapon (its actor's and its physics bodies', where the attack sensor is) are at the controller:
with SAVAGEWIND_VR_WEAPON_HITS=0 (a second run) they are not.

  python tools/vr/test_vr_weapon_hits.py --save <scratch save> [--out build/vr/weapon_hits]
"""
import argparse
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402

G = '/user/hand/right/input/squeeze/value'
OUT = '0.60 1.35 -0.25'   # the right hand far out to the right


def script():
    lines = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.25 1.3 -0.4 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.1f hand right 0.25 1.45 -0.45 0 0 0' % t, '%.1f value %s 0' % (t, G),
                  '%.1f hand right 0.25 1.45 -0.45 0 0 0' % (t + 4), '%.1f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.1f value %s 1' % (t + 5, G), '%.1f value %s 0' % (t + 5.2, G),
                  '%.1f hand right 0.15 1.65 0.12 0 0 0' % (t + 6), '%.1f hand right 0.25 1.45 -0.45 0 0 0' % (t + 6.5),
                  '%.1f hand right %s 0 0 0' % (t + 7, OUT), '%.1f hand right %s 0 0 0' % (t + 11, OUT)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def run(save, out, hits_on):
    env = {'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'SAVAGEWIND_VR_LOG_MODELS': 'Weapon_', 'FAKE_XR_CAPTURE_EVERY': '0',
           'SAVAGEWIND_VR_WEAPON_HITS': '1' if hits_on else '0'}
    game, proc, log = re_run.boot(save, out, env, script())
    logpath = os.path.join(out, 'game.log')
    try:
        now = lambda: game.cmd('vr', ok=False).get('frames', 0) / 90.0  # noqa: E731
        base = (now() // 40 + 1) * 40
        while now() < base + 9.5:
            time.sleep(0.1)
        st = game.cmd('vr', ok=False)
        pos = game.cmd('pos', ok=False)
        text = open(logpath, errors='replace').read()
        held = re.findall(r'right hand holds (Weapon_\S+) \(1\)', text)
        if not held:
            return None
        ms = re.findall(r'\[vr-model\] \d+ %s \S+ (\S+) (\S+) (\S+)' % re.escape(held[-1]), text)
        x, y, z = map(float, ms[-1])
        d = game.cmd('mem', 'find', 'mtx:{},{},{},0.08'.format(x, y, z), '100', ok=False, timeout=300)
        n = int(d.get('found', 0))
        # how far the weapon is from Link, sideways: the controller is held 0.6 m to his right
        side = math.hypot(x - pos.get('x', 0), z - pos.get('z', 0))
        return {'held': held[-1], 'moved': st.get('weapons_moved', 0), 'matrices': n, 'from_link': side,
                'right_holds': st.get('held_right', None)}
    finally:
        perf_routes.stop_game(game, proc, log)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/weapon_hits')
    a = ap.parse_args()
    on = run(a.save, a.out + '_on', True)
    print('hits on :', on)
    off = run(a.save, a.out + '_off', False)
    print('hits off:', off)
    ok = (on and off and on['moved'] > 0 and off['moved'] == 0 and on['matrices'] >= off['matrices'] + 1)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
