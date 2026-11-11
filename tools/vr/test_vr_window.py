#!/usr/bin/env python3
"""The desktop window shows one eye in stereo (docs/VR.md), against fake_xr: in play the game draws a left and a right
frame per headset frame; each right frame, once copied to the headset, gets the left one's picture before the window
shows it. Passes when, over a few seconds, the right frames that showed the left picture (window_left_frames) keep pace
with the stereo pairs, and with SAVAGEWIND_VR_WINDOW=both (a second run, the earlier flickering window) none do.
(The pictures themselves are not compared: grass, clouds and Link's idle move more between two screenshots than the
eyes' 6.4 cm apart do.)

  python tools/vr/test_vr_window.py --save <scratch save> [--out build/vr/window]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402


def run(save, out, mode):
    env = {'FAKE_XR_CAPTURE_EVERY': '0', 'SAVAGEWIND_VR_STEREO': '1'}
    if mode:
        env['SAVAGEWIND_VR_WINDOW'] = mode
    game, proc, log = re_run.boot(save, out, env, ['0 head 0 1.7 0 0 0 0'])
    try:
        a = game.cmd('vr', ok=False)
        time.sleep(5)
        b = game.cmd('vr', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    return (b.get('stereo_pairs', 0) - a.get('stereo_pairs', 0),
            b.get('window_left_frames', 0) - a.get('window_left_frames', 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/window')
    a = ap.parse_args()
    pairs, left = run(a.save, a.out + '_one', None)
    print('window on one eye: {} stereo pairs, {} right frames showed the left picture'.format(pairs, left))
    pairs_b, left_b = run(a.save, a.out + '_both', 'both')
    print('window on both   : {} stereo pairs, {} right frames showed the left picture'.format(pairs_b, left_b))
    ok = pairs > 100 and abs(left - pairs) <= max(3, pairs // 50) and pairs_b > 100 and left_b == 0
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
