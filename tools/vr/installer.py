#!/usr/bin/env python3
"""BOTW VR Recomp installer: a window that takes your own extracted game (ExeFS, RomFS, the DLC) and does the rest.

  1. checks the toolchain (MSYS2 UCRT64 with clang, the OpenXR loader, tools/venv) and installs what is missing
     (tools/setup_windows.ps1 and pacman; it asks first);
  2. puts your folders where the build reads them (extracted/exefs, extracted/romfs, extracted/aoc1, aoc2: links, or
     copies if you ask for them), after checking them;
  3. recompiles the game code (`make gen`) and builds the game with the VR mode (`make VR=1`);
  4. writes "Play BOTW VR.bat" with the VR options you chose, and a desktop shortcut to it.

Standard library only (tkinter). Start it with "Install BOTW VR.bat" at the repository's root, or:
  py -3 tools\\vr\\installer.py
Nothing of the game leaves your machine; nothing is written outside this repository but the desktop shortcut.
"""
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

ROOT = Path(__file__).resolve().parents[2]
MSYS = Path(r'C:\msys64')
UCRT = MSYS / 'ucrt64' / 'bin'
EXEFS_FILES = ('main', 'main.npdm', 'rtld', 'sdk', 'subsdk0')

T = {  # (English, Spanish)
    'title': ('BOTW VR Recomp - installer', 'BOTW VR Recomp - instalador'),
    'intro': ('Your own Breath of the Wild 1.9.0, extracted from your own console (docs/vr/SETUP.md). '
              'Choose the folders, then Install.',
              'Tu propio Breath of the Wild 1.9.0, extraído de tu propia consola (docs/vr/SETUP.es.md). '
              'Elegí las carpetas y apretá Instalar.'),
    'exefs': ('ExeFS folder (main, main.npdm, rtld, sdk, subsdk0)', 'Carpeta ExeFS (main, main.npdm, rtld, sdk, subsdk0)'),
    'romfs': ('RomFS folder (base + 1.9.0 update)', 'Carpeta RomFS (base + update 1.9.0)'),
    'aoc1': ('DLC 1 RomFS (optional)', 'RomFS del DLC 1 (opcional)'),
    'aoc2': ('DLC 2 RomFS (optional)', 'RomFS del DLC 2 (opcional)'),
    'browse': ('Browse...', 'Elegir...'),
    'copy': ('Copy the files instead of linking them (needs ~16 GB)', 'Copiar los archivos en vez de enlazarlos (~16 GB)'),
    'vr': ('VR options', 'Opciones VR'),
    'view': ('View', 'Vista'), 'first': ('First person (BetterVR)', 'Primera persona (BetterVR)'),
    'third': ('Third person', 'Tercera persona'),
    'height': ('Eye image height', 'Alto de imagen por ojo'),
    'turn': ('Turning', 'Giro'), 'motion': ('Motion combat', 'Combate con movimiento'),
    'left': ('Left-handed', 'Zurdo'),
    'install': ('Install', 'Instalar'), 'play': ('Play', 'Jugar'), 'close': ('Close', 'Cerrar'),
    'tools_missing': ('The build tools are missing: {}.\n\nInstall them now? (MSYS2, clang, Python 3.12; a few GB, '
                      'a few minutes)', 'Faltan las herramientas de compilación: {}.\n\n¿Instalarlas ahora? '
                      '(MSYS2, clang, Python 3.12; unos GB, unos minutos)'),
    'done': ('Done. Start the headset\'s OpenXR runtime (Virtual Desktop: VDXR), then Play.',
             'Listo. Abrí el runtime OpenXR del visor (Virtual Desktop: VDXR) y apretá Jugar.'),
    'failed': ('Failed: {}. The log above says why.', 'Falló: {}. El registro de arriba dice por qué.'),
}
LANG = 1 if os.environ.get('LANG', '').startswith('es') or 'es' in (os.environ.get('VR_INSTALLER_LANG') or '') else 0
try:
    import locale
    if (locale.getlocale()[0] or '').lower().startswith(('es', 'spanish')):
        LANG = 1
except Exception:
    pass


def t(key):
    return T[key][LANG]


def check_exefs(d):
    d = Path(d)
    missing = [f for f in EXEFS_FILES if not (d / f).is_file()]
    if missing:
        return 'ExeFS: {} missing in {}'.format(', '.join(missing), d)
    if (d / 'main').read_bytes()[:4] != b'NSO0':
        return 'ExeFS: main is not an NSO (still encrypted?)'
    return None


