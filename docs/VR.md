# VR mode (work in progress)

A PC-VR mode for Savage Wind through OpenXR: stereo, 6DOF head tracking, then motion controls. It is opt-in twice:
built only with `make VR=1`, and turned on at run time with `SAVAGEWIND_VR=1`. A build without `VR=1` has none of it.
The full plan, phase by phase with its tests (in Spanish): [VR-PLAN.md](VR-PLAN.md).

## Status

| Phase | What | State |
|---|---|---|
| 0 | Build flag, OpenXR loader, pose / projection math, the `fake_xr` test runtime, CI | done |
| 1 | OpenXR session on the game's Vulkan device; the flat game on a virtual screen in the headset | done |
| 2 | Head tracking (one view), the game's projection from the headset's field of view | done |
| 3 | Stereo: a frame per eye, the game's clock once a pair | done |
| 4 | HUD apart from the scene, menus on the virtual screen | done |
| 5 | VR controllers as the game's controller | done |
| 6 | Comfort: snap turning | done (vignette and others: not yet) |
| 7 | Link's arms follow the hands | not started (needs the game's bone code: [vr/re-notes.md](vr/re-notes.md)) |
| 8 | Motion combat | first part done: slashes, stabs and the shield by gesture (BetterVR's analysis); the weapon's own hit not yet |
| 9 | Performance | not started |

Phase 1: the window image (the game with its HUD) is copied every frame to a quad layer, a screen 3 m wide 2.5 m ahead
at eye height (`SAVAGEWIND_VR_SCREEN_WIDTH`, `SAVAGEWIND_VR_SCREEN_DISTANCE`, metres). The window keeps showing the
game. If the runtime is missing, has no headset, or ends or loses the session, the game goes on flat and says why:

```
[vr] OpenXR runtime <name> <version>, headset <name>
[vr] session on queue family 0 index 0; virtual screen 3.0 m wide at 2.5 m
[vr] virtual screen swapchain 1920x1080, 3 images
[vr] VR stopped (the session was lost): the game goes on flat
```

The `vr` control command (player safe, also in the in-game console) says what it is doing: on or off, why, the session
state and how many frames went to the headset.

Phase 2: in play (not paused, no event, no title, map or loading screen) the head moves the game's camera
(`runtime/vr/vr_camera.c`, from `rt_lookat` in `runtime/hle/freecam.c`): the game's camera keeps its place and heading
(it still follows Link and turns with the stick), the head adds its turn and its movement since the last recentring and
gives the pitch and roll itself. The world camera's projection (`rt_persp` in `runtime/nvn/output_scale.c`) becomes a
symmetric frustum that holds both eyes' fields, in the window's shape; the window takes the shape of the headset's eye
images at start (`SAVAGEWIND_VR_HEIGHT` lines high, 1080; for the run only), and the frame goes to the headset as a
projection layer with that frustum and the pose the camera was given. One picture for both eyes until phase 3; the HUD
is in it. Menus, events and loading go back to the virtual screen. `vr recenter` (or the first frame in play) makes the
head's pose the game camera's. Measured with fake_xr (RTX 3090, plateau, uncapped): 211 to 218 fps against 219 to 223
flat with the same settings, the longest frame 6 ms in both.

Phase 3: stereo. In play the game's frames come in pairs, left eye then right eye (`SAVAGEWIND_VR_STEREO=0`: one
picture for both, phase 2). The game's clock takes both frames' time in the left frame and nearly stops in the right
one, as it does for the free camera, so both eyes see the same moment and the world keeps its speed. Each eye has its
own swapchain; the left frame updates the left eye's image, the right frame the right one and makes the headset's frame
with both (xrWaitFrame paces the pairs: at 90 Hz the game runs at 180 fps). Which eye, pose and frustum a frame was
drawn with travels with it from the game's present (`nvnQueuePresentTexture`, `vr_present_mark`) to the queue thread's
(`backend_present`, `vr_present_begin`), in order. The game calls its clock and sets its cameras in either order within
a frame; the eye is chosen by whichever comes first after the last present (`runtime/vr/vr_pair.h`). TAA would blend
one eye into the other: in stereo the run uses SMAA in its place. Measured with fake_xr (RTX 3090, plateau, uncapped,
1030x1080 an eye): 213 game fps with TAA asked for (SMAA used), 224 with SMAA, against 225 flat: about 106 to 112
stereo pairs a second, each eye at over 90 Hz.

