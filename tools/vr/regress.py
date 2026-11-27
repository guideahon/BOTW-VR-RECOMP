#!/usr/bin/env python3
"""BOTW VR's regression suite: every check that once found a bug, run as one, before every release (docs/vr/TESTING.md
"Before every release"). Each case names the bug it guards against; a case is added here whenever a test finds a
bug, and is never taken out because the bug is fixed - that is what it is for.

  python tools/vr/regress.py --save <scratch save, the 100% one> [--exe build/native-vr/savagewind]
        [--quick] [--only id ...] [--list] [--out build/vr/regress]

--quick runs the Stereo cases (the default mode) and the unit tests (~15 min); without it every mode (~45 min). Prints a
table and writes <out>/regress.json; the exit code is the number of failed cases. The fps floors are this project's
reference PC's (RTX 3090, Ryzen 9 9950X3D; fake_xr, the Performance preset on): --no-perf leaves them out elsewhere.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.abspath(os.path.join(HERE, '..', '..'))
PY = sys.executable
MV = ['SAVAGEWIND_VR_RENDER=multiview', 'SAVAGEWIND_VR_EXPERIMENTAL=1']

# (id, quick, the bug it guards against, the command; {exe} {save} {out} {B} filled in)
CASES = [
    ('unit-gestures', True, 'right A dropped Link off a tree instead of taking an apple (Quest layout, 2026-11-26); the '
     'controls\' mapping', ['make', '-s', 'VR=1', 'DEPS={deps}', 'B={B}', '{B}/test_vr_gestures', '&&', '{B}/test_vr_gestures']),
    ('unit-pair', True, 'which eye each game frame draws, in every order (Stereo\'s pair)',
     ['make', '-s', 'VR=1', 'DEPS={deps}', 'B={B}', '{B}/test_vr_pair', '&&', '{B}/test_vr_pair']),
    ('version', True, 'the start screen showed Savage Wind\'s version, not BOTW VR\'s (2026-11-26)',
     ['{exe}', 'version']),
    ('launcher-first', True, 'Savage Wind Configuration did not come first in a VR build on Windows (2026-11-26); here '
     'Linux\'s, the same code path', [PY, 'tools/vr/test_vr_live_launcher.py', '--exe', '{exe}', '--save', '{save}', '--out', '{out}']),
    ('hands-moving-stereo', True, 'moving hands crossed between the eyes in Stereo (26.2\'s tester, 2026-11-26)',
     [PY, 'tools/vr/test_vr_hands_moving.py', '--exe', '{exe}', '--save', '{save}', '--out', '{out}', '--mode', 'stereo']),
    ('hands-walking-stereo', True, 'walking, the hands half a step apart between the eyes (2026-11-26); hands crossed '
     'turning with the stick (2026-11-25)', [PY, 'tools/vr/test_vr_stereo_sync.py', '--exe', '{exe}', '--save', '{save}',
                                            '--out', '{out}', '--mode', 'compat', '--walk']),
    ('pair-anchor-stereo', True, 'turning, the right eye drawn from another place (2026-11-24, Steam Frame); the view '
     'standing still then jumping 0.35 rad (2026-11-25)', [PY, 'tools/vr/test_vr_stereo_sync.py', '--exe', '{exe}',
                                                           '--save', '{save}', '--out', '{out}', '--mode', 'compat']),
    ('third-person-stereo', True, 'third person: the left eye a frame behind orbiting (25.9) and the head\'s turn put on '
     'twice (26.2\'s tester, 2026-11-26)', [PY, 'tools/vr/test_vr_third_person.py', '--exe', '{exe}', '--save', '{save}',
                                            '--out', '{out}', '--mode', 'stereo']),
    ('climb-eye', True, 'climbing, the eye 0.38 m over Link\'s head and 0.32 m back (2026-11-26)',
     [PY, 'tools/vr/test_vr_climb.py', '--exe', '{exe}', '--save', '{save}', '--out', '{out}']),
    ('eyes-stereo', True, 'something drawn in one eye only, a darker eye, the eyes apart after a menu, in third person '
     '(seven places; Stereo\'s fires flicker per eye: up to 15%)', [PY, 'tools/vr/bench/validate.py', '--exe', '{exe}',
                                                                    '--save', '{save}', '--out', '{outdir}', '--case',
                                                                    'eyes-stereo', '--limit', '15', '--min-fps', '{fps_stereo}',
                                                                    '--env', 'SAVAGEWIND_VR_RENDER=stereo']),
    ('eyes-multiview', False, 'smoke, ground, snow and markers in one eye only; the right eye the left one\'s picture '
     '(Multiview, 2026-11-25)', [PY, 'tools/vr/bench/validate.py', '--exe', '{exe}', '--save', '{save}', '--out', '{outdir}',
                                 '--case', 'eyes-multiview', '--limit', '1.0', '--min-fps', '{fps_multiview}', '--env'] + MV),
    ('eyes-mono', False, 'Mono: both eyes the same picture', [PY, 'tools/vr/bench/validate.py', '--exe', '{exe}', '--save',
                                                             '{save}', '--out', '{outdir}', '--case', 'eyes-mono', '--limit',
                                                             '0.05', '--min-fps', '{fps_mono}', '--env',
                                                             'SAVAGEWIND_VR_RENDER=mono']),
    ('hands-moving-multiview', False, 'moving hands between the eyes (Multiview)',
     [PY, 'tools/vr/test_vr_hands_moving.py', '--exe', '{exe}', '--save', '{save}', '--out', '{out}', '--mode', 'multiview']),
    ('third-person-multiview', False, 'third person, the head\'s turn put on twice (Multiview)',
     [PY, 'tools/vr/test_vr_third_person.py', '--exe', '{exe}', '--save', '{save}', '--out', '{out}', '--mode', 'multiview']),
    ('mono', False, 'Mono: the same bytes in both eyes, views from the head\'s centre, one update a frame',
     [PY, 'tools/vr/test_vr_mono.py', '--exe', '{exe}', '--save', '{save}', '--out', '{out}']),
]
# headset fps floors on the reference PC (the Performance preset on; measured 2026-11-26: 127-134 / 168 / 244)
FPS = {'stereo': 115, 'multiview': 150, 'mono': 210}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--save', help='the 100%% save (copied per run, never written)')
    ap.add_argument('--exe', default=os.path.join(TREE, 'build', 'native-vr', 'savagewind'))
    ap.add_argument('--deps', default='build/linux/x86_64/deps')
    ap.add_argument('--out', default=os.path.join(TREE, 'build', 'vr', 'regress'))
    ap.add_argument('--quick', action='store_true')
    ap.add_argument('--only', nargs='*')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--no-perf', action='store_true')
    a = ap.parse_args()
    if a.list:
        for cid, quick, why, _ in CASES:
            print('%-24s %-6s %s' % (cid, 'quick' if quick else 'full', why))
        return 0
    if not a.save:
        raise SystemExit('--save <the 100% save>')
    exe = os.path.abspath(a.exe)
    B = os.path.relpath(os.path.dirname(exe), TREE)
    env = dict(os.environ)
    env['LD_LIBRARY_PATH'] = os.path.join(TREE, a.deps, 'lib') + (':' + env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    os.makedirs(a.out, exist_ok=True)
    # (another game or a headset session on this PC shares the GPU: the fps floors mean nothing then - a run made while
    # the game was being played measured half the speed, 2026-11-26)
    busy = subprocess.run(['pgrep', '-fa', 'savagewind|wivrn-server|vrserver|monado'], capture_output=True, text=True).stdout.strip()
    if busy and not a.no_perf:
        print('WARNING: something else uses the GPU (the fps floors will not hold; close it or pass --no-perf):\n  ' +
              busy.replace('\n', '\n  '), flush=True)
    results = []
    for cid, quick, why, cmd in CASES:
        if a.only and cid not in a.only:
            continue
        if a.quick and not quick and not a.only:
            continue
        sub = {'exe': exe, 'save': a.save, 'out': os.path.join(a.out, cid), 'outdir': a.out, 'B': B, 'deps': a.deps}
        for m, f in FPS.items():
            sub['fps_' + m] = '0' if a.no_perf else str(f)
        line = ' '.join("'%s'" % c.format(**sub) if c != '&&' else '&&' for c in cmd)
        t0 = time.monotonic()
        print('=== %s: %s' % (cid, why), flush=True)
        p = subprocess.run(line, shell=True, cwd=TREE, env=env, capture_output=True, text=True, timeout=1800)
        text = p.stdout + p.stderr
        open(os.path.join(a.out, cid + '.log'), 'w').write(text)
        if cid == 'version':
            ok = p.returncode == 0 and re.search(r'^BOTW VR (alpha|beta) \d{4}\.\d\d\.\d\d\.\d+ \(Savage Wind ', text, re.M)
        elif cid.startswith('unit-'):
            ok = p.returncode == 0 and re.search(r' 0 failed', text)
        else:
            ok = p.returncode == 0
        fails = [l.strip() for l in text.splitlines() if 'FAIL' in l and not l.strip().startswith('FAIL ')][:4]
        results.append({'id': cid, 'ok': bool(ok), 'seconds': round(time.monotonic() - t0), 'why': why, 'fails': fails})
        print('    %s (%d s)%s' % ('PASS' if ok else 'FAIL', results[-1]['seconds'],
                                  ''.join('\n      ' + f for f in fails)), flush=True)
    json.dump(results, open(os.path.join(a.out, 'regress.json'), 'w'), indent=1)
    bad = [r for r in results if not r['ok']]
    print('\n%-24s %-5s %s' % ('case', '', 'guards against'))
    for r in results:
        print('%-24s %-5s %s' % (r['id'], 'ok' if r['ok'] else 'FAIL', r['why'][:90]))
    print('\n%d of %d passed' % (len(results) - len(bad), len(results)))
    return len(bad)


if __name__ == '__main__':
    sys.exit(main())
