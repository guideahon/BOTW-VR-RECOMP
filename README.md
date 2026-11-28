<p align="center"><img src="tools/vr/botwvr.png" alt="BOTW VR Recomp" width="160"></p>

<h1 align="center">BOTW VR Recomp</h1>

<p align="center"><i>The Legend of Zelda: Breath of the Wild</i> in PC VR, native on Windows and Linux</p>

<p align="center">
  <a href="https://github.com/guideahon/BOTW-VR-RECOMP/releases/latest"><b>Download</b></a> ·
  <a href="#install">Install</a> ·
  <a href="docs/vr/FEATURES.md">Features</a> ·
  <a href="docs/vr/SETUP.md">Setup</a> ·
  <a href="https://github.com/guideahon/BOTW-VR-RECOMP/issues">Issues</a> ·
  <a href="docs/vr/README.es.md">Español</a>
</p>

**BOTW VR Recomp** adds a PC-VR mode (OpenXR) to a native recompilation of *Breath of the Wild* (Nintendo Switch,
version 1.9.0): stereo, 6DOF head tracking, first person (or third), the HUD floating ahead, Link's arms following
your hands, [BetterVR](https://github.com/Crementif/BotW-BetterVR)'s gestures, motion combat, the bow aimed with your
hands, Magnesis by hand, throwing with your hand, and vibration. It is built from your own copy of the game.

The recompilation underneath is [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp); this project is a
fork of it that adds VR. Upstream's changes are merged in regularly, so everything Savage Wind does (frame rates,
upscalers, graphics presets, keyboard and mouse, mods, saves, ...) works here too, and you can still play flat.

> [!IMPORTANT]
> **No game files are included.** You need your own legally obtained copy of the game, updated to **version 1.9.0**
> and dumped from **your own** Nintendo Switch. This project does not provide game files, keys, firmware, dumping or
> decryption tools, or instructions on how to obtain them. The game code is recompiled locally, on your machine,
> from your files.

> [!WARNING]
> **Status: early.** It runs in a Quest 3 through Virtual Desktop. Expect rough edges: every feature, with what the
> automated tests (a fake OpenXR runtime and a hand simulator) and a person in a headset have checked, is in the
> feature table: **[docs/vr/FEATURES.md](docs/vr/FEATURES.md)**. What is done and what is missing, in short:
> [docs/vr/STATUS.md](docs/vr/STATUS.md).

