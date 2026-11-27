#!/usr/bin/env python3
"""Stereo: both eyes of a pair drawn from one place while the view turns (vr_camera.c, the pair's anchor), against
fake_xr. The eyes are drawn in two game frames half a step apart; the right one used to take its own frame's anchor,
so with the turn stick held its picture was turned against the left one's (one eye "desynchronised", 2026-11-24).

With the eyes 0 m apart (FAKE_XR_IPD=0) both pictures, placed by the fields they were submitted with, must be the same
picture: still, and with the right stick held (a smooth turn). --compare runs the same with SAVAGEWIND_VR_PAIR_ANCHOR=0
too (each eye its own anchor), where the turn shows as a sideways shift between the eyes.

--walk: walking sideways (the left stick) with the hands held ahead, the hands' part of both pictures compared (a
tester on 25.7: the hands "don't keep up and desynchronize with the eyes" when moving).

--view third: the same in third person, the right stick orbiting the game's camera (a tester on 25.7: "when switched
to third person in stereo mode the camera rotation is broken").

--mode compat (SAVAGEWIND_VR_RENDER=stereo, the default here), multiview (SAVAGEWIND_VR_RENDER=multiview SAVAGEWIND_VR_EXPERIMENTAL=1) or sync
(SAVAGEWIND_VR_SYNC=1, the prototype) runs the same checks on those
paths: one game frame a pair there, so "right frames drawn with their left frame's anchor" (normal stereo's right game
frame) does not apply and is only reported; the view against the hands' anchor and the pictures are judged as always.
The log is checked for the mode asked for (a driver without multiview falls back to normal stereo: --mode multiview
fails then, as it should).

  python tools/vr/test_vr_stereo_sync.py --save <scratch save> [--exe build/savagewind] [--out build/vr/stereo_sync]
        [--mode compat|multiview|sync]
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import re_run  # noqa: E402
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402

R = '/user/hand/right/input/thumbstick'
L = '/user/hand/left/input/thumbstick'
HANDS = ['0 head 0 1.7 0 0 0 0', '0 hand right 0.12 1.58 -0.40 0 0 0', '0 hand left -0.12 1.58 -0.40 0 0 0']  # (12 cm under the eyes: in the measured field, tangents to 0.6; 25 cm under they were out of it)


def hands_shift(a, b, max_dx=12):
    """Where the right picture's lower middle (the hands held ahead) matches the left's best: (dx, at it, at 0)"""
    h, w = len(a), len(a[0])
    y0, y1, x0, x1 = int(h * 0.62), int(h * 0.95), int(w * 0.3), int(w * 0.7)
    def sad(dx):
        t, n = 0, 0
        for y in range(y0, y1, 2):
            for x in range(x0, x1, 2):
                t += abs(a[y][x] - b[y][x + dx])
                n += 1
        return t / n
    sc = {dx: sad(dx) for dx in range(-max_dx, max_dx + 1)}
    dx = min(sc, key=sc.get)
    return dx, sc[dx], sc[0]
MODES = {'compat': {'SAVAGEWIND_VR_RENDER': 'stereo'}, 'multiview': {'SAVAGEWIND_VR_RENDER': 'multiview', 'SAVAGEWIND_VR_EXPERIMENTAL': '1'},
         'sync': {'SAVAGEWIND_VR_SYNC': '1'}}
MODE_LOG = {'compat': None, 'multiview': '[vk-mv] multiview: both eyes', 'sync': '[vr] synchronous stereo'}


def run(save, out, extra_env, script=None, stick=True, walk=False):
    live = os.path.join(out, 'fake_xr_live.txt')
    for path in (live, live + '.ack'):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
    env = {'FAKE_XR_LIVE': live, 'FAKE_XR_IPD': '0', 'FAKE_XR_CAPTURE_EVERY': '0', 'SAVAGEWIND_VR_STEREO': '1',
           'SAVAGEWIND_VR_LOG_CAMERA': '1'}
    env.update(extra_env)
    game, proc, log = re_run.boot(save, out, env, script or ['0 head 0 1.7 0 0 0 0'])
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
                        return True
            except (OSError, ValueError, IndexError):
                pass
            time.sleep(0.03)
        return False

    shots = []
    try:
        v0 = T.vr_state(game)
        send('stick {} 0 0'.format(R), 'shot still')
        time.sleep(2)
        shots.append('still')
        x = 1 if stick else 0   # (a smooth turn held, or the head turning by the script)
        tag = 'walk' if walk else 'turn' if stick else 'head'
        S = L if walk else R    # (walking: the left stick sideways, a strafe)
        send('stick {} {} 0'.format(S, x))
        time.sleep(1.5)
        for k in range(3):
            send('stick {} {} 0'.format(S, x), 'shot {}{}'.format(tag, k))
            time.sleep(1.5)
            shots.append('{}{}'.format(tag, k))
        v1 = T.vr_state(game)
        send('stick {} 0 0'.format(S))
        time.sleep(1)
    finally:
        perf_routes.stop_game(game, proc, log)
    fov = dict((t, [float(x) for x in f])
               for t, *f in re.findall(r'capture (\S+) fov (\S+) (\S+) (\S+) (\S+)', T.fake_log(out)))
    res = {}
    for s in shots:
        lt = next((t for t in sorted(fov) if re.fullmatch(re.escape(s) + r'_layer\d+_view0', t)), s + '_view0')
        rt = lt[:-1] + '1'
        lp, rp = os.path.join(out, 'cap', lt + '.ppm'), os.path.join(out, 'cap', rt + '.ppm')
        if not (os.path.exists(lp) and os.path.exists(rp) and lt in fov and rt in fov):
            res[s] = None
            continue
        a, b = T.read_ppm_angles(lp, fov[lt]), T.read_ppm_angles(rp, fov[rt])
        res[s] = hands_shift(a, b) if walk else T.best_shift(a, b)
    return res, v0, v1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--exe', default=T.EXE)
    ap.add_argument('--out', default='build/vr/stereo_sync')
    ap.add_argument('--compare', action='store_true', help='also run with SAVAGEWIND_VR_PAIR_ANCHOR=0')
    ap.add_argument('--mode', choices=sorted(MODES), default='compat')
    ap.add_argument('--walk', action='store_true',
                    help='only: walking sideways with the hands held ahead, the hands compared between the eyes')
    ap.add_argument('--view', choices=['first', 'third'], default='first',
                    help="third: the game's camera behind Link (the right stick orbits it), as a tester's 25.7 report")
    a = ap.parse_args()
    mode_env = dict(MODES[a.mode])
    if a.view == 'third':
        mode_env['SAVAGEWIND_VR_VIEW'] = 'third'
    T.EXE = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    def show(res):
        for s, r in res.items():
            print('    {:6} {}'.format(s, 'no capture' if r is None else
                                     'right against left: dx {} dy {} (0.005-tangent samples), difference {:.1f} '
                                     'there, {:.1f} unshifted'.format(*r)))

    if a.walk:   # (a tester on 25.7: the hands "don't keep up and desynchronize with the eyes" when moving)
        print('== walking sideways, the hands held ahead: the hands in both eyes (eyes 0 m apart)')
        res, _, _ = run(a.save, os.path.join(out, 'walk'), dict(mode_env), script=HANDS, walk=True)
        for s, r in res.items():
            print('    {:6} {}'.format(s, 'no capture' if r is None else
                                     'hands: right against left dx {} (0.005-tangent samples), difference {:.1f} there, '
                                     '{:.1f} unshifted'.format(*r)))
            if s != 'still':
                check(r is not None and abs(r[0]) <= 1, '{}: the hands at one place in both eyes (dx {})'.format(
                    s, r and r[0]))
        print('PASS' if ok else 'FAIL')
        sys.exit(0 if ok else 1)
    print('== the pair anchor (default)')
    print('   mode: {} {}'.format(a.mode, ' '.join('{}={}'.format(k, v) for k, v in mode_env.items())))
    res, v0, v1 = run(a.save, os.path.join(out, 'pair'), dict(mode_env))
    show(res)
    # the view's heading at each camera update (SAVAGEWIND_VR_LOG_CAMERA=1): with the stick held it must turn in small
    # steps, never stand still and then jump (multiview did: 0.35 rad at a time, 2026-11-25). Steps over 90 degrees are
    # the game's other cameras (not the world's), left out
    yaws = [float(y) for y in re.findall(r'^\[vr-cam\] .*view yaw (\S+)', open(
        os.path.join(out, 'pair', 'game.log'), encoding='utf-8', errors='replace').read(), re.M)]
    steps = [abs((b - a + 180) % 360 - 180) for a, b in zip(yaws, yaws[1:])]
    steps = [s for s in steps if s <= 90]
    moving = [s for s in steps if s > 0.01]
    check(len(moving) > 50 and max(moving) < 5,
          'turning with the stick, the view turns in small steps: {} steps, the largest {:.1f} deg'.format(
              len(moving), max(moving) if moving else 0))
    want = MODE_LOG[a.mode]
    log_text = open(
        os.path.join(out, 'pair', 'game.log'), encoding='utf-8', errors='replace').read()
    if want:
        check(want in log_text, 'the run is in the mode asked for ({!r} in the log)'.format(want))
    else:
        check('[vr] synchronous stereo' not in log_text and '[vk-mv] multiview: both eyes' not in log_text,
              'the run is in normal stereo (no synchronous stereo or multiview in the log)')
    turned = v1.get('pair_anchors', 0) - v0.get('pair_anchors', 0)
    pairs = v1.get('stereo_pairs', 0) - v0.get('stereo_pairs', 0)
    what = 'right frames drawn with their left frame\'s anchor: {} of {} pairs'.format(turned, pairs)
    if a.mode == 'compat':
        check(pairs > 50 and turned >= pairs * 0.9, what)
    else:
        print('  --   ' + what + ' (normal stereo only: not judged in ' + a.mode + ')')
    af = v1.get('anchor_frames', 0) - v0.get('anchor_frames', 0)
    am = v1.get('anchor_mismatch', 0) - v0.get('anchor_mismatch', 0)
    if a.view == 'first':   # (counted in first person only: the hands' anchor)
        check(af > 100 and am <= af * 0.02,
              "turning smoothly, each eye's view turned as its hands were placed: {} of {} frames apart".format(am, af))
    for s, r in res.items():
        check(r is not None, '{}: both eyes captured'.format(s))
        if r:
            check(r[0] == 0 and r[1] == 0, '{}: eyes 0 m apart see one picture (shift {} {})'.format(s, r[0], r[1]))
    print('== the head turning fast (180 deg/s), the pair located once')
    resh, _, _ = run(a.save, os.path.join(out, 'head'), dict(mode_env, FAKE_XR_HEAD_SPIN='180'), stick=False)
    show(resh)
    for s, r in resh.items():
        if s == 'still':
            continue
        check(r is not None and r[0] == 0 and r[1] == 0,
              '{}: turning the head, eyes 0 m apart see one picture (shift {})'.format(s, r and r[:2]))
    if a.compare:
        print('== the head turning, each eye located in its own frame (SAVAGEWIND_VR_PAIR_ANCHOR=0)')
        resh0, _, _ = run(a.save, os.path.join(out, 'head_own'),
                          dict(mode_env, SAVAGEWIND_VR_PAIR_ANCHOR='0', FAKE_XR_HEAD_SPIN='180'), stick=False)
        show(resh0)
    if a.compare:
        print('== each eye its own anchor (SAVAGEWIND_VR_PAIR_ANCHOR=0, for comparison)')
        res0, w0, w1 = run(a.save, os.path.join(out, 'own'), dict(mode_env, SAVAGEWIND_VR_PAIR_ANCHOR='0'))
        show(res0)
        print('    view against the hands\' anchor: {} of {} frames apart'.format(
            w1.get('anchor_mismatch', 0) - w0.get('anchor_mismatch', 0), w1.get('anchor_frames', 0) - w0.get('anchor_frames', 0)))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