Phase 4: the HUD apart. In play the eyes get the 3D scene without the HUD: the copy frame generation already makes
right after the scene -> final image composite, before the HUD's first draw (`fg_copy_hudless` in
`runtime/nvn/vulkan/exec.c`). The HUD gets an image of its own (`vr_hud_prepare`, shader `vr_hud.frag`): the
presented frame where it differs from that copy, transparent elsewhere. It goes in front of the eyes as a quad layer
that moves with the head (VIEW space), 1.6 m wide 1.4 m ahead, a little below the line of sight
(`SAVAGEWIND_VR_HUD_WIDTH`, `SAVAGEWIND_VR_HUD_DISTANCE`; `SAVAGEWIND_VR_HUD=0` leaves it in the eyes' pictures). The
pause menu, the map, events and loading stay on the virtual screen; out of them play comes back to the eyes. Cost:
none measurable (220-225 fps with it).

Phase 5: the VR controllers are the game's controller (`runtime/vr/vr_input.c`, mapping in `vr_input_map.h`). An
OpenXR action set with bindings for Oculus / Meta Touch, Valve Index, Windows Mixed Reality, HTC Vive and the simple
controller; each poll of the game's input reads them and adds them to the gamepad's and keyboard's (a gamepad keeps
working). `SAVAGEWIND_VR_CONTROLLERS=0` turns them off.

| VR controller | Game | VR controller | Game |
|---|---|---|---|
| left stick | move | right stick | camera |
| left stick click | crouch | right stick click | look |
| X / Y | X (jump) / Y (attack) | A / B | A / B |
| left grip | L (runes) | right grip | R (throw) |
| left trigger | ZL (shield, lock on) | right trigger | ZR (bow) |
| menu: a tap | + (inventory) | menu: held 0.6 s | - (map) |
| left grip + right stick | d-pad (weapons, shields, bows, runes) | | |

(Index: the left A / B are X / Y and the left trackpad pressed is the menu; WMR and Vive: the touchpads pressed are A
and X, the right menu B, and on the Vive the touchpads are the sticks.)

Phase 8, motion combat (`runtime/vr/vr_melee.h`, `vr_input.c`): the right hand's slashes and stabs are told apart from
the rest of its motion by BetterVR's swing analysis (by Acudofy, ported to C, MIT: `third_party/BetterVR`), and each
one beginning presses the game's attack (Y); the left hand raised in front of the face holds the guard (ZL, the
shield up). The game plays its own attack: the weapon does not yet follow the hand or hit where it moves (that needs
the game's bone and attack-sensor code: [vr/re-notes.md](vr/re-notes.md)). The buttons keep working.

Phase 6, comfort: `SAVAGEWIND_VR_TURN=snap` makes the right stick's sideways push turn the view one step at once
(`SAVAGEWIND_VR_SNAP_DEGREES`, 45) instead of the game's smooth camera turn; the turn sits on top of the game camera's
heading, so Link walks the way the view looks. `vr recenter` puts the view back on the game camera.

Baseline before any VR rendering: [vr/baseline-flat.md](vr/baseline-flat.md) (RTX 3090, 1080p: 167 to 229 fps, the
render thread the limit at 4 to 6 ms a frame).

## Playing it

Only from a source build for now (no release of this fork; Savage Wind's own releases have no VR):

1. Build Savage Wind from source as docs/BUILD-windows.md says, with `make VR=1 -j`, and your own game files in
   `extracted/` (`make gen`, then `make VR=1 -j` again).
2. Start your headset's OpenXR runtime (SteamVR, Meta Quest Link / Air Link, Virtual Desktop's, ALVR through SteamVR,
   WMR) and make it the active OpenXR runtime.
3. Start the game with `SAVAGEWIND_VR=1`:

   ```
   set SAVAGEWIND_VR=1
   build\savagewind.exe extracted --play
   ```

The log says what was found (`[vr]` lines). Useful settings, as environment variables:

| Variable | Default | |
|---|---|---|
| `SAVAGEWIND_VR_TURN` | smooth | `snap`: the right stick turns the view in steps |
| `SAVAGEWIND_VR_SNAP_DEGREES` | 45 | the step |
| `SAVAGEWIND_VR_HEIGHT` | 1080 | the eye images' height (the width follows the headset's shape); with Video > Render scale above 100% the game draws them sharper |
| `SAVAGEWIND_VR_STEREO` | 1 | `0`: one picture for both eyes (lighter, no depth) |
| `SAVAGEWIND_VR_HUD` | 1 | `0`: the HUD stays in the eyes' pictures |
| `SAVAGEWIND_VR_HUD_WIDTH`, `_DISTANCE` | 1.6, 1.4 | the HUD's size and distance (m) |
| `SAVAGEWIND_VR_SCREEN_WIDTH`, `_DISTANCE` | 3.0, 2.5 | the virtual screen of menus, events and loading (m) |
| `SAVAGEWIND_VR_CONTROLLERS` | 1 | `0`: the VR controllers are not read (a gamepad only) |
| `SAVAGEWIND_VR_MOTION` | 1 | `0`: no motion combat (slashes, stabs and the raised-hand guard) |
| `SAVAGEWIND_VR_SWING` | normal | `easy`: BetterVR's easier swing profile (slower swings count) |
| `SAVAGEWIND_VR_SHIELD` | 1 | `0`: raising the left hand does not guard |

