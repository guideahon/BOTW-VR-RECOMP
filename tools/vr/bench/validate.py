#!/usr/bin/env python3
"""Everything that can differ between the eyes, one boot per rendering mode (~5 min), against fake_xr with the eyes
0 m apart (FAKE_XR_IPD=0: both pictures must be one picture). What a headset showed in Multiview (O18, 2026-11-25:
smoke, a patch of ground and a marker in one eye only, snow in another place in each eye) and what each mode could get
wrong: the places below at several yaws and pitches (smoke and torches, night and rain, snow, sand, the sea and its
reflections, a far dense view, the ground at the feet, the sky and the sun), third person orbiting, the inventory and
the map opened and closed.

For each capture: the part of the common field differing by more than 24 grey levels (% "diff"), the worst cell of a
4 x 3 grid (where it is), each eye's mean brightness (a darker eye), the sideways shift between them, and a picture
(<out>/<case>/d_<tag>.png: left | right | difference x4). The speed: headset fps standing at the Great Plateau.

  python tools/vr/bench/validate.py --exe build/savagewind --save <scratch save> --case multiview
        --env SAVAGEWIND_VR_RENDER=multiview SAVAGEWIND_VR_EXPERIMENTAL=1
Prints one line per capture and RESULT {json}.
"""
import argparse
import json
import os
import re
import shutil
import sys
import threading
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import bench  # noqa: E402
import quick  # noqa: E402

R = '/user/hand/right/input/thumbstick'
# (place for goto or None: stay; hour; weather; [(tag, yaw, pitch)])
SCENES = [
    (None, '12', 'clear', [('plateau_y0', 0, -10), ('plateau_y90', 90, -10), ('plateau_y180', 180, -10),
                           ('plateau_y270', 270, -10), ('plateau_ground', 0, -55), ('plateau_sky', 0, 25)]),
    ('Kakariko', '12', 'clear', [('kak_y0', 0, -10), ('kak_y90', 90, -10), ('kak_y180', 180, -10), ('kak_y270', 270, -10)]),
    ('Kakariko', '22', 'rain', [('kaknight_y0', 0, -10), ('kaknight_y180', 180, -10)]),
    ('Hebra Tower', '12', 'snow', [('hebra_y0', 0, -10), ('hebra_y180', 180, -10), ('hebra_ground', 90, -50)]),
    ('Gerudo Town', '12', 'clear', [('gerudo_y0', 0, -10), ('gerudo_y180', 180, -10)]),
    ('Lurelin', '17', 'clear', [('lurelin_y0', 0, -10), ('lurelin_y90', 90, -25), ('lurelin_y180', 180, -10)]),
    ('Hyrule Castle', '12', 'clear', [('castle_y0', 0, 0), ('castle_y180', 180, 0)]),
]


