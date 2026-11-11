@echo off
rem BOTW VR Recomp: start the game in VR (docs/vr/SETUP.md).
rem First: start the headset's OpenXR runtime (Virtual Desktop connected with VDXR or SteamVR, Quest Link, SteamVR...).
rem Copy this file anywhere and set REPO below if it is not in tools\vr of the repository.

set REPO=%~dp0..\..
cd /d "%REPO%"
set PATH=C:\msys64\ucrt64\bin;%PATH%

set SAVAGEWIND_VR=1

rem View: first = first person (in Link's head, as in BetterVR); third = the game's camera behind him.
set SAVAGEWIND_VR_VIEW=first

rem Height of each eye's image in pixels (the width follows the headset's shape).
rem   1080 = light (~110 fps per eye on an RTX 3090)   1440 = sharper (~100)   1800 = very sharp (~90)
set SAVAGEWIND_VR_HEIGHT=1440

rem Turning with the right stick: smooth (the game's own) or snap (steps; less motion sickness).
set SAVAGEWIND_VR_TURN=smooth
set SAVAGEWIND_VR_SNAP_DEGREES=45

rem HUD floating ahead of the view (1) or drawn into the image (0).
set SAVAGEWIND_VR_HUD=1

rem Motion combat: right-hand slashes and stabs attack, the left hand raised guards with the shield.
set SAVAGEWIND_VR_MOTION=1
rem normal, or easy (slower swings count)
set SAVAGEWIND_VR_SWING=normal

build\savagewind.exe extracted --play
if errorlevel 1 (
    echo.
    echo The game ended with an error. See savagewind.log in "%CD%", the [vr] lines.
    pause
)
