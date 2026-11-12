# BOTW VR Recomp: setting it up from zero

[Back to docs/VR.md](../VR.md) · *En español: [SETUP.es.md](SETUP.es.md)*

This repository is Savage Wind Recomp (a native PC port of *Breath of the Wild* 1.9.0, built from your own game) with
a PC-VR mode through OpenXR: stereo, 6DOF head tracking, the HUD floating ahead, VR controllers and motion combat. It
contains **no game files, keys or firmware**. You need your own copy of the game, dumped from your own Nintendo
Switch.

## 1. What you need

- Your own *Breath of the Wild* (cartridge or eShop) with the **1.9.0 update**, on a Switch you own that can run
  homebrew. Optional: both DLC from your own Expansion Pass.
- Windows 10/11 x86-64, a GPU with Vulkan 1.3 (tested on an RTX 3090), about 20 GB free.
- A PC-VR headset with an OpenXR runtime: Quest 2/3/Pro through Virtual Desktop (VDXR or SteamVR), Quest Link or Air
  Link, SteamVR headsets, WMR. Tested with fake_xr (the test runtime) and Virtual Desktop's VDXR on the desktop.

## 2. Dumping your own game

These are the usual community tools; this project ships none of them, and it is your job to follow your country's
laws (dumping a game you own for your own use is legal in some countries, not in others).

1. On your Switch, with homebrew: dump the game **and** its 1.9.0 update with
   [nxdumptool](https://github.com/DarkMatterCore/nxdumptool) (as NSP, or straight to ExeFS / RomFS with the update
   applied: its ExeFS and RomFS options let you pick the installed update).
2. Dump your console's keys (`prod.keys`) with [Lockpick_RCM](https://github.com/shchmue/Lockpick_RCM) if you dumped
   NSPs, and extract them on your PC with [hactool](https://github.com/SciresM/hactool) or an emulator's "Extract
   data" menu (the base NCA with the update NCA, so the RomFS is the merged one).
3. You need two folders out of it:
   - **ExeFS** of 1.9.0: `main`, `main.npdm`, `rtld`, `sdk`, `subsdk0`
   - **RomFS** with the 1.9.0 update applied (base + update merged), about 41,062 files
   - optional: each DLC's RomFS, apart (`aoc1/`, `aoc2/`)

Never share these files, your keys or a build of the game code (`generated/`, `build/swgame.dll`).

## 3. Where each folder goes

```
BOTW-VR-RECOMP/            this repository
  extracted/               (git ignores it)
    exefs/   main, main.npdm, rtld, sdk, subsdk0
    romfs/   the complete RomFS, base + 1.9.0 merged
    aoc1/    optional: first DLC's RomFS
    aoc2/    optional: second DLC's RomFS (System/AocVersion.txt)
```

A copy or a link both work: `mklink /J extracted\romfs D:\BOTW\romfs` saves 15 GB. Details and the DLC:
[docs/game-files.md](../game-files.md). `python tools/sw_probe.py` checks the version.

## 4. Building

The easy way: run `Install BOTW VR.bat` at the repository's root. It asks for your folders (ExeFS, RomFS, the
DLC), checks them, installs the tools if they are missing, links the folders into `extracted/`, builds, and writes
`Play BOTW VR.bat` with the VR options you pick, plus a desktop shortcut. By hand:


Once, from PowerShell at the repository root:

```
powershell -ExecutionPolicy Bypass -File tools\setup_windows.ps1
C:\msys64\usr\bin\pacman -S --needed mingw-w64-ucrt-x86_64-openxr-sdk
```

Then from Git Bash or an MSYS2 UCRT64 shell:

```
export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"
make gen          # once: recompiles the game code from extracted/exefs (a few minutes)
make VR=1 -j      # build/savagewind.exe with the VR mode
make test-vr      # optional: the VR tests, no game or headset needed
```

## 5. Playing in VR (Quest 3 + Virtual Desktop)

1. In Virtual Desktop's streamer on the PC, set the OpenXR runtime to **VDXR** (or SteamVR). On the Quest, open
   Virtual Desktop and connect to the PC.
2. Run `tools\vr\play-vr.bat` from Virtual Desktop's desktop (copy it anywhere and edit it: its variables are the
   VR settings, docs/VR.md "Settings"). Or, by hand:

   ```
   set SAVAGEWIND_VR=1
   build\savagewind.exe extracted --play
   ```

3. Look straight ahead when you get into play: that is "forward" (`vr recenter` in the in-game console sets it
   again). Without a headset or
   runtime the game runs flat and `savagewind.log` says why (`[vr]` lines).

Suggested on a Quest 3: 72 or 80 Hz in Virtual Desktop, `SAVAGEWIND_VR_HEIGHT=1440` (an RTX 3090 holds ~100 frames per
eye at 1440). Controls, comfort options and what is still missing: [docs/VR.md](../VR.md).

The limit is the CPU (Savage Wind's thread that turns the game's draws into Vulkan, ~1.6 us a draw), not the GPU: each
eye is a game frame. Measured in stereo on the Great Plateau (Ryzen 9 9950X3D, RTX 3090; game frames per second, eye
pairs half that): Anti-aliasing TAA 177-190 (~90 pairs), SMAA ~198 (~99), off ~216 (~108). For 90 Hz use SMAA or no
anti-aliasing; at 72 or 80 Hz TAA keeps up.