def detail(a, b, level=24):
    d = np.abs(a - b).max(axis=2) > level
    gh, gw = d.shape
    cells = [(100.0 * d[i * gh // 3:(i + 1) * gh // 3, j * gw // 4:(j + 1) * gw // 4].mean(), i, j)
             for i in range(3) for j in range(4)]
    w = max(cells)
    where = '%s-%s' % (('top', 'mid', 'bottom')[w[1]], ('left', 'cl', 'cr', 'right')[w[2]])
    return round(w[0], 1), where, round(float(a.mean()), 1), round(float(b.mean()), 1)


def main():
    ap = argparse.ArgumentParser()
    common.add_args(ap)
    ap.add_argument('--case', default='validate')
    ap.add_argument('--boot', type=float, default=300)
    ap.add_argument('--measure', type=float, default=12)
    ap.add_argument('--limit', type=float, default=1.0,
                    help='the most of the field that may differ between the eyes, %% (Stereo: its fires flicker per eye, 15)')
    ap.add_argument('--min-fps', type=float, default=0, help='fail under this many headset frames a second (0: not judged)')
    a = ap.parse_args()
    run_dir = os.path.join(os.path.abspath(a.out), a.case)
    shutil.rmtree(run_dir, ignore_errors=True)
    os.makedirs(run_dir)
    live = os.path.join(run_dir, 'fake_xr_live.txt')
    game, proc, log = common.launch(a, run_dir, live=live, extra_env=['FAKE_XR_IPD=0', 'SAVAGEWIND_VR_ROOMSCALE=0'])
    seq = [0]

    def send(*lines):
        seq[0] += 1
        with open(live, 'w') as f:
            f.write('seq {}\n{}\n'.format(seq[0], '\n'.join(lines)))
        until = time.monotonic() + 10
        while time.monotonic() < until:
            try:
                with open(live + '.ack') as f:
                    if int(f.read().split()[0]) >= seq[0]:
                        return
            except (OSError, ValueError, IndexError):
                pass
            time.sleep(0.03)

    def flog():
        try:
            return open(os.path.join(run_dir, 'fake_xr.log'), encoding='utf-8', errors='replace').read()
        except OSError:
            return ''

    def shot(tag, stick=0.0, n=0, limit=120):
        send('stick {} {} 0'.format(R, stick), 'shot {} {}'.format(tag, n) if n else 'shot {}'.format(tag))
        want = 'capture %s_%d_' % (tag, n - 1) if n else 'capture %s_' % tag
        t0 = time.monotonic()
        while time.monotonic() - t0 < limit and want not in flog():
            time.sleep(0.3)

    tags, notes = [], []
    res = {'case': a.case, 'env': a.env}
    alive = True
    try:
        common.ready(game, proc, a.boot)
        stop, gpu = threading.Event(), {}
        th = threading.Thread(target=bench.gpu_sampler, args=(stop, gpu), daemon=True)
        v0 = game.cmd('vr', ok=False, timeout=60); t0 = time.monotonic()
        th.start()
        time.sleep(a.measure)
        v1 = game.cmd('vr', ok=False, timeout=60); t1 = time.monotonic()
        stop.set(); th.join()
        res['hmd_fps'] = round((v1['frames'] - v0['frames']) / (t1 - t0), 1)
        for place, hour, weather, views in SCENES:
            if place:
                r = game.cmd('goto', place, ok=False, timeout=90)
                if isinstance(r, dict) and r.get('error'):
                    notes.append('goto %s: %s' % (place, r.get('error')))
                    continue
                time.sleep(10)
                game.cmd('press', 'b', ok=False)
            game.cmd('time', 'set', hour, ok=False)
            game.cmd('weather', weather, ok=False)
            time.sleep(4)
            for tag, yaw, pitch in views:
                send('stick {} 0 0'.format(R), 'head 0 1.7 0 {} {} 0'.format(yaw, pitch))
                time.sleep(1.5)
                shot(tag)
                tags.append(tag)
            send('head 0 1.7 0 0 -10 0')
            if proc.poll() is not None:
                break
        # turning (first person), then third person orbiting
        send('stick {} 0.6 0'.format(R)); time.sleep(1.5)
        shot('turn_fp', 0.6); tags.append('turn_fp')
        send('stick {} 0 0'.format(R))
        game.cmd('set', 'vr_view', '1', ok=False); time.sleep(3)
        shot('third_still'); tags.append('third_still')
        send('stick {} 0.6 0'.format(R)); time.sleep(1.5)
        shot('third_orbit', 0.6); tags.append('third_orbit')
        send('stick {} 0 0'.format(R))
        game.cmd('set', 'vr_view', '0', ok=False); time.sleep(3)
        # the inventory and the map, opened and closed: the first frames back
        for btn in ('plus', 'minus'):
            game.cmd('press', btn, ok=False)
            t0 = time.monotonic()
            while time.monotonic() - t0 < 10 and game.cmd('vr', ok=False, timeout=30).get('world_view') is not False:
                time.sleep(0.1)
            time.sleep(1.5)
            game.cmd('press', btn, ok=False)
            # (the first frames with the world back: at 160-240 fps the first one or two after the press were still
            # the closing menu, its flat layer only - no eyes to compare, "no capture")
            t0 = time.monotonic()
            while time.monotonic() - t0 < 10 and game.cmd('vr', ok=False, timeout=30).get('world_view') is False:
                time.sleep(0.02)
            shot('after_' + btn, 0.6, 4)
            tags += ['after_%s_%d' % (btn, i) for i in range(4)]
            send('stick {} 0 0'.format(R)); time.sleep(1.5)
        alive = proc.poll() is None
    finally:
        common.stop(game, proc, log)
    res['alive'] = alive
    gl = open(os.path.join(run_dir, 'game.log'), encoding='utf-8', errors='replace').read()
    m = re.search(r'^\[vr\] rendering: (.*)$', gl, re.M)
    res['rendering'] = m.group(1) if m else '?'
    res['performance'] = (re.search(r'^\[vr\] performance: (\w+)', gl, re.M) or [None, '?'])[1]
    mx = re.findall(r'^\[vk-mv\] passes (\d+), multiview (\d+), mixed per-eye and single targets (\d+)', gl, re.M)
    res['mv_passes'] = mx[-1] if mx else None   # (multiview: passes, multiview ones, mixed ones drawn left eye only)
    fl = flog()
    fov = dict((t, [float(x) for x in f]) for t, *f in re.findall(r'capture (\S+) fov (\S+) (\S+) (\S+) (\S+)', fl))
    cap = os.path.join(run_dir, 'cap')
    caps = {}
    for tag in tags:
        lt = next((t for t in sorted(fov) if re.fullmatch(r'%s(_layer\d+)?_view0' % re.escape(tag), t)), None)
        if not lt:
            caps[tag] = None
            print('%-16s no capture' % tag)
            continue
        A = quick.grid(os.path.join(cap, lt + '.ppm'), fov[lt])
        B = quick.grid(os.path.join(cap, lt[:-1] + '1.ppm'), fov[lt[:-1] + '1'])
        dx, part = quick.eyes(A, B)
        cell, where, la, lb = detail(A, B)
        caps[tag] = {'diff': part, 'dx': dx, 'worst_cell': cell, 'where': where, 'mean_l': la, 'mean_r': lb}
        Image.fromarray(np.hstack([A, B, np.abs(A - B) * 4]).clip(0, 255).astype(np.uint8)).resize(
            (3 * 300, 300)).save(os.path.join(run_dir, 'd_%s.png' % tag))
        print('%-16s diff %5.2f%%  dx %2d  worst %5.1f%% at %-11s  brightness L %.1f R %.1f' % (
            tag, part, dx, cell, where, la, lb))
    res['caps'] = caps
    res['notes'] = notes
    bad = [] if alive else ['the game stopped']
    for tag, c in caps.items():
        if c is None:
            bad.append('%s: no capture' % tag)
        elif c['dx']:
            bad.append('%s: the eyes %d samples apart' % (tag, c['dx']))
        elif c['diff'] > a.limit:
            bad.append('%s: %.2f%% differs (limit %.2f%%), worst at %s' % (tag, c['diff'], a.limit, c['where']))
    if a.min_fps and res.get('hmd_fps', 0) < a.min_fps:
        bad.append('%.1f headset fps, under %.0f' % (res.get('hmd_fps', 0), a.min_fps))
    res['broken'] = bad
    json.dump(res, open(os.path.join(run_dir, 'result.json'), 'w'), indent=1)
    print('RESULT', json.dumps({k: v for k, v in res.items() if k != 'caps'}))
    for b in bad:
        print('  FAIL ' + b)
    print('PASS' if not bad else 'FAIL')
    sys.exit(0 if not bad else 1)


if __name__ == '__main__':
    main()