> [!NOTE]
> **Made almost entirely with AI.** Nearly all of the code, tooling, reverse engineering and documentation is written
> by an AI (Anthropic's Claude, through Claude Code), directed, tested and reviewed by a human, the same way Savage
> Wind and LAS Recomp are made. If you don't trust code written by AI, you can simply ignore this project.

## Install

**You need:** your own game files (see [Game files](docs/game-files.md)), Windows 10 or later or Linux on x86-64, a
GPU with Vulkan 1.3, 8 GB of RAM (16 GB with integrated graphics), a 4-thread CPU with SSE4.2 (AVX2 runs faster),
about 20 GB of free disk space, and a PC-VR headset with an OpenXR runtime: Quest 2/3/Pro through Virtual Desktop
(VDXR or SteamVR), Quest Link or Air Link, SteamVR headsets, WMR; on Linux, the system's OpenXR runtime (WiVRn, Monado).

1. Download the latest build from [Releases](https://github.com/guideahon/BOTW-VR-RECOMP/releases):
   - **Windows:** the file ending in `windows.zip`. Extract it to a folder you can write to, not Program Files. Every
     game executable in the VR package starts in VR by default (`SAVAGEWIND_VR=0` starts flat).
   - **Linux PC:** the `BOTW-VR-Recomp-*-x86_64.AppImage`.
   - **Steam Frame (experimental):** the `BOTW-VR-Recomp-*-SteamFrame-aarch64.AppImage`.
     Make the AppImage executable and run it; it starts in VR (`SAVAGEWIND_VR=0` to play flat). More in
     [docs/linux.md](docs/linux.md).
2. Start it and choose the folder with your extracted game (`exefs/` and `romfs/`), then **Set up game files**. It
   checks the version, copies the files and recompiles the game code once, about 15 minutes on a 12-thread CPU.
   Your own files aren't changed.
3. Put your headset on and play.

To prepare the precompiled game code on a PC and move the complete package to another device, use the
[PC Maker](docs/vr/MAKER.md). It builds Windows on Windows, Linux PC on Linux, and Steam Frame ARM64 on Linux with a
local AArch64 sysroot.

**From source:** run `Install BOTW VR.bat` on Windows, which asks for your folders and does the rest; on Linux,
`make gen` and `make -j VR=1` ([docs/linux.md](docs/linux.md)). Step by step, dumping your own game and the manual
way: [docs/vr/SETUP.md](docs/vr/SETUP.md).

## Playing

Every VR option is in the game: Pause > System > Options > **VR** (also from the title screen's options): view,
cutscenes, turning, comfort, hands, combat, bow, HUD, eye resolution. The same page has a **Controls guide** with
what every button and gesture does.

The default eye resolution is 1440 lines. If performance suffers, try **Options > VR > Eye resolution > 1080**
(applied at once; the same page has the FPS overlay, render scale and upscaler); higher settings use more GPU time. The F1 Savage Wind overlay opens with a keyboard. A standard gamepad
can also use its Guide button or Back + Start; in VR, tap the controller's menu button for pause or hold it for 0.6 s
to open the overlay. X/View still opens the map.

The default layout (Quest controllers):

| Control | In the game | Control | In the game |
|---|---|---|---|
| A | accept, interact | B | cancel, sheathe (a tap) |
| left stick | walk; click: crouch; held: run | right stick | turn; up: jump; click: inventory; held: the scope |
| right trigger | the right hand's action (attack, draw, the rune); held: weapon menu; carrying something: release to throw | left trigger | shield and lock on, parry on the press; held: shield menu |
| right grip | draw or put away the bow; held: bow menu | left grip | grab; with the bow out, held: arrow menu |
| Y | the rune; held: rune menu | X | the map |
| menu button | tap: pause; hold 0.6 s: Savage Wind overlay | both hands at the mouth + both triggers | whistle |

Body slots (from BetterVR): right shoulder + grip, the weapon; left shoulder, the bow; left hip, the rune; right hip
held, drop what that hand holds. Slashes and thrusts with the weapon in hand attack where your hand goes.

## Status and docs

- **[Feature table](docs/vr/FEATURES.md)**: every feature and what has been checked.
- **[Checklist](docs/vr/CHECKLIST.md)**: how to test and report by the table's IDs.
- **[Repeatable VR regression cases](docs/vr/TESTING.md)**: turn scripted `fake_xr` input into measurable regression checks.
- **[BetterVR gap](docs/vr/BETTERVR-GAP.md)**: what BetterVR does that this mod does not yet (Magnesis spin,
  ladders, full roomscale, HUD markers, the head's shadow, foveated rendering).
- **[VR reference](docs/VR.md)**: settings, environment variables, how it hooks into the game, the tests.
- **[Plan](docs/VR-PLAN.md)** and **[development log](docs/vr/DEVLOG.md)**.
- **[Performance](docs/vr/PERFORMANCE.md)**: how to measure, where the time goes, and how it compares with BetterVR.

Everything about the base game, which this project shares with Savage Wind: [FAQ](docs/FAQ.md),
[Saves](docs/saves.md), [Controls](docs/controls.md) (keyboard, mouse and flat play),
[Cheats and assists](docs/cheats-and-assists.md), [Linux](docs/linux.md), [Windows build](docs/BUILD-windows.md).
The data folder is still Savage Wind's: next to the executable on Windows, `~/.local/share/savagewind/` on Linux.

## Found a bug?

If it also happens flat (`SAVAGEWIND_VR=0`), it is most likely in the base game: check the [FAQ](docs/FAQ.md) and
[Savage Wind's issues](https://github.com/Revivedx/SavageWindRecomp/issues), and report it there only if it happens
in an official Savage Wind build too. VR bugs go to
[this project's issues](https://github.com/guideahon/BOTW-VR-RECOMP/issues): say which headset and runtime, the
feature's ID from the [table](docs/vr/FEATURES.md) if it has one, and attach your `savagewind.log`.

## Contributing

Help is welcome, from new features to playing it and reporting by the table's IDs: anyone with an AI coding agent can
pick a row of [docs/vr/FEATURES.md](docs/vr/FEATURES.md). Open pull requests against `main`. See
[CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).

## Respect copyright

- *The Legend of Zelda: Breath of the Wild* is © Nintendo. This is a fan project. It is not affiliated with,
  endorsed by or sponsored by Nintendo. *Nintendo Switch* and *The Legend of Zelda* are trademarks of Nintendo.
- This repository contains no game files, no game code (also not the C code the recompiler generates from it), no
  assets, no fonts, no keys - and release builds won't either.
- The game code you generate and the build made from it contain Nintendo's code. They are for your own use only:
  don't share them, upload them or ship them.
- Issues, pull requests or discussions that contain game files, keys, generated code or links to pirated copies
  will be removed. Piracy is not supported in any way.
- Follow the copyright laws of your country. Dumping a game you own for personal use is legal in some countries and
  not in others; that is your responsibility.

## Credits and license

- **VR:** the VR design, controls, gestures and Magnesis approach follow
  [BetterVR](https://github.com/Crementif/BotW-BetterVR) by Crementif and contributors (its swing analysis by Acudofy
  is ported under MIT, `third_party/BetterVR`). BetterVR itself credits the
  [BotW decompilation project](https://github.com/zeldaret/botw) by leoetlino and others.
- **The recompilation:** [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp) by Revivedx and
  contributors (MIT), which is built on [LAS Recomp](https://github.com/peiikai/LASRecomp) by peiikai (MIT) and used
  [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by Valiantsin Dzerakh (MIT) and
  [Ryujinx](https://github.com/Ryujinx/Ryujinx) (MIT) as references.
- **Special thanks** to Reddit user [amzukk1](https://www.reddit.com/user/amzukk1/) for a lot of testing in the
  headset: build after build of reports on the eyes, the hands, third person and Multiview.
- Full list and the licenses of what releases bundle: [Credits](docs/third-party.md).

The code is under the [MIT License](LICENSE), as upstream's. The license covers this project's code only: it grants
nothing for Nintendo's game, which you must supply yourself.
