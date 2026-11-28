#!/usr/bin/env python3
"""Attacking in first person shows two arms, not four (2026-11-27: "when I attack the attack animation plays and my
hand is there at the same time, as if Link had four arms; the attack animation must not play"), against fake_xr: the
right shoulder grip takes the melee weapon out, then four fast cuts. Passes when motion combat reads them as attacks,
none presses the game's attack (Options > VR > Swings play Link's attack: off by default since then, as BetterVR - its
animation moved Link's body and shoulders under the hands that follow the controllers), the weapon still hits (its
sensor turned on by the swing: swing_hits), and his armor's wrists (sleeves, gloves) stay on his arms' (the `vr`
command's armor_wrist_off: not 3 cm off). --swing-attack 1 runs it with the option on (the attack pressed: a negative
control, it must fail).

  python tools/vr/test_vr_attack_arms.py --save <scratch save> [--exe build/native-vr/savagewind] [--out build/vr/attack_arms]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

G = '/user/hand/right/input/squeeze/value'
TR = '/user/hand/right/input/trigger/value'
GUARD_R, REST_L = '0.25 1.45 -0.45', '-0.25 1.0 -0.1'
CUT_END = '-0.2 0.95 -0.5'


def script():
    """the weapon out with the right shoulder grip, then four fast cuts (120 deg and 0.5 m in 0.2 s, as
    test_vr_swing_hits.py's): motion combat reads each as a slash and presses the game's attack"""
    lines = ['0 head 0 1.7 0 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        lines += ['%.2f hand left %s 0 0 0' % (t, REST_L), '%.2f hand right %s 0 0 0' % (t, GUARD_R),
                  '%.2f value %s 0' % (t, G), '%.2f value %s 0' % (t, TR),
                  '%.2f hand right %s 0 0 0' % (t + 4, GUARD_R), '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.2f value %s 1' % (t + 5, G), '%.2f value %s 0' % (t + 5.2, G),          # the weapon out
                  '%.2f hand right 0.15 1.65 0.12 0 0 0' % (t + 6), '%.2f hand right %s 0 0 0' % (t + 7, GUARD_R)]
        for j in range(4):                                                                  # four cuts
            a = t + 9 + 1.5 * j
            lines += ['%.2f hand right %s 0 0 0' % (a, GUARD_R), '%.2f hand right %s 0 -120 0' % (a + 0.2, CUT_END),
                      '%.2f hand right %s 0 -120 0' % (a + 0.7, CUT_END), '%.2f hand right %s 0 0 0' % (a + 1.4, GUARD_R)]
    lines.sort(key=lambda l: float(l.split()[0]))
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/attack_arms')
    ap.add_argument('--swing-attack', choices=('0', '1'), help='SAVAGEWIND_VR_SWING_ATTACK (default: the option\'s default)')
    a = ap.parse_args()
    T.EXE = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    live = os.path.join(out, 'fake_xr_live.txt')
    for p in (live, live + '.ack'):
        if os.path.exists(p):
            os.unlink(p)
    env = {'FAKE_XR_CAPTURE_EVERY': '0', 'FAKE_XR_LIVE': live, 'SAVAGEWIND_VR_RENDER': 'stereo',
           'SAVAGEWIND_VR_LOG_ARMS': '9'}
    if a.swing_attack:
        env['SAVAGEWIND_VR_SWING_ATTACK'] = a.swing_attack
    game, proc, log = re_run.boot(a.save, out, env, script())
    seq = [0]
    shots = []

    def shot(tag):   # both eyes captured now (fake_xr's live file), waited for
        seq[0] += 1
        with open(live, 'w') as f:
            f.write('seq {}\nshot {}\n'.format(seq[0], tag))
        t0 = time.monotonic()
        while 'capture %s_' % tag not in T.fake_log(out) and time.monotonic() - t0 < 60:
            time.sleep(0.1)
        shots.append(tag)
    try:
        flog = os.path.join(out, 'fake_xr.log')

        def now():   # fake_xr's clock (the script's): its last frame's display time
            with open(flog, 'rb') as f:
                f.seek(max(0, os.path.getsize(flog) - 262144))
                t = re.findall(rb'display_time (\d+)', f.read())
            return int(t[-1]) / 1e9 if t else 0.0
        base = (now() // 40 + 1) * 40
        while now() < base + 8:
            time.sleep(0.2)
        shot('guard')   # the weapon out, no attack yet
        while now() < base + 9:
            time.sleep(0.05)
        v0 = game.cmd('vr', ok=False)
        offs = []
        while now() < base + 15:   # the four cuts: the armor's wrists against Link's, polled
            st = game.cmd('vr', ok=False)
            offs.append(st.get('armor_wrist_off_max', 0) or 0)
            if len(shots) < 4 and offs[-1] > 0.03:
                shot('off%d' % len(shots))   # (a picture of it, for a look)
            time.sleep(0.03)
        v1 = game.cmd('vr', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    d = lambda key: (v1.get(key) or 0) - (v0.get(key) or 0)  # noqa: E731
    attacks, pressed, hits = d('motion_attacks'), d('swing_attacks'), d('swing_hits')
    checks, off = d('armor_checks'), d('armor_off_frames')
    ok = attacks >= 2 and pressed == 0 and hits >= 1 and checks > 0 and off == 0
    print('  %s cuts read as attacks: %d' % ('ok  ' if attacks >= 2 else 'FAIL', attacks))
    print('  %s none pressed the game\'s attack (its animation): %d did' % ('ok  ' if pressed == 0 else 'FAIL', pressed))
    print('  %s the weapon hit by the swing: %d' % ('ok  ' if hits >= 1 else 'FAIL', hits))
    print('  %s the armor\'s wrists (sleeves, gloves) on Link\'s: %d of %d arm calcs more than 3 cm off, the worst %.2f m'
          % ('ok  ' if checks and off == 0 else 'FAIL', off, checks, max(offs or [0])))
    print('  --   pictures: %s' % ', '.join(shots))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