Stereo needs the game at about twice the headset's rate (180 fps for 90 Hz): lower Video > Render scale, the draw
distance or the shadows if the headset's runtime reports dropped frames, or run the headset at 72 Hz.

Meta Quest 3 through Virtual Desktop: Virtual Desktop Streamer registers its own OpenXR runtime (VDXR) as the active
one; connect the headset in Virtual Desktop first, then start the game from its desktop view. VDXR 1.1.0 offers
`XR_KHR_vulkan_enable2`, and with the headset not connected the game says so and stays flat (`found no headset
(xrGetSystem -35)`). The Quest's controllers are read through the Touch bindings. Measured against fake_xr on an RTX 3090
(Great Plateau, uncapped, an eye about as wide as high): `SAVAGEWIND_VR_HEIGHT` 1080 gives about 110 stereo pairs a
second, 1440 about 100, 1800 about 90; heavier places run about a quarter slower, so 72 or 80 Hz in Virtual Desktop
leaves room.

None of this has been tried on a real headset yet: every test so far runs against `fake_xr`. Reports from real
headsets (which one, which runtime, what the log says) are what is needed next.

## Not done yet

- Real headsets: untested (above).
- Link's arms following the hands (phase 7), the weapon hitting where it moves (phase 8's second part), grabbing and
  throwing by gesture: they need the game's bone and attack code found in the 1.9.0 code. Where that stands:
  [vr/re-notes.md](vr/re-notes.md).
- Comfort beyond snap turning: no vignette, no seated / standing height choice, no turning with the head only.
- Performance (phase 9): one game frame per eye. The shadows and other passes that do not depend on the eye are drawn
  twice; multiview, a fixed foveation and the runtime's reprojection with motion vectors are not used.
- The settings are environment variables: there is no VR page in the game's Options yet.
- TAA is replaced by SMAA in stereo (its history would blend the eyes).
- The window shows the eyes in turn (it flickers between them) while the headset is on.
- macOS: none of it (no OpenXR runtime there).

## Building

The loader is the system's: MSYS2 `mingw-w64-ucrt-x86_64-openxr-sdk`, Debian / Ubuntu `libopenxr-dev`. The headers are
in `third_party/openxr/`.

```
make VR=1 -j
SAVAGEWIND_VR=1 build/savagewind.exe extracted --play
```

`OPENXR_LIB` points at another loader (`make VR=1 OPENXR_LIB="-L/path -lopenxr_loader"`).

## How it hooks in

