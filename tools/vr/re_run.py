#!/usr/bin/env python3
"""(reverse engineering) Boots the game into play (as tools/vr/test_vr_game.py, against fake_xr), then runs control
commands and prints the replies. In a command, {x} {y} {z} are Link's position; `sleep <s>` waits.

  python tools/vr/re_run.py --save <scratch save> "mem find mtx:{x},{y+1},{z},2 300" ...
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_vr_game as T  # noqa: E402
import perf_routes  # noqa: E402


def boot(save, out, env=None, script=None):
    targs = argparse.Namespace(save=save, out=out, exe=os.path.join(T.TREE, 'build', 'savagewind.exe'),
                               runtime_json=os.path.join(T.TREE, 'build', 'vr', 'fake_xr.json'), port=7792,
                               place='plateau', boot=180.0, settle=8.0, boot_estimate=60.0, capture_every=0,
                               menu_rounds=0)
    os.makedirs(out, exist_ok=True)
    e = {'SAVAGEWIND_VR_STEREO': '0'}
    e.update(env or {})
    game, proc, log = T.launch(targs, out, script or ['0 head 0 1.7 0 0 0 0'], frame_rate=0, extra_env=e)
    T.ready(targs, game, proc)
    return game, proc, log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/re')
    ap.add_argument('--env', action='append', default=[])
    ap.add_argument('cmds', nargs='+')
    a = ap.parse_args()
    game, proc, log = boot(a.save, a.out, dict(kv.split('=', 1) for kv in a.env))
    try:
        pos = game.cmd('pos', ok=False)
        x, y, z = pos.get('x', 0), pos.get('y', 0), pos.get('z', 0)
        print('Link at {:.2f} {:.2f} {:.2f}'.format(x, y, z))
        for c in a.cmds:
            c = c.format(x=x, y=y, z=z, **{'y+1': y + 1, 'y+1.5': y + 1.5})
            words = c.split()
            if words[0] == 'sleep':
                time.sleep(float(words[1]))
                continue
            print('\n$ ' + c)
            d = game.cmd(*words, ok=False, timeout=300)
            print(json.dumps(d, indent=None)[:6000])
    finally:
        perf_routes.stop_game(game, proc, log)


if __name__ == '__main__':
    main()
