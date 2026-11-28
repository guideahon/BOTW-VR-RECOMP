#!/usr/bin/env python3
"""Both eyes the same picture when they stand in one place (docs/vr/PERFORMANCE.md): against fake_xr with the eyes 0 m
apart (FAKE_XR_IPD=0) every difference between the two pictures - once each is placed by the field it was submitted
with - is something drawn for one eye only or drawn differently (smoke in one eye, a patch of ground missing, a marker
moved, a darker eye). Multiview must pass it before Automatic picks it (vr_camera.c VR_AUTO_MULTIVIEW).

The game goes to --place (a place `goto` knows: Kakariko has chimneys' smoke), the hour is set, and the head looks round
(--yaws); at each yaw both eyes are captured (several seconds apart: a capture stalls the game). For each capture: the
part of the common field that differs by more than --level grey levels (colour, 0-255), and a picture of where
(<out>/diff_<yaw>.png: left | right | the difference, red). In normal stereo the right eye is a later game frame, so
moving things (smoke, leaves, water) differ a little there too: compare a mode against that, not against zero.

  python tools/vr/test_vr_eye_match.py --save <scratch save> [--mode multiview|stereo|mono] [--place Kakariko]
        [--hour 12] [--yaws 0 90 180 270] [--pitch -10] [--limit 1.0] [--out build/vr/eye_match] [--exe <game>]
Passes when every capture's differing part is under --limit percent.
"""
import argparse
import os
import re
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

STEP = 0.0025   # (the common grid: tangents, both ways)


def placed(path, fov):
    """the picture's colours on a grid of view tangents (left, right, up, down are the submitted field, radians)"""
    im = np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
    h, w = im.shape[:2]
    tl, tr, tu, td = (np.tan(f) for f in fov)
    return im, (tl, tr, tu, td), w, h


def common(a, b):
    (ia, fa, wa, ha), (ib, fb, wb, hb) = a, b
    l, r = max(fa[0], fb[0]), min(fa[1], fb[1])
    d, u = max(fa[3], fb[3]), min(fa[2], fb[2])
    xs, ys = np.arange(l, r, STEP), np.arange(u, d, -STEP)

    def sample(im, f, w, h):
        px = ((xs - f[0]) / (f[1] - f[0]) * w).astype(int).clip(0, w - 1)
        py = ((f[2] - ys) / (f[2] - f[3]) * h).astype(int).clip(0, h - 1)
        return im[py][:, px]
    return sample(ia, fa, wa, ha), sample(ib, fb, wb, hb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/eye_match')
    ap.add_argument('--mode', default='multiview', choices=('multiview', 'stereo', 'sync', 'mono', 'auto'))
    ap.add_argument('--place', default='Kakariko')
    ap.add_argument('--hour', default='12')
    ap.add_argument('--yaws', type=int, nargs='+', default=[0, 90, 180, 270])
    ap.add_argument('--pitch', type=float, default=-10)
    ap.add_argument('--level', type=int, default=24)
    ap.add_argument('--limit', type=float, default=1.0)
    ap.add_argument('--env', nargs='*', default=[])
    ap.add_argument('--exe', default=T.EXE)
    a = ap.parse_args()
    T.EXE = a.exe
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    live = os.path.join(out, 'fake_xr_live.txt')
    for p in (live, live + '.ack'):
        if os.path.exists(p):
            os.unlink(p)
    # (sync: synchronous stereo, both eyes from one game frame drawn one after the other - SAVAGEWIND_VR_SYNC=1)
    env = {'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_RENDER': 'stereo' if a.mode == 'sync' else a.mode,
 'SAVAGEWIND_VR_EXPERIMENTAL': '1' if a.mode == 'multiview' else '0', 'FAKE_XR_IPD': '0', 'FAKE_XR_LIVE': live,
           'FAKE_XR_CAPTURE_EVERY': '0', 'SAVAGEWIND_VR_ROOMSCALE': '0'}
    if a.mode == 'sync':
        env['SAVAGEWIND_VR_SYNC'] = '1'
    for kv in a.env:
        k, v = kv.split('=', 1)
        env[k] = v
    game, proc, log = re_run.boot(a.save, out, env, ['0 head 0 1.7 0 0 0 0'])
    seq = [0]

    def send(*lines):
        seq[0] += 1
        with open(live, 'w') as f:
            f.write('seq {}\n{}\n'.format(seq[0], '\n'.join(lines)))
        until = time.monotonic() + 5
        while time.monotonic() < until:
            try:
                with open(live + '.ack') as f:
                    if int(f.read().split()[0]) >= seq[0]:
                        return
            except (OSError, ValueError, IndexError):
                pass
            time.sleep(0.03)

    try:
        if a.place:
            r = game.cmd('goto', a.place, ok=False, timeout=60)
            print('goto {}: {}'.format(a.place, r if isinstance(r, dict) and r.get('error') else 'ok'))
            time.sleep(12)   # (the place loads, the camera settles)
        if a.hour:
            game.cmd('time', 'set', a.hour, ok=False)
            time.sleep(2)
        for y in a.yaws:
            send('head 0 1.7 0 {} {} 0'.format(y, a.pitch))
            time.sleep(2)
            send('head 0 1.7 0 {} {} 0'.format(y, a.pitch), 'shot y{}'.format(y))
            time.sleep(3)
    finally:
        perf_routes.stop_game(game, proc, log)
    fl = T.fake_log(out)
    gl = open(os.path.join(out, 'game.log'), encoding='utf-8', errors='replace').read()
    m = re.search(r'^\[vr\] rendering: (.*)$', gl, re.M)
    print('rendering:', m.group(1) if m else '? (no "[vr] rendering" line)')
    fov = dict((t, [float(x) for x in f]) for t, *f in re.findall(r'capture (\S+) fov (\S+) (\S+) (\S+) (\S+)', fl))
    ok = True
    for y in a.yaws:
        lt = next((t for t in sorted(fov) if re.fullmatch(r'y%d_layer\d+_view0' % y, t)), None)
        if not lt:
            print('yaw {:3}: no capture'.format(y))
            ok = False
            continue
        rt = lt[:-1] + '1'
        cap = os.path.join(out, 'cap')
        A = placed(os.path.join(cap, lt + '.ppm'), fov[lt])
        B = placed(os.path.join(cap, rt + '.ppm'), fov[rt])
        pa, pb = common(A, B)
        d = np.abs(pa - pb).max(axis=2)
        bad = d > a.level
        part = 100.0 * bad.mean()
        # where: the picture split in a 4 x 3 grid, the worst cell
        gh, gw = bad.shape
        cells = [(100.0 * bad[i * gh // 3:(i + 1) * gh // 3, j * gw // 4:(j + 1) * gw // 4].mean(), i, j)
                 for i in range(3) for j in range(4)]
        worst = max(cells)
        mean_l, mean_r = pa.mean(), pb.mean()
        print('yaw {:3}: {:5.2f}% differs by more than {} (worst cell row {} col {}: {:.1f}%), mean level left {:.0f} '
              'right {:.0f}'.format(y, part, a.level, worst[1], worst[2], worst[0], mean_l, mean_r))
        ok &= part < a.limit
        red = np.zeros_like(pa)
        red[..., 0] = np.where(bad, 255, (d * 3).clip(0, 255))
        Image.fromarray(np.concatenate([pa, pb, red], axis=1).clip(0, 255).astype(np.uint8)).save(
            os.path.join(out, 'diff_y{}.png'.format(y)))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
