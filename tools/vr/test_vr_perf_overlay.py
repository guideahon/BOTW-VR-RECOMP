#!/usr/bin/env python3
"""The performance overlay in the headset (docs/VR.md; BetterVR's "Show in both window and VR"), against fake_xr: in
play with Video > Performance overlay off, no headset frame has it; set to full (`set perf_overlay 2`) and to FPS only
(1), each frame has a quad in the head's space, 1 m ahead, up and to the right, 0.36 m wide, shaped as the panel
(fake_xr's quad log), and its picture is not empty (fake_xr's captures).

  python tools/vr/test_vr_perf_overlay.py --save <scratch save> [--out build/vr/perf_overlay]
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


def quads(path, start):
    with open(path, errors='replace') as f:
        f.seek(start)
        t = f.read()
    return [tuple(float(x) for x in m) for m in
            re.findall(r'quad \d+ space view at (\S+) (\S+) (\S+) size (\S+) (\S+)', t)], t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default='build/vr/perf_overlay')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    game, proc, log = re_run.boot(a.save, out, {'FAKE_XR_LOG_QUADS': '1', 'FAKE_XR_CAPTURE_EVERY': '60'},
                                  ['0 head 0 1.7 0 0 0 0'])
    fl = os.path.join(out, 'fake_xr.log')
    ok = True

    def check(c, what):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + what)
        ok &= bool(c)

    res = {}
    try:
        for name, v in (('off', '0'), ('full', '2'), ('fps', '1')):
            game.cmd('set', 'perf_overlay', v, ok=False)
            time.sleep(1)
            p0, m = T.vr_state(game).get('perf_frames', 0), os.path.getsize(fl)
            time.sleep(2)
            p1 = T.vr_state(game).get('perf_frames', 0)
            q, text = quads(fl, m)
            panel = [x for x in q if abs(x[3] - 0.36) < 0.001]
            lit = [float(x) for x in re.findall(r'capture \S+_layer(\d+)_view0 (\d+)x(\d+) mean (\S+)', text) and
                   [m3[3] for m3 in re.findall(r'capture \S+_layer(\d+)_view0 (\d+)x(\d+) mean (\S+)', text)
                    if int(m3[1]) < 600]]
            res[name] = (p1 - p0, panel, lit)
        game.cmd('set', 'perf_overlay', '0', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    for name, (frames, panel, lit) in res.items():
        print('    {}: {} frames, {} panel quads (e.g. {}), small captures mean {}'.format(name, frames, len(panel),
              panel[:1], lit[:3]))
    check(res['off'][0] == 0 and not res['off'][1], 'off: not in the headset')
    for name in ('full', 'fps'):
        frames, panel, lit = res[name]
        check(frames > 50 and len(panel) > 50, '{}: in each frame ({} frames)'.format(name, frames))
        check(panel and abs(panel[0][0] - 0.42) < 0.01 and abs(panel[0][1] - 2.0) < 0.01 and abs(panel[0][2] + 1.0) < 0.01,
              '{}: up and to the right, 1 m ahead'.format(name))
    check(res['full'][1] and res['fps'][1] and res['full'][1][0][4] > res['fps'][1][0][4],
          'the full panel taller than the FPS line')
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