def check_romfs(d):
    d = Path(d)
    v = d / 'System' / 'Version.txt'
    if not v.is_file():
        return 'RomFS: System/Version.txt not found in {} (is it the RomFS root?)'.format(d), None
    ver = v.read_text(errors='replace').strip()
    if (d / 'System' / 'AocVersion.txt').is_file():
        return 'RomFS: {} is a DLC RomFS, not the game\'s'.format(d), None
    return None, ver


def check_dlc(d, n):
    d = Path(d)
    if n == 2 and not (d / 'System' / 'AocVersion.txt').is_file():
        return 'DLC 2: System/AocVersion.txt not found in {}'.format(d)
    if n == 1 and not any((d / s).is_dir() for s in ('Map', 'Pack', 'Game')):
        return 'DLC 1: {} does not look like the first DLC\'s RomFS'.format(d)
    return None


def tools_missing():
    miss = []
    if not (UCRT / 'clang.exe').is_file():
        miss.append('MSYS2 UCRT64 clang')
    if not (MSYS / 'usr' / 'bin' / 'make.exe').is_file():
        miss.append('make')
    if not (UCRT / 'libopenxr_loader.dll').is_file():
        miss.append('OpenXR loader')
    if not (ROOT / 'tools' / 'venv' / 'Scripts' / 'python.exe').is_file():
        miss.append('tools/venv')
    return miss


def link_or_copy(src, dst, copy, log):
    src, dst = Path(src).resolve(), Path(dst)
    if dst.is_symlink() or os.path.isjunction(dst):   # (a link from an earlier install: just replaced)
        os.rmdir(dst) if os.path.isdir(dst) else dst.unlink()
    elif dst.exists():
        if dst.resolve() == src:
            return
        bak = dst.with_name(dst.name + '.old')
        log('  {} exists: moved to {}'.format(dst, bak.name))
        if bak.exists():
            shutil.rmtree(bak)
        dst.rename(bak)
    dst.parent.mkdir(parents=True, exist_ok=True)
    if copy:
        log('  copying {} -> {}'.format(src, dst))
        shutil.copytree(src, dst)
    else:
        log('  linking {} -> {}'.format(dst, src))
        r = subprocess.run(['cmd', '/c', 'mklink', '/J', str(dst), str(src)], capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError('mklink /J failed: ' + (r.stdout + r.stderr).strip())


def write_launcher(opts):
    bat = ROOT / 'Play BOTW VR.bat'
    lines = ['@echo off', 'rem Written by tools/vr/installer.py: BOTW VR with the options chosen there.',
             'cd /d "%~dp0"', 'set PATH=C:\\msys64\\ucrt64\\bin;%PATH%', 'set SAVAGEWIND_VR=1',
             'set SAVAGEWIND_VR_HEIGHT={}'.format(opts['height']), 'set SAVAGEWIND_VR_HUD=1',
             'build\\savagewind.exe extracted --play',
             'if errorlevel 1 (echo. & echo The game ended with an error: see savagewind.log, the [vr] lines. & pause)']
    bat.write_text('\r\n'.join(lines) + '\r\n')
    write_vr_settings(opts)
    return bat


def write_vr_settings(opts):
    """The options chosen here go to the game's settings file (savagewind.ini, the vr_* keys of runtime/settings.h
    VR_OPTIONS), where the game's Options screen (VR page) shows and changes them; not as variables, which would win
    over that screen."""
    ini = ROOT / 'savagewind.ini'
    snap = opts['turn'].startswith('snap')
    want = {'vr_view': 1 if opts['view'] == 'third' else 0, 'vr_snap_turn': 1 if snap else 0,
            'vr_snap_degrees': int(opts['turn'][4:]) if snap else 45,
            'vr_turn_speed': {'smooth-slow': 60, 'smooth-fast': 150}.get(opts['turn'], 100),
            'vr_motion_combat': 1 if opts['motion'] else 0, 'vr_left_handed': 1 if opts['left'] else 0}
    lines = ini.read_text(errors='replace').splitlines() if ini.exists() else []
    for k, v in want.items():
        for n, line in enumerate(lines):
            if line.split('=')[0].strip() == k:
                lines[n] = '{} = {}'.format(k, v)
                break
        else:
            lines.append('{} = {}'.format(k, v))
    ini.write_text('\n'.join(lines) + '\n')


def desktop_shortcut(target, name):
    ps = ('$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath("Desktop")+"\\{}.lnk");'
          '$s.TargetPath="{}";$s.WorkingDirectory="{}";$s.IconLocation="{}";$s.Save()').format(
        name, target, ROOT, ROOT / 'docs' / 'icon' / 'icon.ico')
    subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True)


