#!/usr/bin/env python3
"""Each eye's own off-centre frustum (docs/VR.md), against fake_xr in stereo (its eyes are off-centre, as a Quest's:
54 deg out, 40 deg in, 44 up, 51 down): the left eye's picture with the symmetric frustum (SAVAGEWIND_VR_ASYMMETRIC=0)
and with the eye's own, the head still. Each picture is mapped to view angles with the field it was submitted with
(fake_xr's `capture ... fov` lines) and the two compared there: they must line up (the scene where the headset is told
it is), at most a couple of samples (0.01 of a tangent each) apart, and the asymmetric one must cover a smaller angle
(the same pixels on less: sharper). A run with the offset's sign flipped (SAVAGEWIND_VR_OFFSET_SIGN=-1) must not line
up: the check can tell.

  python tools/vr/test_vr_frustum.py --save <scratch save> [--out build/vr/frustum]
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

STEP = 0.01
GX = [-0.75 + STEP * i for i in range(int(1.35 / STEP))]    # (tangents both pictures cover)
GY = [0.75 - STEP * j for j in range(int(1.5 / STEP))]


def read_ppm(path):
    b = open(path, 'rb').read()
    m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+\d+\s', b)
    return int(m.group(1)), int(m.group(2)), b[m.end():]


def to_angles(path, fov):
    """the picture's grey levels on the common grid of tangents"""
    w, h, px = read_ppm(path)
    tl, tr, tu, td = (math.tan(a) for a in fov)
    grid = []
    for ty in GY:
        v = int((tu - ty) / (tu - td) * h)
        row = []
        for tx in GX:
            u = int((tx - tl) / (tr - tl) * w)
            if 0 <= u < w and 0 <= v < h:
                i = (v * w + u) * 3
                row.append((px[i] + px[i + 1] + px[i + 2]) / 3)
            else:
                row.append(None)
        grid.append(row)
    return grid


def best_shift(a, b, r=12):
    """the (dx, dy) in grid samples that best lines b up with a, over the sky-free lower 2/3 and the far middle"""
    best = None
    for dy in range(-r, r + 1, 2):
        for dx in range(-r, r + 1):
            s = n = 0
            for j in range(len(GY) // 3, len(GY) - r, 3):
                ra, rb = a[j], b[j + dy] if 0 <= j + dy < len(GY) else None
                if rb is None:
                    continue
                for i in range(r, len(GX) - r, 3):
                    x, y = ra[i], rb[i + dx]
                    if x is not None and y is not None:
                        s += abs(x - y)
                        n += 1
            if n and (best is None or s / n < best[0]):
                best = (s / n, dx, dy)
    return best


def run(save, out, env_extra):
    env = {'FAKE_XR_CAPTURE_EVERY': '450', 'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_ROOMSCALE': '0'}
    env.update(env_extra)
    game, proc, log = re_run.boot(save, out, env, ['0 head 0 1.7 0 0 0 0'])
    try:
        time.sleep(12)
    finally:
        perf_routes.stop_game(game, proc, log)
    text = open(os.path.join(out, 'fake_xr.log'), errors='replace').read()
    fovs = re.findall(r'capture (frame\d+_layer0_view0) fov (\S+) (\S+) (\S+) (\S+)', text)
    tag, *f = fovs[-1]
    return to_angles(os.path.join(out, 'cap', tag + '.ppm'), [float(x) for x in f]), [float(x) for x in f]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/frustum')
    a = ap.parse_args()
    sym, fs = run(a.save, a.out + '_sym', {'SAVAGEWIND_VR_ASYMMETRIC': '0'})
    asy, fa = run(a.save, a.out + '_asym', {})
    bad, fb = run(a.save, a.out + '_flipped', {'SAVAGEWIND_VR_OFFSET_SIGN': '-1'})
    deg = lambda f: ' '.join('{:.1f}'.format(math.degrees(x)) for x in f)  # noqa: E731
    area = lambda f: (math.tan(f[1]) - math.tan(f[0])) * (math.tan(f[2]) - math.tan(f[3]))  # noqa: E731
    print('left eye submitted with: symmetric {} | own {} (deg L R U D); angular area {:.2f} -> {:.2f} ({:+.0%})'.format(
        deg(fs), deg(fa), area(fs), area(fa), area(fa) / area(fs) - 1))
    e1, dx1, dy1 = best_shift(sym, asy)
    e2, dx2, dy2 = best_shift(sym, bad)
    print('own frustum vs symmetric: best match at ({}, {}) samples, difference {:.1f}'.format(dx1, dy1, e1))
    print('offset sign flipped     : best match at ({}, {}) samples, difference {:.1f}'.format(dx2, dy2, e2))
    ok = abs(dx1) <= 2 and abs(dy1) <= 2 and area(fa) < area(fs) * 0.9 and (abs(dx2) > 4 or abs(dy2) > 4)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
