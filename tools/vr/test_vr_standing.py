#!/usr/bin/env python3
"""Play position (Options > VR, docs/VR.md: BetterVR's Seated / Standing), against fake_xr with LOCAL's origin 1.20 m
above the floor (FAKE_XR_LOCAL_HEIGHT: a player whose eyes are 1.20 m up, seated, at the start) and the head still
there. Seated (the default): the drawn eye is Link's eye height (1.55 m) above his feet. `set vr_play_position 1`
(Standing): it is the player's own, 1.20 m. Each read from `camera` (the drawn view) less `pos` (Link's feet), a few
times, the world still.

  python tools/vr/test_vr_standing.py --save <scratch save> [--out build/vr/standing]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402

REAL = 1.20


def eye_above_feet(game):
    vals = []
    for _ in range(5):
        cam = game.cmd('camera', ok=False)
        pos = game.cmd('pos', ok=False)
        if 'y' in pos and 'eye.y' in cam:
            vals.append(cam['eye.y'] - pos['y'])
        elif 'y' in pos and 'y' in cam:
            vals.append(cam['y'] - pos['y'])
        time.sleep(0.3)
    vals.sort()
    return vals[len(vals) // 2] if vals else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/standing')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    targs = argparse.Namespace(save=a.save, out=out, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                               runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792,
                               place='plateau', boot=180.0, settle=8.0, boot_estimate=60.0, capture_every=0)
    os.makedirs(out, exist_ok=True)
    env = {'SAVAGEWIND_VR_STEREO': '0', 'SAVAGEWIND_VR_ROOMSCALE': '0', 'FAKE_XR_LOCAL_HEIGHT': str(REAL)}
    game, proc, log = T.launch(targs, out, ['0 head 0 0 0 0 0 0'], frame_rate=0, extra_env=env)
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    try:
        T.ready(targs, game, proc)
        print('    camera reply:', game.cmd('camera', ok=False))
        seated = eye_above_feet(game)
        s_used = T.vr_state(game).get('eye_height')
        print(game.cmd('set', 'vr_play_position', '1', ok=False))
        time.sleep(1)
        standing = eye_above_feet(game)
        t_used = T.vr_state(game).get('eye_height')
        game.cmd('set', 'vr_play_position', '0', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    print('    eye above Link\'s feet: seated {} (used {}), standing {} (used {})'.format(seated, s_used, standing, t_used))
    check(seated is not None and abs(seated - 1.55) < 0.08, 'Seated: Link\'s eye height ({})'.format(seated))
    check(standing is not None and abs(standing - REAL) < 0.08, 'Standing: the player\'s own, {} m ({})'.format(REAL, standing))
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