class App:
    def __init__(self, root):
        self.root = root
        root.title(t('title'))
        root.geometry('900x680')
        self.q = queue.Queue()
        self.busy = False
        f = ttk.Frame(root, padding=10)
        f.pack(fill='both', expand=True)
        ttk.Label(f, text=t('intro'), wraplength=860).pack(anchor='w', pady=(0, 8))
        self.paths = {}
        for key in ('exefs', 'romfs', 'aoc1', 'aoc2'):
            row = ttk.Frame(f)
            row.pack(fill='x', pady=2)
            ttk.Label(row, text=t(key), width=48).pack(side='left')
            v = tk.StringVar(value=self.guess(key))
            self.paths[key] = v
            ttk.Entry(row, textvariable=v).pack(side='left', fill='x', expand=True, padx=4)
            ttk.Button(row, text=t('browse'), command=lambda k=key: self.browse(k)).pack(side='left')
        self.copy = tk.BooleanVar(value=False)
        ttk.Checkbutton(f, text=t('copy'), variable=self.copy).pack(anchor='w', pady=4)
        box = ttk.LabelFrame(f, text=t('vr'), padding=6)
        box.pack(fill='x', pady=4)
        self.view = tk.StringVar(value='first')
        ttk.Label(box, text=t('view')).grid(row=0, column=0, sticky='w')
        ttk.Radiobutton(box, text=t('first'), value='first', variable=self.view).grid(row=0, column=1, sticky='w')
        ttk.Radiobutton(box, text=t('third'), value='third', variable=self.view).grid(row=0, column=2, sticky='w')
        ttk.Label(box, text=t('height')).grid(row=1, column=0, sticky='w')
        self.height = tk.StringVar(value='1440')
        ttk.Combobox(box, textvariable=self.height, values=['1080', '1440', '1800'], width=8).grid(row=1, column=1, sticky='w')
        ttk.Label(box, text=t('turn')).grid(row=2, column=0, sticky='w')
        self.turn = tk.StringVar(value='snap30')
        ttk.Combobox(box, textvariable=self.turn, width=14, values=['smooth-slow', 'smooth', 'smooth-fast', 'snap30',
                                                                   'snap45', 'snap60']).grid(row=2, column=1, sticky='w')
        self.motion = tk.BooleanVar(value=True)
        self.left = tk.BooleanVar(value=False)
        ttk.Checkbutton(box, text=t('motion'), variable=self.motion).grid(row=3, column=1, sticky='w')
        ttk.Checkbutton(box, text=t('left'), variable=self.left).grid(row=3, column=2, sticky='w')
        self.log_box = tk.Text(f, height=16, bg='#111', fg='#ddd', font=('Consolas', 9))
        self.log_box.pack(fill='both', expand=True, pady=6)
        self.bar = ttk.Progressbar(f, mode='indeterminate')
        self.bar.pack(fill='x')
        btns = ttk.Frame(f)
        btns.pack(fill='x', pady=6)
        self.b_install = ttk.Button(btns, text=t('install'), command=self.install)
        self.b_install.pack(side='left')
        self.b_play = ttk.Button(btns, text=t('play'), command=self.play)
        self.b_play.pack(side='left', padx=6)
        ttk.Button(btns, text=t('close'), command=root.destroy).pack(side='right')
        if not (ROOT / 'build' / 'savagewind.exe').is_file():
            self.b_play.state(['disabled'])
        root.after(100, self.pump)

    def guess(self, key):
        e = ROOT / 'extracted' / key
        return str(e.resolve()) if e.exists() else ''

    def browse(self, key):
        d = filedialog.askdirectory(title=t(key), initialdir=self.paths[key].get() or str(Path.home()))
        if d:
            self.paths[key].set(d)
            if key == 'exefs' and not self.paths['romfs'].get():   # (a folder with exefs/ and romfs/ side by side)
                for cand in (Path(d).parent / 'romfs', Path(d) / 'romfs'):
                    if (cand / 'System').is_dir():
                        self.paths['romfs'].set(str(cand))

    def log(self, s):
        self.q.put(s)

    def pump(self):
        while not self.q.empty():
            s = self.q.get()
            if s is None:
                self.bar.stop()
                self.busy = False
                self.b_install.state(['!disabled'])
                if (ROOT / 'build' / 'savagewind.exe').is_file():
                    self.b_play.state(['!disabled'])
                continue
            self.log_box.insert('end', s + '\n')
            self.log_box.see('end')
        self.root.after(100, self.pump)

    def run(self, cmd, env=None, cwd=ROOT):
        self.log('$ ' + ' '.join(map(str, cmd)))
        p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             errors='replace', creationflags=0x08000000)
        for line in p.stdout:
            self.log('  ' + line.rstrip())
        if p.wait():
            raise RuntimeError('{} exited with {}'.format(Path(str(cmd[0])).name, p.returncode))

    def install(self):
        if self.busy:
            return
        p = {k: v.get().strip() for k, v in self.paths.items()}
        errs = []
        if not p['exefs'] or (e := check_exefs(p['exefs'])):
            errs.append(e if p['exefs'] else 'ExeFS: choose the folder')
        ver = None
        if not p['romfs']:
            errs.append('RomFS: choose the folder')
        else:
            e, ver = check_romfs(p['romfs'])
            if e:
                errs.append(e)
        for n in (1, 2):
            if p['aoc%d' % n] and (e := check_dlc(p['aoc%d' % n], n)):
                errs.append(e)
        if errs:
            messagebox.showerror(t('title'), '\n'.join(errs))
            return
        if ver and not ver.startswith('1.9.0'):
            if not messagebox.askyesno(t('title'), 'RomFS Version.txt says {}; the recomp needs 1.9.0. Go on?'.format(ver)):
                return
        miss = tools_missing()
        if miss and not messagebox.askyesno(t('title'), t('tools_missing').format(', '.join(miss))):
            return
        opts = {'view': self.view.get(), 'height': self.height.get(), 'turn': self.turn.get(),
                'motion': self.motion.get(), 'left': self.left.get()}
        self.busy = True
        self.b_install.state(['disabled'])
        self.bar.start(12)
        threading.Thread(target=self.work, args=(p, miss, opts), daemon=True).start()

    def work(self, p, miss, opts):
        try:
            if miss:
                self.log('== Installing the build tools')
                self.run(['powershell', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'tools' / 'setup_windows.ps1')])
            if not (UCRT / 'libopenxr_loader.dll').is_file():
                self.run([str(MSYS / 'usr' / 'bin' / 'bash.exe'), '-lc',
                          'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-openxr-sdk'])
            self.log('== Your game files -> extracted/')
            for key in ('exefs', 'romfs', 'aoc1', 'aoc2'):
                if p[key]:
                    link_or_copy(p[key], ROOT / 'extracted' / key, self.copy.get(), self.log)
            env = dict(os.environ)
            env['PATH'] = '{};{};{}'.format(UCRT, MSYS / 'usr' / 'bin', env.get('PATH', ''))
            env['MSYSTEM'] = 'UCRT64'
            make = str(MSYS / 'usr' / 'bin' / 'make.exe')
            jobs = str(max(2, (os.cpu_count() or 4)))
            self.log('== Recompiling the game code (make gen: a few minutes)')
            self.run([make, 'gen'], env=env)
            self.log('== Building with the VR mode (make VR=1: 5-20 minutes)')
            self.run([make, 'VR=1', '-j' + jobs], env=env)
            bat = write_launcher(opts)
            desktop_shortcut(bat, 'BOTW VR')
            self.log('== ' + t('done'))
            self.log('   {} (+ desktop shortcut "BOTW VR")'.format(bat))
        except Exception as e:
            self.log('== ' + t('failed').format(e))
        self.log(None)

    def play(self):
        bat = ROOT / 'Play BOTW VR.bat'
        if not bat.is_file():
            bat = write_launcher({'view': self.view.get(), 'height': self.height.get(), 'turn': self.turn.get(),
                                  'motion': self.motion.get(), 'left': self.left.get()})
        subprocess.Popen(['cmd', '/c', str(bat)], cwd=ROOT, creationflags=0x00000010)


def main():
    if sys.platform != 'win32':
        print('This installer is for Windows; on Linux follow docs/linux.md and docs/VR.md.')
        return 1
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
