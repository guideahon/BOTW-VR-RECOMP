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
import re
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


def launch(args, run_dir, script_lines, frame_rate=60, extra_env=None):
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
    env.update(extra_env or {})
    msys = r'C:\msys64\ucrt64\bin'  # (Windows: a source build's DLLs; docs/BUILD-windows.md)
    if os.name == 'nt' and os.path.isdir(msys) and msys.lower() not in env.get('PATH', '').lower():
        env['PATH'] = msys + os.pathsep + env.get('PATH', '')
    log = open(os.path.join(run_dir, 'game.log'), 'wb')
    proc = subprocess.Popen([args.exe, 'extracted'], cwd=TREE, env=env, stdout=log, stderr=subprocess.STDOUT)
    game = perf_routes.Game(args.port)
    return game, proc, log


def ready(args, game, proc):
    """The world is up: let it settle, and close any notice the game put up (a tip, a save) with B."""
    perf_routes.wait_world(game, proc, args.boot)
    time.sleep(args.settle)
    for _ in range(2):
        game.cmd('press', 'b', ok=False)
        time.sleep(0.7)


def vr_state(game):
    return game.cmd('vr', ok=False)


def fake_log(run_dir):
    p = os.path.join(run_dir, 'fake_xr.log')
    return open(p, encoding='utf-8', errors='replace').read() if os.path.exists(p) else ''


def scenario_cinema(args, run_dir):
    game, proc, log = launch(args, run_dir, ['# nothing scripted: a still head'])
    try:
        ready(args, game, proc)
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
    game, proc, log = launch(args, run_dir, ['{} {}'.format(at, event)], frame_rate=0)
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
    # (mono: `camera` reads the drawn view from TAA's matrix, and stereo turns TAA off)
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={'SAVAGEWIND_VR_STEREO': '0'})
    try:
        ready(args, game, proc)

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


def read_ppm_gray(path, step=2):
    """A PPM as rows of grey levels, every step-th pixel both ways."""
    data = open(path, 'rb').read()
    head = data.split(b'\n', 3)
    w, h = map(int, head[1].split())
    px = head[3]
    rows = []
    for y in range(0, h, step):
        base = y * w * 3
        rows.append([(px[base + x * 3] * 3 + px[base + x * 3 + 1] * 6 + px[base + x * 3 + 2]) // 10
                     for x in range(0, w, step)])
    return rows


def best_shift(a, b, max_dx=20, max_dy=4):
    """(dx, dy, mean difference at it, at 0): where b's middle matches a's best (b moved by dx, dy against a)."""
    h, w = len(a), len(a[0])
    y0, y1, x0, x1 = h // 4, 3 * h // 4, w // 4, 3 * w // 4
    def sad(dx, dy):
        s, n = 0, 0
        for y in range(y0, y1, 2):
            ra, rb = a[y], b[y + dy]
            for x in range(x0, x1, 2):
                s += abs(ra[x] - rb[x + dx])
                n += 1
        return s / n
    scores = {(dx, dy): sad(dx, dy) for dy in range(-max_dy, max_dy + 1) for dx in range(-max_dx, max_dx + 1)}
    (dx, dy), best = min(scores.items(), key=lambda kv: kv[1])
    return dx, dy, best, scores[(0, 0)]


