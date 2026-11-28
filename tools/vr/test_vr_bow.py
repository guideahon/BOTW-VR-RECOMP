#!/usr/bin/env python3
"""The bow aimed by hand (docs/VR.md), against fake_xr: equips the bow with the left-shoulder grip, holds the hands
60 degrees right of the head (the head keeps looking ahead), draws with the right trigger for 3 s and looses. Passes when
the runtime turned the shot (arrows_aimed) and every arrow, logged in flight by SAVAGEWIND_VR_LOG_MODELS, flies along the
hands' line and not where the head looks (with a multi-shot bow equipped, --others 2: a Lynel bow's other two as well), and the eye's pictures show the arc's yellow dots while drawing, not
before and not after the shot.

  python tools/vr/test_vr_bow.py --save <scratch save> [--exe build/native-vr/savagewind] [--others 2] [--out build/vr/bow]
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
import test_vr_game as T  # noqa: E402

GL = '/user/hand/left/input/squeeze/value'
TR = '/user/hand/right/input/trigger/value'


def script():
    rest_l, rest_r = '-0.25 1.2 -0.3', '0.25 1.2 -0.3'
    a = math.radians(60)
    string = (0.05, 1.5, -0.1)
    bow = (string[0] + 0.5 * math.sin(a), 1.5, string[2] - 0.5 * math.cos(a))
    s = ['0 head 0 1.7 0 0 0 0']
    for k in range(1, 40):
        t = 40.0 * k
        s += ['%.1f hand left %s 0 0 0' % (t, rest_l), '%.1f hand right %s 0 0 0' % (t, rest_r),
              '%.1f value %s 0' % (t, GL), '%.1f value %s 0' % (t, TR),
              '%.1f hand left %s 0 0 0' % (t + 4, rest_l), '%.1f hand left -0.15 1.65 0.12 0 0 0' % (t + 4.5),   # bow out
              '%.1f value %s 1' % (t + 5, GL), '%.1f value %s 0' % (t + 5.2, GL),
              '%.1f hand left -0.15 1.65 0.12 0 0 0' % (t + 6),
              '%.1f hand left %.3f %.3f %.3f -60 0 0' % (t + 7, *bow),
              '%.1f hand right %.3f %.3f %.3f -60 0 0' % (t + 7, *string),
              '%.1f value %s 1' % (t + 9, TR), '%.1f value %s 0' % (t + 12, TR)]   # draw 3 s, loose
    s.sort(key=lambda l: float(l.split()[0]))
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--others', type=int, default=0,
                    help='a multi-shot bow equipped: at least this many other arrows turned with the main one')
    ap.add_argument('--out', default='build/vr/bow')
    ap.add_argument('--capture-every', type=int, default=30,
                    help='fake_xr frames between screenshots (a larger gap reduces GPU readback stalls)')
    a = ap.parse_args()
    T.EXE = os.path.abspath(a.exe)
    env = {'FAKE_XR_CAPTURE_EVERY': str(a.capture_every), 'SAVAGEWIND_VR_LOG_MODELS': 'Weapon_Arrow',
           'SAVAGEWIND_VR_LOG_ARROWS': '1'}
    game, proc, log = re_run.boot(a.save, a.out, env, script())
    try:
        now = lambda: game.cmd('vr', ok=False, timeout=60).get('frames', 0) / 90.0  # noqa: E731
        base = (now() // 40 + 1) * 40   # the next round of the script
        while now() < base + 6.5:
            time.sleep(0.1)
        t_before = time.time()      # the bow out, no arrow yet
        while now() < base + 8.5:
            time.sleep(0.1)
        t_draw = time.time()
        while now() < base + 10.5:
            time.sleep(0.1)
        pos = game.cmd('pos', ok=False)
        lx, lz = pos.get('x', 0), pos.get('z', 0)
        t_drawn = time.time()
        while now() < base + 11.8:
            time.sleep(0.1)
        t_end = time.time()
        arc_frames = game.cmd('vr', ok=False).get('arc_frames', 0)
        while now() < base + 12.6:
            time.sleep(0.1)
        t_shot = time.time()   # the trigger let go at 12: the arc gone
        while now() < base + 14:
            time.sleep(0.1)
        st = game.cmd('vr', ok=False)
        aimed, side = st.get('arrows_aimed', 0), st.get('side_arrows_aimed', 0)
        t_after = time.time()
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(os.path.join(a.out, 'game.log'), errors='replace').read()
    shot = re.findall(r'\[vr-arrow\] shot from \S+ \S+ \S+ \(\S+ m from Link\) dir (\S+) \S+ (\S+)', text)
    hand = re.findall(r'\[vr-arrow\] aimed by hand: (\S+) \S+ (\S+)', text)
    head_h = math.degrees(math.atan2(float(shot[0][0]), float(shot[0][1]))) if shot else None
    hand_h = math.degrees(math.atan2(float(hand[0][0]), float(hand[0][1]))) if hand else None
    # each arrow in flight (a Weapon_Arrow model, by its address): its heading when first 4..10 m from Link. A multi-
    # shot bow's other arrows (2026-11-27: they flew where the head looked) must go along the hands too, within the
    # bow's spread
    flown = {}
    for m in re.finditer(r'\[vr-model\] \d+ Weapon_Arrow\S* (\S+) (\S+) \S+ (\S+)', text):
        dx, dz = float(m.group(2)) - lx, float(m.group(3)) - lz
        if 4 < math.hypot(dx, dz) < 10 and m.group(1) not in flown:
            flown[m.group(1)] = math.degrees(math.atan2(dx, dz))
    fly = sorted(flown.values())
    print('arrows aimed', aimed, '(others', side, ') | head heading', head_h, '| hands', hand_h, '| arrows flew',
          ['%.1f' % f for f in fly])
    ok = (aimed >= 1 and hand_h is not None and fly and all(abs(f - hand_h) < 8 for f in fly) and
          (head_h is None or all(abs(f - head_h) > 20 for f in fly)) and side >= a.others)
    # the arc's dots in the left eye's pictures: before the draw, during it and after the shot
    def dots(path):
        b = open(path, 'rb').read()
        m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+\d+\s', b)
        w, h = int(m.group(1)), int(m.group(2))
        px = b[m.end():]
        # the arc's own colour (255 214 72, copied as it is): the golden bow and the flowers are near it, not on it
        return sum(1 for i in range(0, w * h * 3, 3)
                   if px[i] >= 250 and 206 <= px[i + 1] <= 222 and 64 <= px[i + 2] <= 80)
    capdir = os.path.join(a.out, 'cap')
    caps = [os.path.join(capdir, f) for f in os.listdir(capdir) if f.endswith('_view0.ppm')]
    before = [dots(c) for c in caps if t_before <= os.path.getmtime(c) < t_draw - 0.5]
    during = [dots(c) for c in caps if t_drawn <= os.path.getmtime(c) < t_end]
    after = [dots(c) for c in caps if t_shot <= os.path.getmtime(c) < t_after]
    print('arc frames', arc_frames, '| arc pixels before the draw', before[-3:], '| while drawing', during[-3:],
          '| after the shot', after[-3:])
    ok = (ok and arc_frames > 0 and before and during and after and max(before) < 20 and min(during) >= 100 and
          max(after) < 20)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
