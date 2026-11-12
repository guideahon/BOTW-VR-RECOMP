#!/usr/bin/env python3
"""The controllers shown where Link's arms are not the player's hands (Options > VR > Show the controllers, docs/VR.md),
against fake_xr, the hands held still (left at -0.3 1.2 -0.4, right at 0.3 1.2 -0.4, pointing ahead):
  first person (the default): on the title screen and while loading (the virtual screen) each frame has a 6 cm quad
      in each hand's space, 3 cm ahead of the grip (fake_xr's quad log: where it is in the room); in play none
      (Link's arms are the hands);
  third person: in play too;
  the option off (SAVAGEWIND_VR_SHOW_CONTROLLERS=0): never.

  python tools/vr/test_vr_markers.py --save <scratch save> [--out build/vr/markers]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402

HANDS = {'left_hand': (-0.3, 1.2, -0.43), 'right_hand': (0.3, 1.2, -0.43)}   # (the grip 3 cm further ahead)


def run(save, out, env):
    targs = argparse.Namespace(save=save, out=out, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                               runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792,
                               place='plateau', boot=180.0, settle=8.0, boot_estimate=60.0, capture_every=0)
    os.makedirs(out, exist_ok=True)
    e = {'SAVAGEWIND_VR_STEREO': '0', 'FAKE_XR_LOG_QUADS': '1'}
    e.update(env)
    script = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.3 1.2 -0.4 0 0 0', '0 hand right 0.3 1.2 -0.4 0 0 0']
    game, proc, log = T.launch(targs, out, script, frame_rate=0, extra_env=e)
    try:
        T.ready(targs, game, proc)
        v0 = T.vr_state(game)
        time.sleep(5)
        v1 = T.vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(os.path.join(out, 'fake_xr.log'), errors='replace').read()
    quads = [(m[0], tuple(float(x) for x in m[1:4]), float(m[4]))
             for m in re.findall(r'quad \d+ space (\w+) at (\S+) (\S+) (\S+) size (\S+) \S+', text)]
    return v1.get('marker_frames', 0) - v0.get('marker_frames', 0), v1.get('first_person'), quads


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/markers')
    a = ap.parse_args()
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    def placed(quads):
        hq = [q for q in quads if q[0] in HANDS]
        bad = [q for q in hq if max(abs(q[1][i] - HANDS[q[0]][i]) for i in range(3)) > 0.01 or abs(q[2] - 0.06) > 0.001]
        return hq, bad

    print('== first person (the default)')
    play, fp, quads = run(a.save, os.path.abspath(a.out + '_first'), {})
    hq, bad = placed(quads)
    check(len([q for q in hq if q[0] == 'left_hand']) > 20 and len([q for q in hq if q[0] == 'right_hand']) > 20,
          'markers on both hands before play ({} quads)'.format(len(hq)))
    check(hq and not bad, 'each 6 cm, at its controller ({} misplaced{})'.format(len(bad), ', e.g. %s' % (bad[:1],) if bad else ''))
    check(play == 0, 'none in first-person play ({} frames; first person {})'.format(play, fp))
    print('== third person')
    play, fp, quads = run(a.save, os.path.abspath(a.out + '_third'), {'SAVAGEWIND_VR_VIEW': 'third'})
    check(play > 100, 'markers in third-person play ({} frames)'.format(play))
    print('== off')
    play, fp, quads = run(a.save, os.path.abspath(a.out + '_off'), {'SAVAGEWIND_VR_SHOW_CONTROLLERS': '0',
                                                                     'SAVAGEWIND_VR_VIEW': 'third'})
    hq, bad = placed(quads)
    check(not hq and play == 0, 'never ({} quads, {} frames)'.format(len(hq), play))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