def scenario_stereo(args, run_dir):
    """Two eyes: the left and right pictures of one headset frame differ by a sideways shift (near things more), never
    an up-down one; in mono they are the same picture."""
    # (eyes 25 cm apart: four times a person's, so the shift is plain even for things at a middle distance)
    game, proc, log = launch(args, run_dir, ['# a still head'], frame_rate=0, extra_env={'FAKE_XR_IPD': '0.25'})
    try:
        ready(args, game, proc)
        v1 = vr_state(game)
        time.sleep(4)
        v2 = vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    pairs = v2.get('stereo_pairs', 0) - v1.get('stereo_pairs', 0)
    frames = v2.get('frames', 0) - v1.get('frames', 0)
    check(pairs > 50, '{} stereo pairs in 4 s'.format(pairs))
    check(frames > 0 and abs(pairs / frames - 1) < 0.1, 'one headset frame a pair ({} frames, {} pairs)'
          .format(frames, pairs))
    caps = sorted(f for f in os.listdir(os.path.join(run_dir, 'cap')) if f.endswith('_view0.ppm'))
    check(len(caps) >= 2, '{} two-eye captures'.format(len(caps)))
    if not caps:
        return
    left = os.path.join(run_dir, 'cap', caps[-1])
    right = left.replace('_view0.ppm', '_view1.ppm')
    a, b = read_ppm_gray(left), read_ppm_gray(right)
    dx, dy, best, at0 = best_shift(a, b)
    print('    right eye against left: best shift dx {} dy {} (half-size pixels), difference {:.1f} there, {:.1f} '
          'unshifted'.format(dx, dy, best, at0))
    check(at0 > 0.5, 'the eyes see different pictures (difference {:.1f})'.format(at0))
    # the right eye is to the right of the left one, so what both see appears further left in its picture
    check(dx <= -2, 'the right eye sees things further left: right[x + dx] = left[x] with dx {} < 0'.format(dx))
    check(dy == 0, 'no up-down shift between the eyes ({})'.format(dy))
    check(best < at0, 'the shift explains the difference ({:.1f} < {:.1f})'.format(best, at0))


def read_pgm(path):
    data = open(path, 'rb').read()
    head = data.split(b'\n', 3)
    w, h = map(int, head[1].split())
    return w, h, head[3]


def scenario_hud(args, run_dir):
    """The HUD apart: the eyes' pictures have none of it, a layer in front of them has it alone."""
    game, proc, log = launch(args, run_dir, ['# a still head'], frame_rate=0)
    try:
        perf_routes.wait_world(game, proc, args.boot)
        time.sleep(args.settle + 6)
    finally:
        perf_routes.stop_game(game, proc, log)
    fl = fake_log(run_dir)
    check('layers 2 projection,quad' in fl, 'in play the headset gets the eyes and the HUD (projection + quad)')
    import re
    caps = re.findall(r'capture (frame\d+_layer1_quad) \d+x\d+ .* opaque ([\d.]+)%', fl)
    check(len(caps) > 0, '{} HUD captures'.format(len(caps)))
    if not caps:
        return
    tag, opaque = caps[-1]
    check(0.2 < float(opaque) < 20, 'the HUD layer is see-through but for the HUD ({}% opaque)'.format(opaque))
    hud = os.path.join(run_dir, 'cap', tag + '.ppm')
    eye = os.path.join(run_dir, 'cap', tag.replace('layer1_quad', 'layer0_view0') + '.ppm')
    alpha = os.path.join(run_dir, 'cap', tag + '_alpha.pgm')
    if not all(os.path.exists(p) for p in (hud, eye, alpha)):
        check(False, 'HUD, eye and alpha captures of the same frame')
        return
    w, h, a = read_pgm(alpha)
    u = open(hud, 'rb').read().split(b'\n', 3)[3]
    e = open(eye, 'rb').read().split(b'\n', 3)[3]
    diff, n = 0, 0
    for i in range(0, w * h, 3):
        if a[i] == 255:  # (where the HUD is: the eye's picture shows the world there, not the HUD)
            diff += sum(abs(u[i * 3 + k] - e[i * 3 + k]) for k in range(3)) / 3
            n += 1
    check(n > 0 and diff / n > 15, 'the eyes\' pictures have no HUD where the HUD layer has it (mean difference {:.1f} '
          'over {} pixels)'.format(diff / max(n, 1), n))


def scenario_menus(args, run_dir):
    """The pause menu, open and closed again and again: each time back in play, the headset gets the eyes again."""
    game, proc, log = launch(args, run_dir, ['# a still head'], frame_rate=0)
    rounds, back = args.menu_rounds, 0
    try:
        ready(args, game, proc)
        seen = []
        for i in range(rounds):
            game.cmd('press', 'plus', ok=False)  # (the pause menu)
            time.sleep(1.5)
            paused = vr_state(game).get('world_view') is False
            game.cmd('press', 'plus', ok=False)
            time.sleep(1.5)
            v1 = vr_state(game)
            time.sleep(0.5)
            v2 = vr_state(game)
            ok = paused and v2.get('world_view') and v2.get('world_frames', 0) > v1.get('world_frames', 0)
            back += bool(ok)
            seen.append('{}{}{}'.format('P' if paused else '-', 'W' if v2.get('world_view') else '-',
                                        '+' if v2.get('world_frames', 0) > v1.get('world_frames', 0) else '0'))
        print('    rounds (paused / back in play / eyes drawn): ' + ' '.join(seen))
        alive = proc.poll() is None
    finally:
        perf_routes.stop_game(game, proc, log)
    check(alive, 'the game still runs after {} pause menus'.format(rounds))
    check(back == rounds, 'every time out of the pause menu the eyes come back ({} of {})'.format(back, rounds))


