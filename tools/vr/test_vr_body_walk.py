#!/usr/bin/env python3
"""Stereo: Link's body the same in both eyes while he walks (2026-11-27: "extreme jitter when the character walks, the
whole body flickers"), against fake_xr with the eyes 0 m apart (FAKE_XR_IPD=0): both pictures must be one picture.

The head looks down at Link's body (--pitch), the left stick walks him forward (--stick), and both eyes are captured
several times while he walks (and once standing still). For each capture: the part of the lower picture (the body, the
feet) differing by more than --level grey levels, and where the right eye's lower picture matches the left's best (a
body drawn somewhere else in one eye shows as a shift). In Stereo each eye is a game frame half a step apart and both
are drawn from the left frame's place (vr_camera.c, the pair's anchor): Link, who moved on, must be drawn where he was.

  python tools/vr/test_vr_body_walk.py --save <scratch save> [--exe build/savagewind] [--mode stereo|multiview]
        [--out build/vr/body_walk] [--pitch -55] [--stick 1] [--shots 6] [--limit 12] [--env K=V ...]
Passes when every walking capture's differing part is under --limit percent and its shift is at most 1 step.
Measured 2026-11-27 (Great Plateau grass): each eye animating Link (SAVAGEWIND_VR_PAIR_POSE=0) 19-34% walking; the
left frame's pose in both eyes 2-9% (the grass in the wind and the light, which differ between the frames too; 1.6%
standing).
"""
import argparse
import os
import re
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import perf_routes  # noqa: E402
import test_vr_game as T  # noqa: E402

L = '/user/hand/left/input/thumbstick'


def lower(path, fov):
    a = np.array(T.read_ppm_angles(path, fov), dtype=np.int16)
    h = a.shape[0]
    return a[h // 2:, :]


def shift(a, b, m=12):
    h, w = a.shape
    best = None
    for dy in range(-m, m + 1):
        for dx in range(-m, m + 1):
            pa = a[m:h - m, m:w - m]
            pb = b[m + dy:h - m + dy, m + dx:w - m + dx]
            s = float(np.abs(pa - pb).mean())
            if best is None or s < best[0]:
                best = (s, dx, dy)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/body_walk')
    ap.add_argument('--mode', default='stereo', choices=('stereo', 'multiview'))
    ap.add_argument('--pitch', type=float, default=-55)
    ap.add_argument('--stick', type=float, default=1.0)
    ap.add_argument('--shots', type=int, default=6)
    ap.add_argument('--level', type=int, default=24)
    ap.add_argument('--limit', type=float, default=12.0)
    ap.add_argument('--env', nargs='*', default=[])
    a = ap.parse_args()
    T.EXE = a.exe
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    live = os.path.join(out, 'fake_xr_live.txt')
    for p in (live, live + '.ack'):
        if os.path.exists(p):
            os.unlink(p)
    env = {'SAVAGEWIND_VR_STEREO': '1', 'SAVAGEWIND_VR_RENDER': a.mode,
           'SAVAGEWIND_VR_EXPERIMENTAL': '1' if a.mode == 'multiview' else '0', 'FAKE_XR_IPD': '0',
           'FAKE_XR_LIVE': live, 'FAKE_XR_CAPTURE_EVERY': '0', 'SAVAGEWIND_VR_ROOMSCALE': '0'}
    for kv in a.env:
        k, v = kv.split('=', 1)
        env[k] = v
    head = 'head 0 1.7 0 0 {} 0'.format(a.pitch)
    game, proc, log = re_run.boot(a.save, out, env, ['0 ' + head])
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

    shots = []
    try:
        send(head)
        time.sleep(3)
        send(head, 'shot still')
        shots.append('still')
        time.sleep(2)
        send(head, 'stick {} 0 {}'.format(L, a.stick))
        time.sleep(2)
        for k in range(a.shots):
            send(head, 'stick {} 0 {}'.format(L, a.stick), 'shot walk{}'.format(k))
            shots.append('walk{}'.format(k))
            time.sleep(1.2)
        send(head, 'stick {} 0 0'.format(L))
        time.sleep(0.5)
    finally:
        perf_routes.stop_game(game, proc, log)
    fov = dict((t, [float(x) for x in f])
               for t, *f in re.findall(r'capture (\S+) fov (\S+) (\S+) (\S+) (\S+)', T.fake_log(out)))
    ok = True
    for s in shots:
        lt = next((t for t in sorted(fov) if re.fullmatch(re.escape(s) + r'_layer\d+_view0', t)), None)
        if not lt:
            print('{:6}: no capture'.format(s))
            ok = False
            continue
        rt = lt[:-1] + '1'
        pa = lower(os.path.join(out, 'cap', lt + '.ppm'), fov[lt])
        pb = lower(os.path.join(out, 'cap', rt + '.ppm'), fov[rt])
        part = 100.0 * (np.abs(pa - pb) > a.level).mean()
        sad, dx, dy = shift(pa, pb)
        bad = s != 'still' and (part > a.limit or abs(dx) > 1 or abs(dy) > 1)
        ok &= not bad
        print('{:6}: lower picture differing {:5.2f}%  best shift dx {:+d} dy {:+d} (steps of 0.005)  {}'.format(
            s, part, dx, dy, 'BAD' if bad else 'ok'))
    for f in os.listdir(os.path.join(out, 'cap')) if os.path.isdir(os.path.join(out, 'cap')) else []:
        if f.endswith(('.ppm', '.pgm')) and not os.environ.get('KEEP_CAPTURES'):
            os.unlink(os.path.join(out, 'cap', f))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
