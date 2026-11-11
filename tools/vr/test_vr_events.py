#!/usr/bin/env python3
"""Events (cutscenes) in VR (docs/VR.md), against fake_xr: a new game from an empty save, whose opening is the event
Demo169_0 (BetterVR's table: on the event's camera), once per SAVAGEWIND_VR_EVENTS mode. While the event plays:
  auto    the headset gets the eyes' 3D view (a projection layer), not in first person
  first   the eyes' 3D view, in first person (the eye in Link's head)
  screen  the virtual screen only (a quad layer, no projection)
  bars    auto with SAVAGEWIND_VR_CUTSCENE_BARS=1: this event, on its own camera, on the virtual screen

  python tools/vr/test_vr_events.py [--modes auto first screen] [--out build/vr/events]
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402

NEW_GAME = '150:A:2,170:A:2,190:A:2,210:A:2,260:A:2,300:A:2'   # (30ths of a second) the title, New Game, its notice


def run(mode, out):
    os.makedirs(out, exist_ok=True)
    empty = os.path.join(out, 'empty_save')
    shutil.rmtree(os.path.join(out, 'save'), ignore_errors=True)
    os.makedirs(empty, exist_ok=True)
    args = argparse.Namespace(save=empty, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                              runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792, place='',
                              capture_every=0)
    env = {'SAVAGEWIND_VR_EVENTS': 'auto' if mode == 'bars' else mode, 'SAVAGEWIND_INPUT': NEW_GAME,
           'SAVAGEWIND_VR_STEREO': '0'}
    if mode == 'bars':
        env['SAVAGEWIND_VR_CUTSCENE_BARS'] = '1'
    game, proc, log = T.launch(args, out, ['0 head 0 1.7 0 0 0 0'], frame_rate=0, extra_env=env)
    seen = []
    try:
        end = time.monotonic() + 420
        while time.monotonic() < end:
            if proc.poll() is not None:
                break
            try:
                st = game.cmd('vr', ok=False)
            except OSError:
                game.close()
                game.conn = perf_routes.swctl.connect('127.0.0.1', game.port, 5, 30)
                continue
            ev = st.get('event', '')
            if ev:
                pos = game.cmd('pos', ok=False)
                seen.append((ev, st.get('world_view'), st.get('first_person'), st.get('frames', 0), 'error' not in pos))
                if len(seen) >= 40:
                    break
            time.sleep(0.25)
    finally:
        perf_routes.stop_game(game, proc, log)
    # the headset's layers in the frames of the event's samples (fake_xr's log: "frame <n> ... layers <k> <kinds>")
    frames = set(s[3] for s in seen[5:])
    kinds = {}
    for line in open(os.path.join(out, 'fake_xr.log'), errors='replace'):
        p = line.split()
        if len(p) >= 6 and p[0] == 'frame' and p[4] == 'layers':
            n, kind = int(p[1]), p[6] if len(p) > 6 else ''
            if any(abs(n - f) < 3 for f in frames):
                kinds[kind] = kinds.get(kind, 0) + 1
    return seen, kinds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modes', nargs='+', default=['auto', 'first', 'screen', 'bars'])
    ap.add_argument('--out', default='build/vr/events')
    a = ap.parse_args()
    ok = True
    for mode in a.modes:
        seen, kinds = run(mode, os.path.join(a.out, mode))
        names = sorted(set(s[0] for s in seen))
        later = seen[5:]
        wv = sorted(set(s[1] for s in later))
        fp = sorted(set(s[2] for s in later))
        link = sorted(set(s[4] for s in later))
        proj = sum(v for k, v in kinds.items() if 'projection' in k)
        quad_only = sum(v for k, v in kinds.items() if 'projection' not in k)
        print('{:7}: events {} | world view {} | first person {} | Link placed {} | headset frames with the 3D eyes {}, '
              'screen only {}'.format(mode, names, wv, fp, link, proj, quad_only))
        if not later:
            ok = False
            continue
        if mode == 'auto':
            ok &= wv == [True] and fp == [False] and proj > 0 and quad_only == 0
        elif mode == 'first':
            # (in first person when Link is in the world; before that, as in the opening, on the event's camera)
            ok &= wv == [True] and proj > 0 and quad_only == 0 and all(s[2] == s[4] for s in later)
        elif mode in ('screen', 'bars'):
            ok &= wv == [False] and proj == 0 and quad_only > 0
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
