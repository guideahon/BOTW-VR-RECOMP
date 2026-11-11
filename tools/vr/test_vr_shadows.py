#!/usr/bin/env python3
"""The right eye reuses the left one's sun shadow maps (docs/VR.md), against fake_xr in stereo: the same place and
head twice, with it (the default) and without (SAVAGEWIND_VR_SHADOWS_ONCE=0). Reports the game's frame rate (the
`[vk] t=` lines after the world is up) and the shadow passes left out, and keeps a right-eye capture of each run
(<out>_once/cap, <out>_both/cap) to look at. Passes when passes were left out (several each pair) only with it, and the
frame rate did not drop.

  python tools/vr/test_vr_shadows.py --save <scratch save> [--out build/vr/shadows]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402


def run(save, out, once):
    env = {'FAKE_XR_CAPTURE_EVERY': '900', 'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_ROOMSCALE': '0'}
    if not once:
        env['SAVAGEWIND_VR_SHADOWS_ONCE'] = '0'
    game, proc, log = re_run.boot(save, out, env, ['0 head 0 1.7 0 0 0 0'])
    logpath = os.path.join(out, 'game.log')
    try:
        time.sleep(5)
        mark = os.path.getsize(logpath)
        a = game.cmd('vr', ok=False)
        time.sleep(25)
        b = game.cmd('vr', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(logpath, 'rb').read()[mark:].decode('utf-8', 'replace')
    fps = [float(x) for x in re.findall(r'\[vk\] t=\S+ frame \d+ ([\d.]+) fps', text)]
    pairs = b.get('stereo_pairs', 0) - a.get('stereo_pairs', 0)
    skipped = b.get('shadow_passes_skipped', 0) - a.get('shadow_passes_skipped', 0)
    return (sum(fps) / len(fps) if fps else 0), pairs, skipped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/shadows')
    a = ap.parse_args()
    f1, p1, s1 = run(a.save, a.out + '_once', True)
    print('shadows once: {:.1f} fps, {} stereo pairs, {} shadow passes left out ({:.1f} a pair)'.format(
        f1, p1, s1, s1 / max(p1, 1)))
    f2, p2, s2 = run(a.save, a.out + '_both', False)
    print('shadows twice: {:.1f} fps, {} stereo pairs, {} shadow passes left out'.format(f2, p2, s2))
    print('frame rate: {:+.1f}%'.format((f1 / f2 - 1) * 100 if f2 else 0))
    ok = s1 >= p1 and s2 == 0 and f1 >= f2 * 0.97
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
