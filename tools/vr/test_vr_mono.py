#!/usr/bin/env python3
"""Mono (SAVAGEWIND_VR_STEREO=0: one picture for both eyes, docs/vr/PERFORMANCE.md), against fake_xr with the head
turned off the world's axes: the checks the performance plan asks of it before any player option.

- the log says mono;
- each capture's two eye images are the same bytes (the scene before the runtime's composition);
- both projection views are submitted from the head's centre (FAKE_XR_LOG_VIEWS: under 1 mm from it), with the same,
  symmetric field;
- with --control, one game update a headset frame: the `vr` command's camera_updates a headset frame half normal
  stereo's (the game sets a few cameras a frame, so the count is compared, not taken as 1).

What it cannot show: comfort, legibility and whether mono serves people with one working eye - those need a person in a
headset. --control runs stereo (SAVAGEWIND_VR_RENDER=stereo: a game frame per eye) the same way: its views must come out half the IPD off the centre (the check
sees a stereo submission), its images differ.

  python tools/vr/test_vr_mono.py --save <scratch save> [--out build/vr/mono] [--control]
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

EVERY = 300


def run(save, out, stereo):
    env = {'SAVAGEWIND_VR_STEREO': '1' if stereo else '0', 'FAKE_XR_CAPTURE_EVERY': str(EVERY),
           'SAVAGEWIND_VR_RENDER': 'stereo' if stereo else 'mono',
           'FAKE_XR_LOG_VIEWS': str(EVERY), 'SAVAGEWIND_VR_ROOMSCALE': '0'}
    game, proc, log = re_run.boot(save, out, env, ['0 head 0 1.7 0 37 0 0'])
    try:
        v0 = T.vr_state(game)
        time.sleep(10)
        v1 = T.vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    fl = T.fake_log(out)
    gl = open(os.path.join(out, 'game.log'), encoding='utf-8', errors='replace').read()
    caps = sorted(set(re.findall(r'capture (\S+)_view0 ', fl)))
    same = []
    for c in caps:
        a, b = os.path.join(out, 'cap', c + '_view0.ppm'), os.path.join(out, 'cap', c + '_view1.ppm')
        if os.path.exists(a) and os.path.exists(b):
            same.append(open(a, 'rb').read() == open(b, 'rb').read())
    views = [(int(v), [float(x) for x in (p + ' ' + f).split()])
             for v, p, f in re.findall(r'^view (\d) frame \d+ from head (\S+ \S+ \S+) fov (.+)$', fl, re.M)]
    frames = v1.get('frames', 0) - v0.get('frames', 0)
    upd = (v1.get('camera_updates', 0) - v0.get('camera_updates', 0)) / max(frames, 1)
    return gl, same, views, upd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/mono')
    ap.add_argument('--control', action='store_true', help='also run normal stereo, which must fail the mono checks')
    a = ap.parse_args()
    T.EXE = os.path.abspath(a.exe)
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    print('== mono')
    gl, same, views, upd = run(a.save, os.path.abspath(a.out), False)
    check('[vr] mono: one picture for both eyes' in gl, 'the log says mono')
    check(len(same) >= 2 and all(same), 'both eyes the same bytes: {} of {} captures'.format(sum(same), len(same)))
    off = max((max(abs(x) for x in v[:3]) for _, v in views), default=None)
    check(off is not None and off < 0.001, 'views submitted from the head\'s centre: furthest {} m ({} views logged)'
          .format('?' if off is None else '{:.4f}'.format(off), len(views)))
    fovs = {tuple(round(x, 4) for x in v[3:]) for _, v in views}
    sym = all(abs(f[0] + f[1]) < 1e-3 for f in fovs)
    check(len(fovs) == 1 and sym, 'one symmetric field for both views: {}'.format(sorted(fovs)))
    # (camera_updates counts every world camera the game sets, a few a game frame: compared against normal stereo's,
    # which updates the game twice a headset frame)
    print('    camera updates a headset frame: {:.2f}'.format(upd))
    if a.control:
        print('== control: normal stereo (these must differ from mono)')
        gl, same, views, upd_s = run(a.save, os.path.abspath(a.out) + '_stereo', True)
        xs = sorted(round(v[0], 4) for _, v in views)
        print('    eyes the same bytes: {} of {}; views x from the head: {} .. {}; camera updates a headset frame {:.2f}'
              .format(sum(same), len(same), xs[0] if xs else '?', xs[-1] if xs else '?', upd_s))
        check(not any(same) and xs and xs[0] < -0.02 and xs[-1] > 0.02, 'the control is told apart from mono')
        check(upd_s > 0 and 0.4 <= upd / upd_s <= 0.6,
              'one game update a headset frame: mono {:.2f} against stereo {:.2f} camera updates'.format(upd, upd_s))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