| Where | What |
|---|---|
| `runtime/main.c` | `vr_init` after the settings: the OpenXR instance (with `XR_KHR_vulkan_enable2`) and the headset |
| `runtime/nvn/vulkan/device.c` `create_instance` / `pick_device` / `create_device` | through `vr_vk_*`: the runtime makes the Vulkan instance and device and names the GPU; then the session on the game's queue |
| `device.c` `vk_frame_end` | `vr_frame_record` copies the window image into the headset's swapchain image; `vr_frame_after_submit` releases it and ends the OpenXR frame through `rec_custom`, after the submission, in order with every other use of the queue |
| `runtime/hle/freecam.c` `rt_lookat` | `vr_camera_lookat`: the head on the view cameras while the free camera is off |
| `runtime/nvn/output_scale.c` `rt_persp` | `vr_persp`: the world camera's frustum in play |
| `runtime/window_size.c` | a window in the headset's shape, none of the offered sizes |
| `runtime/hle/framepace.c` `rt_vfr_update` | `vr_frame_ratio`: stereo, the pair's game time in its left frame |
| `runtime/nvn/objects.c` `nvnQueuePresentTexture` | `vr_present_mark`: the frame's eye and pose, queued |
| `device.c` `backend_present` | `vr_present_begin`: ... taken in the same order |
| `runtime/nvn/vulkan/exec.c` `fg_draw`, `vk_fg_generate` | the HUD-less copy made in VR too; `vr_hud_prepare` splits the presented frame into scene and HUD |
| `runtime/nvn/vulkan/shaders.c` | `vr_hud.frag` |
| `runtime/hle/input.c` | `vr_input_apply`: the controllers added to the game's input, where the test script's is |
| `runtime/control_game.c` | the `vr` command |

Without `VR=1` the device.c hooks are macros that make the plain Vulkan calls.

| File | What |
|---|---|
| `runtime/vr/vr.h`, `vr.c` | `vr_init`, `vr_enabled`, `vr_status_text`; the OpenXR instance |
| `runtime/vr/xr_vulkan.c` | the Vulkan side and the session, the swapchain and the frame loop |
| `runtime/vr/vr_camera.c` | phases 2 and 3: the eye on the world's view cameras, the headset's frustum, recentring, the eye and pose each frame was drawn with |
| `runtime/vr/vr_pair.h` | the stereo pair's state machine (header only, unit tested) |
| `runtime/vr/vr_input.c`, `vr_input_map.h` | the controllers: actions, bindings, the game's buttons and sticks, snap turning, the hands' poses |
| `runtime/vr/vr_melee.h` | motion combat: BetterVR's swing and stab analysis, the shield gesture (header only, unit tested) |
| `runtime/vr/vr_control.c` | the `vr` command (`vr recenter` too) |
| `runtime/vr/vr_math.h` | quaternions, poses, the tracking space to game world mapping (`vr_anchor`), recentring, the LookAtCamera eye / at / up of a view, projections from `XrFovf`. Its conventions are at the top of the file |
| `tools/vr/fake_xr/` | `fake_xr`, a scripted OpenXR runtime for tests (below) |
| `tests/vr/`, `tools/vr/test_*` | the tests |

## Tests

