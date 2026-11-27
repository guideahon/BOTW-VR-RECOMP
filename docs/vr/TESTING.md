# Writing repeatable VR regression cases

The useful part of a recorded-game workflow is a replayable input sequence with explicit expected observations. Here,
`fake_xr` already supplies a deterministic display clock and scripted head, hand and controller input. A game test adds
a known start state and checks the VR behavior exposed by the game. This does not make every part of BOTW deterministic,
so each case must state its setup and assert only the behavior it is meant to cover.

## Existing tools

- `tools/vr/fake_xr/fake_xr.c` reads `FAKE_XR_SCRIPT`. Its timestamps are seconds from the first display frame;
  `head` sets a head pose and `hand` adds an interpolated hand pose. For example:

  ```text
  0.00 head 0 1.70 0 0 0 0
  0.00 hand right 0.20 1.35 -0.40 0 0 0
  0.50 hand right 0.20 1.35 -0.40 0 0 90
  ```

- `tools/vr/test_vr_game.py` starts each scenario with a scratch save, a test clock and a scripted runtime. Focused
  cases such as `test_vr_arm_rig.py` and `test_vr_ladder.py` keep their own setup and assertions beside the test.
- `tools/vr/handsim.py` is useful for exploring a reported pose interactively. Move the finding into a scripted test so
  it can be replayed after the fix.

## Add or update a case

1. Link it to the feature ID and describe one behavior as **setup → input → expected observation**. Include the shortest
   sequence that reproduces the problem and the expected result as well as what happened.
2. Make the start state repeatable: record the game version, build/commit, scratch-save requirements, test warp, fake
   runtime refresh rate, relevant settings and environment variables. Never add a save to the repository.
3. Put head, hand and button changes on the fake runtime's virtual timeline. Keep timing assertions independent of
   wall-clock delays. State any dependency on game state that the fake clock does not control.
4. Assert the narrowest useful observable (for example, wrist-to-controller distance, a game state, an OpenXR layer or
   a controller action). Give numeric tolerances and explain what they protect. Do not use pixel equality across GPUs
   as a gameplay oracle; keep captures for local visual review or use a documented image metric.
5. Check that the case is sensitive to the regression when practical. A known negative control is
   `python tools/vr/test_vr_arm_rig.py --save <scratch save> --old`: the old arm rig is expected to fail the wrist
   sweep. Do not leave a deliberate mutation in the production code.
6. Store run output under an ignored directory such as `build/vr/<case>`. Captures read images back from the GPU and can
   stall the game, so disable periodic captures during timing measurements and request only the frames needed for
   visual review.

Each assertion should print the measured value and the limit, so a failure points to the first useful difference. Keep
automated evidence separate from headset evidence: mark **AI validated** only when the automated case passes, and mark
**Human validated** only after a person reports the behavior in a headset. A script can measure the arm geometry; it
cannot establish that the arms look natural to the player.

## Before every release: the regression suite

`tools/vr/regress.py` (`make vr-regress VR=1 B=build/native-vr DEPS=build/linux/x86_64/deps VR_SAVE=<the 100% save>`)
runs every check that once found a bug, each named with the bug it guards against (`--list`). `--quick`: the Stereo
cases and the unit tests (~15 min); without it, every mode (~45 min). A release is made only when it passes, and every
bug a test finds adds its case there - a fixed bug's case stays, so it cannot come back unnoticed. The fps floors are
the reference PC's (`--no-perf` elsewhere). What fake_xr cannot show (dropped frames, a runtime's own prediction, comfort)
still needs a headset; fake_xr's `FAKE_XR_HAND_DRIFT` gives later hand locations later poses, as a runtime does.

## Experimental features and releases

A change that can make play worse while it is being finished (a faster rendering path, a new effect, a new way of
placing the hands) goes in behind **Options > VR > Experimental features** (`vr_experimental`, off by default;
`SAVAGEWIND_VR_EXPERIMENTAL=1`) or its own option that is off by default. With it off the game uses what is tested, even
if the experimental choice is selected (the log says so). A release never makes an experimental path the default; it
graduates once its automated checks (for rendering: `test_vr_eye_match.py`, `test_vr_stereo_sync.py`) pass and a person
has seen it in a headset. Every release's notes and [CHECKLIST.md](CHECKLIST.md) say what to test and what to ignore in
that build.

## Example: arm rig (B01 / B10)

The headset report said that the wrists followed the controllers while Link's arms looked unnatural. The scripted arm
case sweeps eleven poses and turns the right wrist from -150° to +150°. It checks that the wrist is within 2 cm, each
30° step moves the elbow less than 6 cm, the full sweep stays within 12 cm, forearm roll has no step over 60°, and the
clavicles/elbows respond to an overhead pose. `--old` demonstrates that the sweep detects the earlier rig. These
measurements support the automated status; the headset report remains separate evidence for natural appearance.

Use synthetic inputs. Never commit game files, generated code, saves or test/golden captures; screenshots or short clips
may still be attached to an issue as allowed by the project's [copyright rules](../../CONTRIBUTING.md). Report results
by feature ID; see [the headset checklist](CHECKLIST.md).
