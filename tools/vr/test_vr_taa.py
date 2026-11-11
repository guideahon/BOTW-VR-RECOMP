#!/usr/bin/env python3
"""TAA in stereo (docs/VR.md), against fake_xr with the eyes 25 cm apart (four times a person's: what one eye's picture
blended into the other's would show is plain): three runs with Anti-aliasing TAA, the head still, the eyes' pictures
compared as the headset shows them (each placed by its submitted field):
  per-eye   (the default) each eye its own history
  shared    SAVAGEWIND_VR_TAA_SHARED=1: one history for both eyes (what stereo had: the eyes blend into each other)
  smaa      SAVAGEWIND_VR_TAA=0: SMAA in place of TAA (no history: the reference)
For each, how much of the right eye's difference from the left one a sideways shift explains (the rest: blending, or
noise). Passes when the per-eye run's eyes are lit (TAA's picture is not black: it was, off-centre, so TAA keeps a
symmetric frustum) and line up at least as well as SMAA's. The shared run is reported, not judged: with the camera's
reprojection a still scene looks alike either way; the per-eye history is what a moving one needs. The pictures are
kept in <out>_<run>/cap to look at.

  python tools/vr/test_vr_taa.py --save <scratch save> [--out build/vr/taa]
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


def run(save, out, env_extra):
    env = {'FAKE_XR_CAPTURE_EVERY': '600', 'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_ROOMSCALE': '0',
           'FAKE_XR_IPD': '0.25'}
    env.update(env_extra)
    game, proc, log = re_run.boot(save, out, env, ['0 head 0 1.7 0 0 0 0'])
    try:
        time.sleep(12)
    finally:
        perf_routes.stop_game(game, proc, log)
    fl = open(os.path.join(out, 'fake_xr.log'), errors='replace').read()
    fov = dict((t, [float(x) for x in f]) for t, *f in re.findall(r'capture (\S+) fov (\S+) (\S+) (\S+) (\S+)', fl))
    tags = sorted(t for t in fov if t.endswith('_view0'))
    lt = tags[-1]
    rt = lt.replace('_view0', '_view1')
    a = T.read_ppm_angles(os.path.join(out, 'cap', lt + '.ppm'), fov[lt])
    b = T.read_ppm_angles(os.path.join(out, 'cap', rt + '.ppm'), fov[rt])
    dx, dy, best, at0 = T.best_shift(a, b)
    lit = re.findall(r'capture %s \d+x\d+ mean (\S+) (\S+) (\S+)' % re.escape(rt), fl)
    bright = sum(float(x) for x in lit[-1]) / 3 if lit else 0
    return dx, dy, best, at0, 1 - best / at0 if at0 else 0, bright


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/taa')
    a = ap.parse_args()
    res = {}
    for name, env in (('per-eye', {}), ('shared', {'SAVAGEWIND_VR_TAA_SHARED': '1'}), ('smaa', {'SAVAGEWIND_VR_TAA': '0'})):
        dx, dy, best, at0, share, bright = run(a.save, a.out + '_' + name, env)
        res[name] = (share, bright)
        print('{:8}: right eye mean level {:.0f}; against left: shift ({}, {}), difference {:.1f} -> {:.1f}: the shift '
              'explains {:.0%}'.format(name, bright, dx, dy, at0, best, share))
    ok = res['per-eye'][1] > 40 and res['per-eye'][0] >= res['smaa'][0] - 0.1
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