| Command | Needs | What |
|---|---|---|
| `make test-vr` | nothing | `test_vr_math` (the math against values worked out by hand), `test_vr_pair` (the stereo pair's state machine in every order of clock and cameras: eyes alternate, a left frame takes the pair's time, a right one none), `test_vr_input_map` (each controller input's game button or stick: dead zone, d-pad chord, menu tap and hold, snap turning), `test_vr_melee` (slashes and stabs against slow turns, reaches, twists, returns to guard, tracking loss; the easy profile; the shield gesture) and `test_fake_xr_session` (fake_xr's lifecycle and its errors, driven like the loader does) |
| `make test-vr-vulkan` | a Vulkan driver (any GPU; Mesa's lavapipe in CI) | `test_fake_xr_vulkan`: the instance and device through XR_KHR_vulkan_enable2, a swapchain, frames cleared to known colours and checked in fake_xr's captures. Says "skipped" without a Vulkan driver |
| `make test-vr-loader` | the system's OpenXR loader | `test_fake_xr_loader` (a whole session through the loader) and `test_vr_init` (VR off, a runtime, no runtime) |
| `python tools/vr/test_vr_game.py --save <scratch save> --out <dir>` | your game files, a `VR=1` build, a GPU | the game against fake_xr: `cinema` (session focused, frames keep coming, the window and swapchain in the headset's shape, the headset image lit and within a few levels of the window's own screenshot), `headtrack` (mono: fake_xr turns the head 30 deg, looks up 20 deg, steps 50 cm right; the drawn view, read by `camera` from the TAA matrix, follows each to within 2 deg / 10 cm and comes back), `stereo` (one headset frame a pair; the right eye's picture is the left one moved left, never up or down, with eyes 25 cm apart), `hud` (the headset gets the eyes and a HUD layer; the HUD layer is see-through but for the HUD, and where it has the HUD the eyes' pictures show the world), `menus` (the pause menu opened and closed 20 times, `--menu-rounds`: each time the eyes come back), `controllers` (fake_xr's Touch right stick turns the game's camera, its left stick walks Link), `snap` (a push right turns the view 45 deg, one step however long it is held), `motion` (fake_xr's hands: a cut is one slash, a thrust one stab, the slow returns none, the left hand raised guards), `lost` and `exit` (VR stops, the game goes on flat) |

`VR_TEST_VERBOSE=1` prints each part of a C test as it starts. `SAVAGEWIND_VR_LOG_CAMERA=<n>` logs every n-th camera
update: the eye, the head, the game's camera and the view made of them. `SAVAGEWIND_VR_TRACE=<n>`: the first n of the
game's clock, camera, projection and present calls in play, and the queue thread's frames, in order. fake_xr's captures read each image back and stall the
game for about a second each time: keep `FAKE_XR_CAPTURE_EVERY` high when measuring. `.github/workflows/vr.yml` runs the first three on Linux
and Windows, and builds the whole app with `VR=1` on Windows. The game test never runs in public CI (it needs your own
game files).

On Windows outside MSYS2, a `libstdc++-6.dll` of another MinGW earlier on `PATH` (Git for Windows has one) keeps a
loader built with a newer GCC from starting (exit code 127): put the compiler's own `bin` first. GitHub's Windows runners
are elevated, and the loader ignores `XR_RUNTIME_JSON` in an elevated process: the CI registers fake_xr in the registry
instead.

### fake_xr

A small OpenXR runtime (`build/vr/fake_xr.dll` / `.so` and `build/vr/fake_xr.json`, `make fake-xr`). Any OpenXR app,
the game included, uses it with `XR_RUNTIME_JSON=build/vr/fake_xr.json`. It follows the session lifecycle strictly, so
an app that gets it wrong fails here as on a real runtime, and its time is deterministic (frame n is shown at a fixed
base plus n periods; it never waits, so the game runs at its own frame rate). What it reads
(`tools/vr/fake_xr/fake_xr.h`):

| Variable | |
|---|---|
| `FAKE_XR_SCRIPT` | head poses and events over time: `<t> head x y z yaw pitch roll`, the hands' tracks `<t> hand left\|right x y z yaw pitch roll` (interpolated, with velocities), `<t> unfocus`, `focus`, `exit`, `lose`; the Touch controllers: `<t> button <path> 0\|1`, `<t> value <path> <v>`, `<t> stick <path> <x> <y>` (paths as `/user/hand/right/input/a/click`) |
| `FAKE_XR_LOG` | one line per state change, per `xrEndFrame` (layer count and kinds) and per capture (mean colour, lit share, the share of pixels at least half opaque) |
| `FAKE_XR_CAPTURE` | a folder for PPM captures of every layer's image (and a PGM of its alpha for a layer blended by alpha) |
| `FAKE_XR_CAPTURE_EVERY` | every n-th ended frame (30 with `FAKE_XR_CAPTURE`) |
| `FAKE_XR_HZ` | display rate (90) |
| `FAKE_XR_IPD` | eye distance in metres (0.063) |
| `FAKE_XR_GPU` | the GPU it names for the headset (default: the first discrete one) |

It has `XR_MND_headless` and `XR_KHR_vulkan_enable2`, and one pair of controllers: the Oculus Touch profile, with the
actions an app binds to it (bindings before attaching, paths the profile has, states read through their type, active
only when synced in focus). With Vulkan it checks what a real runtime would: the
requirements call before the session, the session on the device it named, swapchain images acquired, waited and
released in order, layers that name a released image and a rectangle inside it.