def scenario_controllers(args, run_dir):
    """The VR controllers drive the game: the right stick turns the camera, the left stick walks Link."""
    R, L = '/user/hand/right/input/thumbstick', '/user/hand/left/input/thumbstick'
    cycle, lines = 80.0, []
    for k in range(60):
        t = k * cycle
        lines += ['{} stick {} 0 0'.format(t, R), '{} stick {} 0 0'.format(t, L),
                  '{} stick {} 1 0'.format(t + 40, R), '{} stick {} 0 0'.format(t + 50, R),
                  '{} stick {} 0 -1'.format(t + 60, L), '{} stick {} 0 0'.format(t + 70, L)]
    # (mono: `camera` reads the drawn view from TAA's matrix; fake_xr's time then runs with the game's frames)
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={'SAVAGEWIND_VR_STEREO': '0'})
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_until(offset):  # the next time the cycle reaches offset
            t = now_s()
            target = (t // cycle) * cycle + offset
            if target <= t:
                target += cycle
            while now_s() < target:
                time.sleep(0.1)

        wait_until(38)
        cam0, pos0 = camera(game), game.cmd('pos', ok=False)
        wait_until(45)
        v_turning = vr_state(game)
        wait_until(55)
        cam1 = camera(game)
        wait_until(58)
        pos1 = game.cmd('pos', ok=False)
        wait_until(65)
        v_walking = vr_state(game)
        wait_until(75)
        pos2 = game.cmd('pos', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    check(v_turning.get('controllers') is True, 'the controllers are read ({} syncs)'.format(v_turning.get('controller_syncs')))
    check(v_turning.get('controller_rx', 0) > 0.9, 'right stick right reaches the game ({:.2f})'
          .format(v_turning.get('controller_rx', 0)))
    check(v_walking.get('controller_ly', 0) < -0.9, 'left stick back reaches the game ({:.2f})'
          .format(v_walking.get('controller_ly', 0)))
    if cam0 and cam1:
        turn = angle_diff(cam1['yaw'], cam0['yaw'])
        check(abs(turn) > 20, 'the right stick turned the camera ({:.1f} deg)'.format(turn))
    else:
        check(False, 'the camera could be read')
    import math
    def dist(a, b):
        return math.sqrt(sum((a[k] - b[k]) ** 2 for k in ('x', 'z'))) if 'x' in a and 'x' in b else 0
    check(dist(pos0, pos1) < 0.5, 'Link stood still while the camera turned ({:.2f} m)'.format(dist(pos0, pos1)))
    # (back, towards the camera: where it is there is room; at least a metre)
    check(dist(pos1, pos2) > 1, 'the left stick walked Link ({:.2f} m)'.format(dist(pos1, pos2)))


def scenario_firstperson(args, run_dir):
    """First person (the default view): the eye in Link's head; with the head turned 90 deg, the left stick pushed
    back (2 s: the place may have a cliff ahead) walks Link away from where the view looks."""
    L = '/user/hand/left/input/thumbstick'
    cycle, lines = 60.0, []
    for k in range(60):
        t = k * cycle
        lines += ['{} head 0 1.7 0 0 0 0'.format(t), '{} stick {} 0 0'.format(t, L),
                  '{} head 0 1.7 0 90 0 0'.format(t + 30), '{} stick {} 0 -1'.format(t + 40, L),
                  '{} stick {} 0 0'.format(t + 42, L)]
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={'SAVAGEWIND_VR_STEREO': '0'})
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_until(offset):
            t = now_s()
            target = (t // cycle) * cycle + offset
            if target <= t:
                target += cycle
            while now_s() < target:
                time.sleep(0.1)

        wait_until(20)
        cam0, pos0 = camera(game), game.cmd('pos', ok=False)
        wait_until(38)
        cam1, pos1 = camera(game), game.cmd('pos', ok=False)
        wait_until(48)
        pos2 = game.cmd('pos', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    import math
    if not (cam0 and cam1 and 'x' in pos0 and 'x' in pos1 and 'x' in pos2):
        check(False, 'the camera and Link could be read')
        return
    d = math.sqrt((cam0['x'] - pos0['x']) ** 2 + (cam0['z'] - pos0['z']) ** 2)
    h = cam0['y'] - pos0['y']
    print('    eye from Link: {:.2f} m across, {:.2f} m up; view yaw {:.1f} -> {:.1f}'.format(d, h, cam0['yaw'], cam1['yaw']))
    check(d < 0.5 and 1.0 < h < 2.2, 'the eye is in Link\'s head ({:.2f} m from him, {:.2f} m up)'.format(d, h))
    glog = open(os.path.join(run_dir, 'game.log'), errors='replace').read()
    sk = [l for l in glog.splitlines() if "Link's skeleton:" in l]
    check("[vr] Link's head hidden" in glog, 'Link\'s head is hidden in first person ({})'.format(sk[0] if sk else 'no skeleton'))
    walk = (pos2['x'] - pos1['x'], pos2['z'] - pos1['z'])
    n = math.hypot(*walk)
    check(n > 0.5, 'the left stick walked Link ({:.2f} m)'.format(n))
    if n > 0.5:
        yaw = math.radians(cam1['yaw'])
        fwd = (math.sin(yaw), math.cos(yaw))
        cos_a = (walk[0] * fwd[0] + walk[1] * fwd[1]) / n
        check(cos_a < -0.8, 'with the head turned 90 deg, back walks him away from the view (cos {:.2f})'.format(cos_a))


def scenario_gestures(args, run_dir):
    """BetterVR's body gestures in play (third person, to see Link): the right hand's grip pressed and let go over the
    right shoulder takes the weapon out, again puts it away; each pulses the controller; both grips at the mouth
    whistle. Read from the gestures' trace, fake_xr's haptics and two pictures."""
    G = '/user/hand/right/input/squeeze/value'
    GL = '/user/hand/left/input/squeeze/value'
    rest_r, rest_l, shoulder = '0.25 1.1 -0.35', '-0.25 1.1 -0.35', '0.15 1.65 0.12'
    cycle, lines = 40.0, []
    for k in range(40):
        t = k * cycle
        lines += ['{:.2f} hand right {} 0 0 0'.format(t, rest_r), '{:.2f} hand left {} 0 0 0'.format(t, rest_l),
                  '{:.2f} value {} 0'.format(t, G), '{:.2f} value {} 0'.format(t, GL),
                  '{:.2f} hand right {} 0 0 0'.format(t + 4, rest_r), '{:.2f} hand right {} 0 0 0'.format(t + 4.5, shoulder),
                  '{:.2f} value {} 1'.format(t + 5, G), '{:.2f} value {} 0'.format(t + 5.2, G),        # out
                  '{:.2f} hand right {} 0 0 0'.format(t + 6, shoulder), '{:.2f} hand right {} 0 0 0'.format(t + 6.5, rest_r),
                  '{:.2f} hand right {} 0 0 0'.format(t + 14, rest_r), '{:.2f} hand right {} 0 0 0'.format(t + 14.5, shoulder),
                  '{:.2f} value {} 1'.format(t + 15, G), '{:.2f} value {} 0'.format(t + 15.2, G),      # away
                  '{:.2f} hand right {} 0 0 0'.format(t + 16, shoulder), '{:.2f} hand right {} 0 0 0'.format(t + 16.5, rest_r),
                  '{:.2f} hand right {} 0 0 0'.format(t + 24, rest_r), '{:.2f} hand left {} 0 0 0'.format(t + 24, rest_l),
                  '{:.2f} hand right 0.02 1.62 -0.1 0 0 0'.format(t + 24.5), '{:.2f} hand left -0.03 1.62 -0.1 0 0 0'.format(t + 24.5),
                  '{:.2f} value {} 1'.format(t + 25, G), '{:.2f} value {} 1'.format(t + 25, GL),       # whistle
                  '{:.2f} value {} 0'.format(t + 26, G), '{:.2f} value {} 0'.format(t + 26, GL),
                  '{:.2f} hand right 0.02 1.62 -0.1 0 0 0'.format(t + 26.5), '{:.2f} hand left -0.03 1.62 -0.1 0 0 0'.format(t + 26.5),
                  '{:.2f} hand right {} 0 0 0'.format(t + 27, rest_r), '{:.2f} hand left {} 0 0 0'.format(t + 27, rest_l)]
    lines.sort(key=lambda l: float(l.split()[0]))
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={
        'SAVAGEWIND_VR_STEREO': '0', 'SAVAGEWIND_VR_VIEW': 'third', 'SAVAGEWIND_VR_TRACE_GESTURES': '1',
        'SAVAGEWIND_VR_MOTION': '1', 'FAKE_XR_CAPTURE_EVERY': '0'})
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_until(offset):
            t = now_s()
            target = (t // cycle) * cycle + offset
            if target <= t:
                target += cycle
            while now_s() < target:
                time.sleep(0.1)

        wait_until(3)
        game.cmd('shot', 'build/vr/gest_before.png', ok=False)
        wait_until(11)
        game.cmd('shot', 'build/vr/gest_out.png', ok=False)
        wait_until(21)
        game.cmd('shot', 'build/vr/gest_away.png', ok=False)
        wait_until(30)
    finally:
        perf_routes.stop_game(game, proc, log)
    for f in ('gest_before', 'gest_out', 'gest_away'):
        src = os.path.join('build', 'vr', f + '.png')
        if os.path.exists(src):
            os.replace(src, os.path.join(run_dir, f + '.png'))
    glog = open(os.path.join(run_dir, 'game.log'), errors='replace').read()
    fl = fake_log(run_dir)
    check('[vr] controls: BetterVR' in glog, 'BetterVR\'s control scheme is on')
    check('gesture: in hand 0 -> 1' in glog, 'the right shoulder grip took the melee weapon out')
    check('gesture: in hand 1 -> 0' in glog, 'again: put it away')
    check('haptic /user/hand/right' in fl, 'the right controller vibrated ({} pulses)'.format(fl.count('haptic /user/hand/right')))
    print('    pictures: {}'.format(os.path.join(run_dir, 'gest_*.png')))


def scenario_arms(args, run_dir):
    """Link's arms follow the controllers (first person): fake_xr's hands held ahead, then moved; each wrist is solved
    onto its controller (read from the arms' log), the head hidden; a picture from the eye shows the arms."""
    lines = []
    for k in range(60):
        t = k * 20.0
        lines += ['{:.2f} hand right 0.25 1.45 -0.45 0 0 0'.format(t), '{:.2f} hand left -0.25 1.3 -0.4 0 0 0'.format(t),
                  '{:.2f} hand right 0.1 1.6 -0.5 0 30 0'.format(t + 10), '{:.2f} hand left -0.3 1.1 -0.3 0 -20 0'.format(t + 10)]
    lines.sort(key=lambda l: float(l.split()[0]))
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={
        'SAVAGEWIND_VR_STEREO': '0', 'SAVAGEWIND_VR_LOG_ARMS': '60', 'FAKE_XR_CAPTURE_EVERY': '0'})
    try:
        ready(args, game, proc)
        time.sleep(4)
        game.cmd('shot', 'build/vr/arms.png', ok=False)
        time.sleep(4)
    finally:
        perf_routes.stop_game(game, proc, log)
    if os.path.exists(os.path.join('build', 'vr', 'arms.png')):
        os.replace(os.path.join('build', 'vr', 'arms.png'), os.path.join(run_dir, 'arms.png'))
    glog = open(os.path.join(run_dir, 'game.log'), errors='replace').read()
    check("[vr] Link's arms follow the controllers" in glog, 'Link\'s arms follow the controllers')
    misses = [(float(a), float(b)) for a, b in re.findall(r'\[vr-arms\] left ok ([\d.]+) m, right ok ([\d.]+) m', glog)]
    check(len(misses) >= 5, '{} frames with both arms solved'.format(len(misses)))
    worst = max((max(m) for m in misses), default=9)
    check(worst < 0.02, 'each wrist on its controller (worst {:.3f} m)'.format(worst))
    check("[vr] Link's head hidden" in glog, 'his head hidden')
    print('    picture: {}'.format(os.path.join(run_dir, 'arms.png')))


def scenario_walkdir(args, run_dir):
    """SAVAGEWIND_VR_WALK=controller: with the head ahead and the left controller turned 90 deg left, the left stick
    pushed forward walks Link to the view's left."""
    L = '/user/hand/left/input/thumbstick'
    lines = []
    for k in range(60):
        t = 30.0 * k
        lines += ['{} head 0 1.7 0 0 0 0'.format(t), '{} hand left -0.25 1.2 -0.3 90 0 0'.format(t),
                  '{} stick {} 0 0'.format(t, L), '{} stick {} 0 1'.format(t + 20, L), '{} stick {} 0 0'.format(t + 21.5, L)]
    lines.sort(key=lambda l: float(l.split()[0]))
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={
        'SAVAGEWIND_VR_STEREO': '0', 'SAVAGEWIND_VR_WALK': 'controller', 'FAKE_XR_CAPTURE_EVERY': '0'})
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_until(offset):
            t = now_s()
            target = (t // 30) * 30 + offset
            if target <= t:
                target += 30
            while now_s() < target:
                time.sleep(0.1)
        wait_until(18)
        cam, p1 = camera(game), game.cmd('pos', ok=False)
        wait_until(25)
        p2 = game.cmd('pos', ok=False)
    finally:
        perf_routes.stop_game(game, proc, log)
    import math
    if not (cam and 'x' in p1 and 'x' in p2):
        check(False, 'the camera and Link could be read')
        return
    wx, wz = p2['x'] - p1['x'], p2['z'] - p1['z']
    n = math.hypot(wx, wz)
    check(n > 0.5, 'the left stick walked Link ({:.2f} m)'.format(n))
    if n > 0.5:
        yaw = math.radians(cam['yaw'])
        left = (math.cos(yaw), -math.sin(yaw))
        c = (wx * left[0] + wz * left[1]) / n
        check(c > 0.8, 'he walked the left controller\'s way, to the view\'s left (cos {:.2f})'.format(c))


def scenario_weapon(args, run_dir):
    """First person: the right grip at the right shoulder takes the weapon out; it is in Link's right hand, which
    follows the controller (vr_skel.c reads what each hand holds from the weapon models there); a picture of it."""
    G = '/user/hand/right/input/squeeze/value'
    lines = ['0 head 0 1.7 0 0 0 0', '0 hand left -0.25 1.3 -0.4 0 0 0']
    for k in range(40):
        t = 30.0 * k
        lines += ['%.1f hand right 0.25 1.45 -0.45 0 0 0' % t, '%.1f value %s 0' % (t, G),
                  '%.1f hand right 0.25 1.45 -0.45 0 0 0' % (t + 4), '%.1f hand right 0.15 1.65 0.12 0 0 0' % (t + 4.5),
                  '%.1f value %s 1' % (t + 5, G), '%.1f value %s 0' % (t + 5.2, G),
                  '%.1f hand right 0.15 1.65 0.12 0 0 0' % (t + 6), '%.1f hand right 0.25 1.45 -0.45 0 0 0' % (t + 6.5)]
    lines.sort(key=lambda l: float(l.split()[0]))
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={
        'SAVAGEWIND_VR_STEREO': '0', 'SAVAGEWIND_VR_TRACE_GESTURES': '1', 'FAKE_XR_CAPTURE_EVERY': '0'})
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0
        drawn = False
        for _ in range(3):   # (each cycle toggles: out, away, out...)
            t = now_s()
            target = (t // 30 + 1) * 30 + 8.0
            while now_s() < target:
                time.sleep(0.1)
            glog = open(os.path.join(run_dir, 'game.log'), errors='replace').read()
            if re.search(r'right hand holds Weapon_\S+ \(1\)', glog):
                game.cmd('shot', 'build/vr/weapon.png', ok=False)
                drawn = True
                break
    finally:
        perf_routes.stop_game(game, proc, log)
    if os.path.exists(os.path.join('build', 'vr', 'weapon.png')):
        os.replace(os.path.join('build', 'vr', 'weapon.png'), os.path.join(run_dir, 'weapon.png'))
    glog = open(os.path.join(run_dir, 'game.log'), errors='replace').read()
    held = re.findall(r'right hand holds (Weapon_\S+) \(1\)', glog)
    check(drawn and held, 'the shoulder grab put a melee weapon in Link\'s right hand ({})'.format(held[0] if held else 'none'))
    check("[vr] Link's arms follow the controllers" in glog, 'the arm (and the weapon with it) follows the controller')
    print('    picture: {}'.format(os.path.join(run_dir, 'weapon.png')))


def scenario_snap(args, run_dir):
    """Snap turning: a push of the right stick turns the view one step, at once, and only one."""
    R = '/user/hand/right/input/thumbstick'
    cycle, lines = 40.0, []
    for k in range(120):
        t = k * cycle
        lines += ['{} stick {} 0 0'.format(t, R), '{} stick {} 1 0'.format(t + 20, R),
                  '{} stick {} 0 0'.format(t + 24, R)]
    game, proc, log = launch(args, run_dir, lines, frame_rate=0,
                             extra_env={'SAVAGEWIND_VR_STEREO': '0', 'SAVAGEWIND_VR_TURN': 'snap'})
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_until(offset):
            t = now_s()
            target = (t // cycle) * cycle + offset
            if target <= t:
                target += cycle
            while now_s() < target:
                time.sleep(0.1)

        wait_until(15)
        before = camera(game)
        wait_until(22)  # (the stick still held over: one step, not one a frame)
        held = camera(game)
        wait_until(32)
        after = camera(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    if not all((before, held, after)):
        check(False, 'the camera could be read')
        return
    step = angle_diff(after['yaw'], before['yaw'])
    check(abs(step + 45) < 3, 'a push right turns the view 45 deg right ({:.1f})'.format(step))
    check(abs(angle_diff(held['yaw'], after['yaw'])) < 3, 'one step for one push, however long it is held ({:.1f})'
          .format(angle_diff(held['yaw'], after['yaw'])))
    check('[vr] snap turning: 45 degrees a step' in open(os.path.join(run_dir, 'game.log'), errors='replace').read(),
          'snap turning on')


def scenario_motion(args, run_dir):
    """Motion combat: the right hand's slash and stab each attack once, slow returns to guard do not, the left hand
    raised in front of the face guards."""
    cycle, lines = 30.0, []
    guard, low, raised = '0.25 1.2 -0.35', '-0.25 1.0 -0.1', '-0.1 1.55 -0.35'
    for k in range(100):
        t = k * cycle
        lines += [
            '{:.2f} hand right {} 0 0 0'.format(t, guard),
            '{:.2f} hand right {} 0 0 0'.format(t + 10, guard),       # a fast downward cut: 120 deg in 0.2 s
            '{:.2f} hand right {} 0 -120 0'.format(t + 10.2, guard),
            '{:.2f} hand right {} 0 -120 0'.format(t + 12, guard),    # back to guard slowly (2 s)
            '{:.2f} hand right {} 0 0 0'.format(t + 14, guard),
            '{:.2f} hand right {} 0 0 0'.format(t + 18, guard),       # a thrust: 35 cm forward in 0.15 s
            '{:.2f} hand right 0.25 1.2 -0.70 0 0 0'.format(t + 18.15),
            '{:.2f} hand right 0.25 1.2 -0.70 0 0 0'.format(t + 20),  # back slowly (2 s)
            '{:.2f} hand right {} 0 0 0'.format(t + 22, guard),
            '{:.2f} hand left {} 0 0 0'.format(t, low),
            '{:.2f} hand left {} 0 0 0'.format(t + 23.5, low),        # the left hand up in front of the face...
            '{:.2f} hand left {} 0 0 0'.format(t + 24, raised),
            '{:.2f} hand left {} 0 0 0'.format(t + 27.5, raised),     # ... and down again
            '{:.2f} hand left {} 0 0 0'.format(t + 28, low),
        ]
    game, proc, log = launch(args, run_dir, lines, frame_rate=0, extra_env={'FAKE_XR_CAPTURE_EVERY': '0'})  # (no captures: each one stalls the game)
    try:
        ready(args, game, proc)

        def now_s():
            return vr_state(game).get('frames', 0) / 90.0

        def wait_until(offset):
            t = now_s()
            target = (t // cycle) * cycle + offset
            if target <= t:
                target += cycle
            while now_s() < target:
                time.sleep(0.05)

        wait_until(5)
        v0 = vr_state(game)
        wait_until(16)
        v_slash = vr_state(game)
        wait_until(23)
        v_stab = vr_state(game)
        wait_until(26)
        v_up = vr_state(game)
        wait_until(29.5)
        v_end = vr_state(game)
    finally:
        perf_routes.stop_game(game, proc, log)
    def d(a, b, k):
        return b.get(k, 0) - a.get(k, 0)
    check(v0.get('motion_combat') is True, 'motion combat on')
    check(d(v0, v_slash, 'motion_slashes') == 1, 'the cut is one slash ({} slashes, {} stabs)'
          .format(d(v0, v_slash, 'motion_slashes'), d(v0, v_slash, 'motion_stabs')))
    check(d(v_slash, v_stab, 'motion_stabs') == 1 and d(v_slash, v_stab, 'motion_slashes') == 0,
          'the thrust is one stab ({} stabs, {} slashes)'.format(d(v_slash, v_stab, 'motion_stabs'),
                                                                d(v_slash, v_stab, 'motion_slashes')))
    check(d(v0, v_end, 'motion_attacks') == 2, 'two attacks in the cycle: the slow returns to guard are none ({})'
          .format(d(v0, v_end, 'motion_attacks')))
    check(v_slash.get('motion_last_power', 0) > 0.5, 'a strong cut ({:.2f})'.format(v_slash.get('motion_last_power', 0)))
    check(v_up.get('motion_shield') is True and v_stab.get('motion_shield') is False and v_end.get('motion_shield') is False,
          'the shield is up while the left hand is raised, and only then')
    check(v_up.get('controller_buttons', 0) & (1 << 8), 'the raised hand holds the guard (ZL) in the game\'s input')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--save', required=True, help='scratch save folder (holds save_0); copied, never written')
    ap.add_argument('--out', required=True)
    ap.add_argument('--scenario', nargs='+', default=['cinema', 'headtrack', 'firstperson', 'stereo', 'hud', 'menus', 'controllers', 'gestures', 'arms', 'weapon', 'walkdir', 'snap', 'motion', 'lost', 'exit'])
    ap.add_argument('--exe', default=os.path.join(TREE, 'build', 'savagewind.exe' if os.name == 'nt' else 'savagewind'))
    ap.add_argument('--runtime-json', default=os.path.join(TREE, 'build', 'vr', 'fake_xr.json'))
    ap.add_argument('--port', type=int, default=7792)
    ap.add_argument('--place', default='plateau')
    ap.add_argument('--boot', type=float, default=180)
    ap.add_argument('--settle', type=float, default=8)
    ap.add_argument('--boot-estimate', type=float, default=60,
                    help='fake_xr seconds (frames / 90) at which the lost / exit events come')
    ap.add_argument('--capture-every', type=int, default=240)
    ap.add_argument('--menu-rounds', type=int, default=20)
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
        elif sc == 'stereo':
            scenario_stereo(args, run_dir)
        elif sc == 'hud':
            scenario_hud(args, run_dir)
        elif sc == 'menus':
            scenario_menus(args, run_dir)
        elif sc == 'controllers':
            scenario_controllers(args, run_dir)
        elif sc == 'firstperson':
            scenario_firstperson(args, run_dir)
        elif sc == 'gestures':
            scenario_gestures(args, run_dir)
        elif sc == 'walkdir':
            scenario_walkdir(args, run_dir)
        elif sc == 'weapon':
            scenario_weapon(args, run_dir)
        elif sc == 'arms':
            scenario_arms(args, run_dir)
        elif sc == 'snap':
            scenario_snap(args, run_dir)
        elif sc == 'motion':
            scenario_motion(args, run_dir)
        else:
            check(False, 'known scenario ' + sc)
    print('{} failed'.format(len(failures)) if failures else 'all passed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
