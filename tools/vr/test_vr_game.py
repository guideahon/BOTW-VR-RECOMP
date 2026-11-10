#!/usr/bin/env python3
"""The game itself in VR against fake_xr (docs/VR.md "Tests", level T3): boots a `make VR=1` build with your own game
files and a scratch save, waits for the world, and checks what the headset got.

Needs your game files (extracted/), so it never runs in public CI. Standard library only.

  python tools/vr/test_vr_game.py --save <scratch save folder with save_0> --out <dir> [--scenario ...]

Scenarios (each one boot):
  cinema   the flat game on the virtual screen: the session reaches FOCUSED, every frame is ended, the captured
           headset image is the game's (lit, close to the window's own screenshot)
  lost     the runtime loses the session in play: VR stops, the game goes on flat
  exit     the runtime asks the app to stop (as a dashboard's Quit does): the session ends cleanly, the game goes on
"""
import argparse
import os
import struct
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(TREE, 'tools'))
import perf_routes  # noqa: E402  (Game, wait_world, stop_game, CONTINUE)
import swctl  # noqa: E402

failures = []


def check(cond, what):
    print('  {} {}'.format('ok  ' if cond else 'FAIL', what))
    if not cond:
        failures.append(what)


def read_ppm_mean(path):
    data = open(path, 'rb').read()
    head = data.split(b'\n', 3)
    w, h = map(int, head[1].split())
    px = head[3]
    n = w * h
    step = max(1, n // 20000) * 3  # (a sample is enough for a mean)
    s = [0, 0, 0]
    k = 0
    for i in range(0, n * 3, step):
        s[0] += px[i]; s[1] += px[i + 1]; s[2] += px[i + 2]
        k += 1
    return [v / k for v in s]


def read_png_mean(path):
    """Mean RGB of an 8-bit RGB/RGBA PNG (the game's `shot`), decoded with zlib only."""
    data = open(path, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        ln, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    s, k = [0, 0, 0], 0
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):  # (PNG filters: none, sub, up, average, paeth)
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        if y % 8 == 0:
            for x in range(0, w, 8):
                s[0] += line[x * bpp]; s[1] += line[x * bpp + 1]; s[2] += line[x * bpp + 2]
                k += 1
        prev = line
    return [v / k for v in s]


def launch(args, run_dir, script_lines, frame_rate=60):
    os.makedirs(os.path.join(run_dir, 'cap'), exist_ok=True)
    save = os.path.join(run_dir, 'save')
    if not os.path.isdir(save):
        import shutil
        shutil.copytree(args.save, save)
    ini = os.path.join(run_dir, 'settings.ini')
    with open(ini, 'w') as f:
        f.write('window_size = 1280x720\nframe_rate = {}\nvsync = off\nperf_overlay = off\nframe_generation = 0\n'
                'fullscreen = 0\nantialias = taa\n'.format(frame_rate))  # (TAA: `camera` reads the drawn view)
    script = os.path.join(run_dir, 'fake_xr_script.txt')
    with open(script, 'w') as f:
        f.write('\n'.join(script_lines) + '\n')
    env = dict(os.environ,
               SAVAGEWIND_VR='1', XR_RUNTIME_JSON=os.path.abspath(args.runtime_json),
               FAKE_XR_LOG=os.path.join(run_dir, 'fake_xr.log'), FAKE_XR_CAPTURE=os.path.join(run_dir, 'cap'),
               FAKE_XR_CAPTURE_EVERY=str(args.capture_every), FAKE_XR_SCRIPT=script,
               SAVAGEWIND_NO_LAUNCHER='1', SAVAGEWIND_HIDDEN='1', SAVAGEWIND_SAVE_DIR=save,
               SAVAGEWIND_SETTINGS=ini, SAVAGEWIND_CONTROL=str(args.port), SAVAGEWIND_SCRIPT_CLOCK='1',
               SAVAGEWIND_INPUT=perf_routes.CONTINUE, SAVAGEWIND_TEST_WARP=args.place,
               SAVAGEWIND_TEST_WEATHER='0')
    msys = r'C:\msys64\ucrt64\bin'  # (Windows: a source build's DLLs; docs/BUILD-windows.md)
    if os.name == 'nt' and os.path.isdir(msys) and msys.lower() not in env.get('PATH', '').lower():
        env['PATH'] = msys + os.pathsep + env.get('PATH', '')
    log = open(os.path.join(run_dir, 'game.log'), 'wb')
    proc = subprocess.Popen([args.exe, 'extracted'], cwd=TREE, env=env, stdout=log, stderr=subprocess.STDOUT)
    game = perf_routes.Game(args.port)
    return game, proc, log


def vr_state(game):
    return game.cmd('vr', ok=False)


def fake_log(run_dir):
    p = os.path.join(run_dir, 'fake_xr.log')
    return open(p, encoding='utf-8', errors='replace').read() if os.path.exists(p) else ''


def scenario_cinema(args, run_dir):
    game, proc, log = launch(args, run_dir, ['# nothing scripted: a still head'])
    try:
        perf_routes.wait_world(game, proc, args.boot)
        time.sleep(args.settle)
        v1 = vr_state(game)
        time.sleep(3)
        v2 = vr_state(game)
        # (`shot` writes only inside the game's folder: build/vr/, then moved next to the run)
        os.makedirs(os.path.join(TREE, 'build', 'vr'), exist_ok=True)
        game.cmd('shot', 'build/vr/test_vr_game_window.png', ok=False)
        time.sleep(2)
        src = os.path.join(TREE, 'build', 'vr', 'test_vr_game_window.png')
        if os.path.exists(src):
            os.replace(src, os.path.join(run_dir, 'window.png'))
        check(v2.get('enabled') is True, 'vr enabled ({})'.format(v2.get('status')))
        check(v2.get('session') == 'focused', 'session focused (got {})'.format(v2.get('session')))
        check(v2.get('rendered', 0) > v1.get('rendered', 0) + 60, 'frames with an image keep coming ({} -> {})'
              .format(v1.get('rendered'), v2.get('rendered')))
    finally:
        perf_routes.stop_game(game, proc, log)
    fl = fake_log(run_dir)
    caps = sorted(f for f in os.listdir(os.path.join(run_dir, 'cap')) if f.endswith('.ppm'))
    check(len(caps) >= 3, '{} headset captures'.format(len(caps)))
    import re
    glog = open(os.path.join(run_dir, 'game.log'), errors='replace').read()
    m = re.search(r'\[vr\] headset eye images \d+x\d+: window (\d+)x(\d+)', glog)
    check(m is not None and 'swapchain {}x{}'.format(*m.groups()) in fl,
          "swapchain at the window size, in the headset's shape ({})".format(m.groups() if m else None))
    check(' frame discarded' not in fl, 'no frame discarded')
    if caps:
        last = os.path.join(run_dir, 'cap', caps[-1])
        m = read_ppm_mean(last)
        check(sum(m) / 3 > 20, 'last headset image is lit (mean {:.0f} {:.0f} {:.0f})'.format(*m))
        win = os.path.join(run_dir, 'window.png')
        if os.path.exists(win):
            w = read_png_mean(win)
            diff = max(abs(a - b) for a, b in zip(m, w))
            check(diff < 30, 'headset image close to the window\'s (mean diff {:.1f}: gamma and colours kept)'
                  .format(diff))
        else:
            check(False, 'window screenshot taken')


def scenario_stop(args, run_dir, event, want_status):
    # the event comes well after the world is up: boot + settle seconds of fake_xr time at 90 Hz
    at = args.boot_estimate
    game, proc, log = launch(args, run_dir, ['{} {}'.format(at, event)])
    try:
        perf_routes.wait_world(game, proc, args.boot)
        deadline = time.monotonic() + args.boot
        v = vr_state(game)
        while v.get('enabled') and time.monotonic() < deadline:
            time.sleep(2)
            v = vr_state(game)
        check(v.get('enabled') is False, 'VR stopped after "{}" ({})'.format(event, v.get('status')))
        check(want_status in str(v.get('status')), 'status says why: "{}"'.format(v.get('status')))
        s1 = game.cmd('state', ok=False)
        time.sleep(3)
        s2 = game.cmd('state', ok=False)
        check(proc.poll() is None, 'the game is still running')
        check(s2.get('frame', 0) > s1.get('frame', 0) + 60, 'the game goes on flat ({} -> {} frames)'
              .format(s1.get('frame'), s2.get('frame')))
    finally:
        perf_routes.stop_game(game, proc, log)
    fl = fake_log(run_dir)
    if event == 'exit':
        check('session ended' in fl, 'the game called xrEndSession')
        check('state XR_SESSION_STATE_EXITING' in fl, 'the session reached EXITING')


HEAD_PHASES = [  # (fake_xr seconds per phase; the head in STAGE space, metres and degrees)
    ('neutral', (0.0, 1.7, 0.0, 0.0, 0.0, 0.0)),
    ('yaw30', (0.0, 1.7, 0.0, 30.0, 0.0, 0.0)),
    ('pitch20', (0.0, 1.7, 0.0, 0.0, 20.0, 0.0)),
    ('right50cm', (0.5, 1.7, 0.0, 0.0, 0.0, 0.0)),
]
PHASE_S = 12.0


def camera(game):
    d = game.cmd('camera', ok=False)
    return d if 'yaw' in d else None


def angle_diff(a, b):
    return (a - b + 180.0) % 360.0 - 180.0


def scenario_headtrack(args, run_dir):
    """The head moves the camera: turn, look up, step right; each read from the game's own `camera`."""
    lines = []
    cycle = PHASE_S * len(HEAD_PHASES)
    for k in range(400):  # (a long time: the phases repeat)
        for i, (_, (x, y, z, yaw, pitch, roll)) in enumerate(HEAD_PHASES):
            lines.append('{:.2f} head {} {} {} {} {} {}'.format(k * cycle + i * PHASE_S, x, y, z, yaw, pitch, roll))
    game, proc, log = launch(args, run_dir, lines, frame_rate=0)
    try:
        perf_routes.wait_world(game, proc, args.boot)
        time.sleep(args.settle)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_phase(name):  # the middle of the next phase of that name
            i = [p[0] for p in HEAD_PHASES].index(name)
            t = now_s()
            start = (t // cycle) * cycle + i * PHASE_S
            if start + PHASE_S * 0.4 < t:
                start += cycle
            while now_s() < start + PHASE_S * 0.5:
                time.sleep(0.2)

        v = vr_state(game)
        check(v.get('world_view') is True, 'in play the headset gets the world view')
        wait_phase('neutral')
        base = camera(game)
        wait_phase('yaw30')
        turned = camera(game)
        wait_phase('pitch20')
        up = camera(game)
        wait_phase('right50cm')
        moved = camera(game)
        wait_phase('neutral')
        back = camera(game)
        v = vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    if not all((base, turned, up, moved, back)):
        check(False, 'the camera could be read in every phase')
        return
    print('    camera yaw/pitch: neutral {:.1f}/{:.1f}  yaw30 {:.1f}/{:.1f}  pitch20 {:.1f}/{:.1f}  back {:.1f}/{:.1f}'
          .format(base['yaw'], base['pitch'], turned['yaw'], turned['pitch'], up['yaw'], up['pitch'], back['yaw'],
                  back['pitch']))
    check(abs(angle_diff(turned['yaw'], base['yaw']) - 30) < 2, 'turning the head 30 deg left turns the view 30 deg '
          '({:.1f})'.format(angle_diff(turned['yaw'], base['yaw'])))
    check(abs(up['pitch'] - 20) < 2, 'looking 20 deg up: the view\'s pitch is the head\'s ({:.1f})'.format(up['pitch']))
    check(abs(base['pitch']) < 2, 'a level head: a level view ({:.1f}; the game\'s own pitch is left out)'
          .format(base['pitch']))
    import math
    yaw = math.radians(base['yaw'])
    right = (-math.cos(yaw), 0.0, math.sin(yaw))  # (the view's right: forward x up, forward (sin yaw, 0, cos yaw))
    d = [moved[k] - base[k] for k in ('x', 'y', 'z')]
    along = sum(a * b for a, b in zip(d, right))
    check(abs(along - 0.5) < 0.1, 'a 50 cm step right moves the eye 50 cm right ({:.2f} m; moved {:.2f} {:.2f} {:.2f})'
          .format(along, *d))
    check(abs(angle_diff(back['yaw'], base['yaw'])) < 2, 'back to neutral, the view comes back (no drift: {:.1f})'
          .format(angle_diff(back['yaw'], base['yaw'])))
    check(v.get('world_frames', 0) > 100, '{} frames sent as the head\'s view'.format(v.get('world_frames')))
    fl = fake_log(run_dir)
    check('layers 1 projection' in fl, 'the headset gets a projection layer')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--save', required=True, help='scratch save folder (holds save_0); copied, never written')
    ap.add_argument('--out', required=True)
    ap.add_argument('--scenario', nargs='+', default=['cinema', 'headtrack', 'lost', 'exit'])
    ap.add_argument('--exe', default=os.path.join(TREE, 'build', 'savagewind.exe' if os.name == 'nt' else 'savagewind'))
    ap.add_argument('--runtime-json', default=os.path.join(TREE, 'build', 'vr', 'fake_xr.json'))
    ap.add_argument('--port', type=int, default=7792)
    ap.add_argument('--place', default='plateau')
    ap.add_argument('--boot', type=float, default=180)
    ap.add_argument('--settle', type=float, default=8)
    ap.add_argument('--boot-estimate', type=float, default=60,
                    help='fake_xr seconds (frames / 90) at which the lost / exit events come')
    ap.add_argument('--capture-every', type=int, default=240)
    args = ap.parse_args(argv)
    for sc in args.scenario:
        run_dir = os.path.join(args.out, sc)
        os.makedirs(run_dir, exist_ok=True)
        print('== ' + sc)
        if sc == 'cinema':
            scenario_cinema(args, run_dir)
        elif sc == 'lost':
            scenario_stop(args, run_dir, 'lose', 'lost')
        elif sc == 'exit':
            scenario_stop(args, run_dir, 'exit', 'ended the session')
        elif sc == 'headtrack':
            scenario_headtrack(args, run_dir)
        else:
            check(False, 'known scenario ' + sc)
    print('{} failed'.format(len(failures)) if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
