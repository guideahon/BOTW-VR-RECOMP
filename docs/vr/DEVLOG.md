# BOTW VR: development log

*En español (with every measurement in full): [DEVLOG.es.md](DEVLOG.es.md)*

What was done, how it was tested and what is left, newest first. Settings and tests: [../VR.md](../VR.md); reverse
engineering: [re-notes.md](re-notes.md); the state of every feature: [FEATURES.md](FEATURES.md).

## 2026-11-26 (26.2's tester: cross-eyed hands while moving them, third-person rotation; both reproduced and fixed)

- **Hands crossed while moving (Stereo, first person):** the head was located once a pair (2026-11-24), the
  controllers every game frame. In a headset the right eye's frame locates them later, and the runtime's prediction for
  the same display time has moved on with new tracking: moving hands were drawn somewhere else in each eye. fake_xr gave
  the same pose for the same time, and the hands checks measured a field (tangents to 0.6) the hands 25 cm under the
  eyes were not in: both hid it. Now the pair's left frame locates the controllers and its right frame draws with
  them (vr_camera.c `vr_pair_hand`; the `vr` command's `pair_hands_kept`). `test_vr_hands_moving.py` (new; fake_xr's
  `FAKE_XR_HAND_DRIFT` makes a later location see a later pose, as a runtime): before, the hands 3-12 samples apart
  between the eyes (up to ~3.4 deg); after, 0. `test_vr_stereo_sync.py --walk` now holds the hands where it measures.
- **Third-person rotation:** with the head turned, the game hands back our own camera with its at point at another
  distance; not recognised as ours, it got the head's turn again: the view swung between the game's way plus the
  head's turn and plus twice it, frame to frame and eye to eye, the right eye taking the doubled one from the pair's
  anchor. The checks had the head ahead (twice 0 is 0). Ours is now told by the eye and the direction, against the
  last eight written: the view off "game + head" in 22.3% of the camera updates before, 0 after.
- **Climbing (P10):** the eye sat 0.38 m over Link's head and 0.32 m back while he hung on the Plateau's tree (the
  `vr` command's new `eye_above_head` / `eye_from_head_flat`): it was put back from his feet at standing height. Now
  from his head bone, 0.12 m off the wall: 0.08 m over, 0.10-0.14 m back (`test_vr_climb.py`).
- **Right A on a wall (Quest layout):** it let go (the game's B, BetterVR's scheme), so "A Take" on an apple dropped
  Link off the tree; in the Quest layout A stays A while climbing, B lets go (`test_vr_gestures`, 180 checks).
- **The regression suite** (`tools/vr/regress.py`, TESTING.md): every check that found a bug, run before each release.

## 2026-11-26 (Options > VR > Performance; a few minutes a mode; the 25.9 tester's report checked)

`tools/vr/bench/quick.py`: one boot a mode (~2 min, fake_xr, the eyes 0 m apart): headset fps, both eyes at two yaws,
mid-turn and right after a menu closes. Each of what the "performance mods" change (Cemu's packs; Savage Wind already
has FPS++'s dynamic frame rate, framepace.c), alone in Stereo (base 104-106 fps, noise +-3%):

| Option | fps | Gain | Picture |
|---|---|---|---|
| Draw distance Potato | 116 | +11% | the same up close; fewer actors far off |
| SMAA in place of TAA | 111 | +6% | a little more aliasing |
| Grass off | 112 | +7% (nothing on top of the two above) | the grass gone: left out |
| Shadows Potato | 103 | 0% | left out |

**Options > VR > Performance** (on by default, `vr_performance`, `SAVAGEWIND_VR_PERFORMANCE=0` off): in the headset
the draw distance is Potato and TAA becomes SMAA (draw_distance.c `level`, output_scale.c `rt_antialias`); Video's
settings are not changed. With the default settings: Stereo 106.5 -> 133.6 headset fps, Multiview 136 -> 169, Mono
185 -> 243; every check passed in each (eyes alike, turning, the menu).

The 25.9 tester's report: third person, the view against the game camera while orbiting, 25.9: left eye 0.4 deg
(max 1.6), right 1.6 (max 3.1); now left 0.0, right 0.8 (max 1.6) - the right eye drawn from the pair's anchor, on
purpose (both eyes from one place: 0 px between them turning and walking, `test_vr_stereo_sync.py --view third
[--walk]`). Eyes after closing the inventory (`test_vr_menu_flicker.py`): no difference in Stereo in 25.9 or now; a
flicker that needs dropped frames is beyond fake_xr: the tester's log and rendering mode are needed. Sync (the hidden
prototype) still draws the eyes differently (9-14%, up to 100% after a menu).

**Savage Wind Configuration before the game, Windows too:** the VR zip has no `sw-manifest.txt` (no SW Update), and on Windows a plain start opened the configuration only in a release with it: the Windows VR build went straight into the headset while the AppImages showed the configuration first. In a VR build it now comes first on every platform (main.c); the source build's `play-vr.bat` and the installer's `Play BOTW VR.bat` no longer pass `--play`. Linux checked with `test_vr_live_launcher.py` (PASS); Windows not run here. The Steam entry "Savage Wind" (`--game`) still goes straight in; "Savage Wind Configuration" opens it.

**BOTW VR's own version:** `runtime/vr/botwvr_version.h` (`alpha-2026.11.26.2`, the start screen: "Version alpha 2026.11.26.2"); it was Savage Wind's 0.6.0-alpha.6. The release files are `BOTW-VR-<version>-...`; the release workflow checks the tag against it. Savage Wind's version is named beside it in the logs.

## 2026-11-26 (both eyes compared in every rendering mode, four places)

`test_vr_eye_match.py` (fake_xr, the eyes 0 m apart: both pictures must be one; the part differing by more than 24
levels) at Kakariko (chimneys' smoke, torches), Hebra Tower (snow, far view), Gerudo Town (bright sand, torches) and
Lurelin (sea, torches), four ways each:

| Mode | Kakariko | Hebra Tower | Gerudo Town | Lurelin |
|---|---|---|---|---|
| Stereo (default) | 2.3-3.8% | 0.3-1.2% | 0.0-3.9% | 1.9-2.7%, **22% at 90 deg** |
| Multiview | 0.00-0.16% | 0.00% | 0.00% | 0.00-0.02% |
| Sync (prototype, `SAVAGEWIND_VR_SYNC=1`) | up to 97% | up to 24% | up to 69% | up to 94% |

- **Multiview passes everywhere**, also at night in the rain at Kakariko (0.00-0.02%), and the hands while walking
  sideways are the same in both eyes (difference 0.0). It is now the mode where both eyes see one world.
- **Stereo's differences are the torches' light**: each eye is a different game frame and a fire's light flickers and
  moves between frames, so one eye sees a rock or a wall lit orange and the other not (Lurelin's shrine statue, Gerudo
  Town's walls). Inherent to a game frame per eye; in a headset, a light that shimmers differently in each eye near
  fires. Multiview does not have it.
- **Sync (the prototype, not in the menu)** loses much of the terrain in the right eye: so in a build from before this
  week's changes too (`bin/sync3`). Giving its right pass the whole constant block (as multiview's terrain fix) helped
  at two of four yaws at Lurelin; the rest is left: multiview replaces it.
- Not a bug: the **red lines over the far terrain** seen from Hebra Tower and the Great Plateau are the Divine Beasts'
  beams at Hyrule Castle (the test save is at 100%).
- **Performance** (`tools/vr/bench/bench.py`, fake_xr uncapped, Great Plateau, RTX 3090, Ryzen 9 9950X3D, 3 runs each):
  Stereo **95.9** headset fps (p95 pair 11.2 ms); Multiview **130.7** (pair p95 8.2 ms), **+36%**; Multiview without the
  union field (`SAVAGEWIND_VR_SYNC_UNION_FOV=0`) 136.0: the right eye's complete edge costs ~4%. GPU at 59-65%: the CPU
  (the game's thread) is the limit in both.
- **Multiview applies Video > Colour and Sharpening** (per eye: each eye's layer graded / sharpened and written back,
  as TAA per eye does; `exec.c mv_scene_post`). Automatic no longer falls back to Stereo for them; only the Upscaler
  (it changes the scene's size) still does. Kakariko with saturation 150, contrast 120, temperature 30 and sharpening
  60: eyes 0.05-0.21% apart, the picture graded as in Stereo. (A first try pushed 8 bytes of the grade's 64 - a
  pointer's `sizeof` - and the eyes came out grey.)
- `SAVAGEWIND_TEST_SETTINGS="key = value;..."` (tests): more settings lines for `test_vr_game.py`'s runs.

- `test_vr_eye_match.py --mode sync` (new); the test no longer turned multiview off (it set `SAVAGEWIND_VR_SYNC=0` for
  every mode for a while).

## 2026-11-25 (multiview in a Quest 3: the right eye that got the left eye's picture; the stretched HUD)

- **The player's report** (Quest 3 over Virtual Desktop, the Testing shortcut, Multiview): the left eye "full of
  movement", the eyes different, the HUD stretched. Measured in the real headset with the new
  `SAVAGEWIND_VR_DUMP_EYES=<s>` (every so many seconds, the pictures each eye is given for 4 presents in a row, as
  `vrdump_<burst>_<k>_eye<e>.png` in the working folder) and per-frame counters in its log line (camera blocks patched
  for the right eye, unpatched, reused). Three faults, one after another:
  1. **The present queue out of step**: a frame's two presents (left, right) were taken from the queue as two independent
     entries; a dropped entry put every later frame one entry off (the order seen: L L R L) and the eyes swapped poses.
     Now the right eye is taken only as the second present of its own pair (`vr_present_take_right`), orphans are
     dropped, and a full queue drops whole pairs.
  2. **Several head poses in one frame**: the game sets its cameras more than once a frame, each located again; the
     right eye's camera, made from one of them, missed the frame's constants. Now every camera of a frame uses the
     frame's first location (vr_camera.c).
  3. **A third of the frames drawn with a camera no record had** (pairs with the right eye exactly the left one: 0.0%
     difference, and 0 of ~485 camera blocks patched in those frames; fake_xr never shows it, even with
     `FAKE_XR_HEAD_SPIN`). The records are now one per camera the game makes (64 kept, the last 8 frames searched), and a
     camera block no record matches takes the right eye from **its own view** (three rows of a rotation followed by the
     view-projection): the eyes' offset V_R V_L^-1 is the same for any camera. Checked in fake_xr forcing that way for
     every block (`SAVAGEWIND_VR_SYNC_ADOPT_TEST=1`): the same picture as the matched way. `SAVAGEWIND_VR_SYNC_DIAG=1`
     logs a frame without its camera with its block and the records.
  Before: L vs R per pair 0.0% in ~1/3 of the pairs. After 1+2: still ~1/3. After 3: to confirm in the headset (it
  went to sleep during the test).
- **The HUD stretched up and down in multiview: found and fixed.** The HUD's draws read cbuf 3 the per-eye way
  (multiview's two copies of the block), which returned before `hud_patch` - the 16:9 layout on the eye's taller
  picture - ever ran. The per-eye block is now made from hud_patch's bytes. fake_xr: the HUD layer in multiview is now
  the same as in Stereo (hearts, items, minimap, clock in place).
- The right eye's HUD in the HUD-less path compares the right eye's own layer (vk_image_view already picks the eye's
  layer of a per-eye image).
- **Multiview / synchronous stereo: the right eye's outer edge complete.** One game frame draws both eyes and the game
  culls with its camera, the left eye's: the right eye's right edge, beyond the left eye's field, had holes (trees cut,
  sky through rock). Both eyes now take the field of both together (left eye's left edge, right eye's right edge),
  submitted as drawn: the game culls for both (`SAVAGEWIND_VR_SYNC_UNION_FOV=0`: each eye its own). Quest 3: ~16% wider
  picture for the same pixels. `test_vr_eye_match.py --mode multiview` (Kakariko, chimneys' smoke, eyes 0 m apart):
  0.00-0.18% differing, PASS; `test_vr_stereo_sync.py --mode multiview` PASS.
- **Third person in Stereo (25.7's tester: "the camera rotation is broken like before")**: a pair's left frame kept
  the heading of the frame before (for the hands, first person) but took the game camera's fresh place; in third
  person the game's camera orbits Link, so the view looked a game frame of the orbit off its own place (fake_xr: 1-2
  degrees each pair, more at a headset's game rate). The heading is kept in first person only; the left frame now
  matches the game's camera exactly (0.00 deg). New `test_vr_stereo_sync.py --view third` (PASS) and `--walk` (walking
  sideways with the hands held ahead, the hands compared between the eyes: dx 0 in Stereo, first and third person).
- Not reproduced in fake_xr, asked back to the tester: "hands and Link in third person out of proper scale" (the view
  is at the game's camera, Link in proportion; the controller markers are in the room, by design) and "hands don't
  keep up when moving" (walking sideways the hands are at one place in both eyes; turning, 0 frames apart).

- New `tools/vr/bench/eye_flicker.py` (per-eye change between consecutive frames, the far patch's shift) and
  `common.launch(ini=...)` / `--ini` (settings lines for a bench run).

## 2026-11-25 (the monitor window hidden in VR; the configuration open on Windows; the 100% save)

- **The game's window on the monitor is hidden while the headset session runs** (Options > VR > Hide the window on the
  monitor, on by default; back if the headset disconnects): it is not updated in play and the player did not want a
  stale picture on the desktop. Tried with the window visible: hidden as the session starts, the headset at 90 fps.
- **Savage Wind Configuration kept open while playing** (what the player calls "the Linux launcher") works on Windows
  too: PLAY leaves the configuration open (`--config`) and the game runs in its own process (`--play`), tried with
  fake_xr. It is `launcher_stays_open`, on by default.

## 2026-11-25 (multiview: the terrain back)

- Testing with the "BOTW VR Testing" shortcut (Multiview), the player saw things "lower than they should be" in both
  eyes. In fake_xr **much of the terrain was missing in multiview**, showing what is under it (water, tree bases; things
  floating or sunk). Cause: cbuf 3 was copied per eye cut at 512 rows, and dynamically indexed reads (the terrain's
  patches) ran past it into the other eye's copy. Now the block goes whole, as the game has it, then only the right
  eye's 64 camera rows (`MV_CAM_ROWS`): fixed reads of those rows pick the eye, everything else reads the real data.
  Multiview still 1.42x Stereo in fake_xr.
- Still in multiview: different brightness between the eyes up high (sky, far haze), the stretched HUD, snow in a
  different place in each eye (reported; not reproduced yet).
- The launcher keeps development builds' log (`savagewind.log`; Testing: `savagewind-testing.log`).

## 2026-11-25 (Experimental features: new work does not degrade the default game)

- **Options > VR > Experimental features** (off by default; `SAVAGEWIND_VR_EXPERIMENTAL=1`): experimental paths run only
  with it on. Today that is Rendering: Multiview; with it off a chosen Multiview plays in Stereo, the row says "(needs
  Experimental features)" and the log explains. fake_xr: Multiview without the switch 91 fps (Stereo), with it 141
  (Multiview). Multiview's tests turn it on. 13 languages.
- The rule is written in [TESTING.md](TESTING.md): a change that can make play worse goes in off, and every release says
  what to test and what to ignore.

## 2026-11-25 (the tester's report on 25.4; restart after changing Rendering; what to test in each build)

- amzukk1 on 25.4: with any Rendering choice the eyes come apart while turning, and the hands and Link look wrong. In
  fake_xr, Stereo in 25.4 and now is as in 25.2 (`test_vr_stereo_sync.py --compare`: the same smooth turn, 140 of 140
  pairs with the left eye's anchor, the same eye differences), and Stereo's code did not change beyond metrics. Most
  likely: Rendering applies only after a restart, and coming from Multiview (25.3's default) and switching in play kept
  Multiview. The row says "(after a restart)" now while the choice is not the one running; the log says `[vr]
  rendering: ...`. Still to confirm with the tester's log.
- Each release's notes and the testing guide ([CHECKLIST.md](CHECKLIST.md)) say what to test and what to ignore in that
  build (the tester's request).

## 2026-11-25 (the sun's flares in VR really off; multiview: the stretched HUD)

- **New `test_vr_eye_match.py`**: the eyes 0 m apart (FAKE_XR_IPD=0), so both pictures must match; what differs is
  marked red. At Kakariko at noon it showed **the sun's flare discs in both modes, in a different place in each eye**
  (with flares off): the game also measures the sun on the GPU (fs `6b2c95ce38cf7523` into a 1x1 target, from the
  depth), so answering its occlusion queries with 0 did not hide it all. Its glare (`9fa83457c0203b51`), nine discs
  (`a4484bb9bf4aa4e1`) and streak (`8bda266a8639cee3`) are skipped now with the flare off (VR's default, and Video >
  Lens flare off on the flat screen). Stereo's differing part at 180 degrees: 6.5% -> 2.9% (left: TAA's edges and fire
  flickering between the pair's two frames).
- **Multiview**: the HUD layer comes out stretched up and down (hearts at the very top, long items, the minimap cut, a
  place name twice) and the sky near the sun brighter in the right eye - what the player saw as stretched signs. In
  Stereo, the default, the HUD is right (the interface in its 16:9 band). Multiview stays optional, experimental.
- macOS: `vk_mv_on` without VR too (the link failed); a whole build without VR links.
- The desktop shortcut shows the start screen, as the Windows zip and the AppImage; the Linux branches and repository
  hold nothing that is not in main.

## 2026-11-25 (Automatic back to Stereo: multiview still differs between the eyes in a headset)

- **A headset report** (Quest 3, Windows, the build with Multiview by default): smoke in one eye and not the other, part
  of the ground in one eye only, an enemy's marker moved between the eyes, and item windows stretched up and down. The
  first ones fit what multiview does not do yet: passes mixing per-eye and single targets are drawn for the left eye only
  (`[vk-mv] single target in a per-eye pass` every frame), and effects whose targets the game makes again never become
  per-eye. **Automatic uses Stereo** until multiview passes an eye-equality check in scenes with smoke, particles and
  markers (`VR_AUTO_MULTIVIEW` in vr_camera.c); Multiview stays an explicit, experimental choice (13 languages). The
  stretched windows are still to reproduce (in Stereo too?).
- **macOS** did not build: multiview code (`mv_on`, `vr_sync_camera`) outside the `SW_VR` guard - every build without VR
  failed alike. Empty versions for those builds now (`make runtime-objs` without VR compiles).
- The desktop shortcut skipped the start screen (`--play`, copied from the old launcher): it shows it now, as the
  Windows zip and the Linux AppImage do.

## 2026-11-25 (Options > VR > Rendering: Multiview by default; Mono; multiview's turning fixed)

- **Options > VR > Rendering** (also on Savage Wind Configuration's VR tab; `SAVAGEWIND_VR_RENDER`; after a restart):
  **Automatic** (the default), Stereo, Multiview, Mono. Automatic picks Multiview when the driver has it and Video's
  Upscaler, Sharpening and Colour are off (multiview does not apply them yet), else Stereo. Mono is never picked by
  itself. The log says which ran (`[vr] rendering: ...`); the older variables still win. In all 13 languages.
- **Turning with the stick in multiview went in 0.35 rad (~20 degree) jumps**: stereo's "the left frame keeps the
  heading its skeleton was placed with, the right one takes the new" ran on every multiview frame, which has no right
  frame - the view stood still until 0.35 rad and jumped. Those were also the 18 of ~210 frames with the view against
  the hands' anchor. Now at most 1.9 degree steps and 0 of 219 frames apart; test_vr_stereo_sync.py checks it.
- **A player on 25.2**: flares fixed; arms "not correctly updated when moving". With multiview's variables the jumps
  above explain it; otherwise `vr lead` (new) in a headset says how far the hands trail (fake_xr gives 0 ms).
- Final build, 3 interleaved runs: **the default (Multiview) 144.3 headset fps against 94.5 in Stereo**, pair p95/p99
  7.6/8.1 ms against 11.6/12.2, no freezes. The whole VR suite in the default mode passes but `window`, `eye_res` and
  test_vr_game's `headtrack`, which fail alike on the previous build (78d98a5) and in Stereo. Automatic uses Stereo when
  the eyes' depth is asked for (`SAVAGEWIND_VR_DEPTH=1`: multiview does not attach it yet); `camera` reads the game's
  camera in multiview (TAA's stood still).
- macOS: the disk image workflow takes a vr-* tag to attach to; no OpenXR runtime on macOS, so it plays flat.

## 2026-11-25 (performance audit: the four modes measured; test hooks for what was left open)

- Windows, Ryzen 9 9950X3D (~5.3 GHz effective), RTX 3090 driver 616.92, fake_xr 500 Hz, eyes 1718x1800, dev build of
  78d98a5, Great Plateau, a scratch save, 5 interleaved rounds: **normal stereo 98.5 headset fps, multiview 147.5
  (1.50x), mono 178.6 (1.81x), synchronous stereo alone 79.6 (0.81x)**; GPU 64-69% (41% for the prototype); the
  translating thread busy in every mode: still CPU-bound. Not measured: PGO builds, a real headset, a slower CPU.
- **The numbers that compare between modes**: `state`'s frame counts presents, not game updates (multiview presents
  twice a pair), so the old p95 was bimodal there. New: `vr frametimes [s]` (the times between headset frames: a pair
  each) and bench.py's pair p95/p99, camera updates a pair (6 in normal stereo, 3 with multiview or mono: one game
  update a pair, measured), the mode, CPU and driver in each result. Pair p95/p99: normal stereo 10.4 / 11.0 ms (the
  edge of 90 Hz's 11.1), multiview 7.3 / 8.0, mono 5.8 / 6.1.
- **Fallback**: multiview asked for on a driver without it used to leave the synchronous stereo prototype on; now VR
  falls back to normal stereo (`SAVAGEWIND_VK_NO_MULTIVIEW=1` runs as on such a driver: normal stereo, 100 fps).
- **Mono** (`test_vr_mono.py --control`): both eyes the same bytes, both views submitted from the head's centre (0.0 mm;
  normal stereo ±31.5 mm), one symmetric field (±0.942 rad: ~20% fewer pixels a degree than an eye), one game update a
  pair. Comfort and its use for people with one working eye need a person in a headset.
- **Multiview still fails** `test_vr_stereo_sync.py --mode multiview`: turning with the stick, 18 of ~210 frames have
  the view turned against the hands' anchor (normal stereo 0 of 288) - three runs alike. `test_vr_taa.py --mode
  multiview` passes.
- **Sun flares** (a player's report on 2026-11-25.1: seen though off, flat): `test_vr_sun_flare.py` shows VR's default
  (flares off) with a milky veil over the picture and a thin red diagonal streak that `SAVAGEWIND_VR_LENS_FLARE=1` also
  has; what draws them is not found yet (next: the passes' shaders with `SAVAGEWIND_LOG_NEW_FS`). A lead, not a
  reproduction of the report.

## 2026-11-25 (multiview: TAA per eye; 145 fps against 100 with TAA)

- Each eye gets its own TAA at the scene's composite (its camera from vr_sync.c, its history, its output over its layer
  of the scene). Default settings: 144.9 headset fps against 100.2 in normal stereo, both with TAA; depth unchanged, no
  ghosting seen turning. Next: object motion vectors in multiview passes, the runtime's post effects per eye, a real
  headset. Details in [PERFORMANCE.md](PERFORMANCE.md).

## 2026-11-25 (multiview: the HUD apart per eye; 150 fps with the default settings)

- The HUD-less copy is made of each layer and kept for the right eye's present; multiview puts SMAA in place of TAA by
  itself. Default settings: 150.5 headset fps against 98.3 in normal stereo with TAA and 104.3 with SMAA, the depth and
  the HUD layer as normal stereo's. Next: TAA per layer, a real headset. Details in [PERFORMANCE.md](PERFORMANCE.md).

## 2026-11-25 (multiview working: 150 headset fps against 104)

- `SAVAGEWIND_VR_SYNC=1 SAVAGEWIND_VR_MV=1` (for now with `SAVAGEWIND_VR_TAA=0 SAVAGEWIND_VR_HUD=0`): both eyes drawn in
  one pass from one game frame, 150 headset fps against 104 in normal stereo (fake_xr, RTX 3090, dev builds), the depth
  of the hands and the far patch as normal stereo's. Fixed on the way: per-eye texture slots bound as 2D views of layer
  0 (both eyes the same), mixed passes with a 2D array (the right eye black), a 64 KB per-eye copy per draw, and a
  `getenv` per draw (40% of the translating thread). Next: the HUD apart and TAA per layer. Details in
  [PERFORMANCE.md](PERFORMANCE.md) and [DEVLOG.es.md](DEVLOG.es.md).

## 2026-11-25 (multiview: the ceiling measured, the shaders' multiview variant)

- Mono (one translation per headset frame) runs at 173 headset fps against 102 in stereo: what translating once per
  pair is worth. The shaders' multiview variant (cbuf 3 per eye, read at `gl_ViewIndex`) compiles for all ~400 shaders
  the game used (`SAVAGEWIND_VK_MV_TEST=1`). Details in [PERFORMANCE.md](PERFORMANCE.md).

## 2026-11-25 (why below BetterVR; the measuring tools in the repository)

- In stereo each headset frame is two whole game frames and every draw is translated twice on one thread (~4.9 ms of CPU
  a game frame): ~100 pairs a second at most, the GPU at 70%. BetterVR draws each thing once into two layers (Vulkan
  multiview), once per headset frame. The plan is multiview here; tools and method in [PERFORMANCE.md](PERFORMANCE.md).

## 2026-11-25 (synchronous stereo: the right eye's shadows)

- The shadow pass reads the cascade matrices from view space at rows 42-53 of the context block (S V^-1), the last
  frame's camera at rows 21-34. In the right pass both go to the right eye's view space (M D^-1, D V).
- With SMAA the right eye now matches normal stereo but for one black far tree; about one shot in six still has a
  darker right eye. Details in [DEVLOG.es.md](DEVLOG.es.md) and [SYNC_STEREO.md](SYNC_STEREO.md).

## 2026-11-25 (synchronous stereo: mono passes)

- The left pass marks the targets of draws that read the camera (the exact vec4 rows) or marked textures; the right
  pass runs only draws, clears and copies into marked targets. A texture read by a marked draw and written again later
  is marked too (the shadow map, drawn cascade by cascade): without it the right eye was all in shadow.
- The left eye is as in normal stereo, the hands' depth and the far patch match it; left in the right eye: the sun's
  shadows (the view-space shadow matrix is the left eye's) and one far tree. Headset 85-87 fps against 102. Details in
  [DEVLOG.es.md](DEVLOG.es.md) and [SYNC_STEREO.md](SYNC_STEREO.md).

## 2026-11-25 (synchronous stereo: both eyes from one game frame, a first prototype)

- BetterVR 1.0.1's way begun ([SYNC_STEREO.md](SYNC_STEREO.md)): a census of the camera in the constant buffers
  (cbuf 3: view, view-projection, projection, inverse view, as BetterVR's slots 0-3), and `SAVAGEWIND_VR_SYNC=1` (off
  by default): every game frame is the left eye; at its present the translator runs the frame's lists (copied as they
  ran) again with the right eye's camera patched into the constant buffers and presents that as the right eye.
- The right eye's picture is right in its geometry; the light is wrong in both eyes (the second run redraws passes that
  keep state or do not depend on the eye). Headset 103 -> 87 fps. Next: passes drawn once, per-eye state, the
  view-space shadow matrices and lights. Details in [DEVLOG.es.md](DEVLOG.es.md).

## 2026-11-25 (both eyes at the same game moment: tried, not possible yet)

- Each eye is a whole game frame and the right one is half a step later (5.6 ms of game time at 90 pairs; Link running
  moves ~2.8 cm between the eyes). The camera already looks from the same place in both.
- **No game time in the right frame** (`SAVAGEWIND_VR_PAIR_SECOND=0.000001`): the stick turn went wrong and the game
  crashed in its own code after ~20 s. Like the 5% / 0.001 tries of 2026-11-20: the game divides by the step and counts
  frames. Dropped.
- **The right frame only drawing:** needs the game loop's calc skipped in right frames and the right eye's camera
  matrices (and shadow cascades) rebuilt before the draw; where calc and draw split was not found without the Ghidra
  project. Not tried.
- **BetterVR 1.0.1's way:** one game frame, every draw repeated for both eyes with each eye's camera constants
  (~2200 lines of game hooks plus emulator changes). The right path here too (the translating thread drawing each scene
  pass twice), and half the simulation; weeks of work, not started. Details in [DEVLOG.es.md](DEVLOG.es.md).

## 2026-11-25 (a tester's report: the right eye's shadows, the hands crossed while turning)

- **Shadows once a pair (O13) off by default:** reproduced at Hebra (the right eye's dome dark, shadows moved): the game
  fits the cascades to each frame's camera, so the right eye read the left's maps through its own matrices. ~4-6%
  headset fps. `SAVAGEWIND_VR_SHADOWS_ONCE=1`: on.
- **Hands while turning (B11):** the right frame is drawn with the left's anchor, but the game sometimes calculates
  Link's skeleton after the frame's camera, with the right frame's fresh anchor: turning, the hands showed at ~0.7 m
  instead of 0.54 (2 degrees off in the right eye). `vr_tracking_to_world` now uses the pair's anchor in a right frame:
  0.51-0.54 m turning, the world unchanged. Details in [DEVLOG.es.md](DEVLOG.es.md).

## 2026-11-25 (VR performance, Windows: the HUD once a pair, fewer questions a draw)

Measured as the entry below, with development builds (no release PGO), 4 interleaved runs a case: **103.8 -> 108.0
headset fps (+4%)**, p95 5.65 -> 5.34 ms.
- **The game's HUD only in the right eye's frame (O16):** the left eye gets its frame's HUD-less copy and the HUD layer
  comes from the right frame, so the left frame's draws into the composite's image after that copy (~85) reached
  nothing; they now return at once. 103.8 -> 105.6; the HUD layer the same. `SAVAGEWIND_VR_HUD_ONCE=0`: as before.
- **Whether the HUD goes apart, asked once a frame** instead of twice a draw (`vr_world_view`, ~7000 times a frame), and
  the DOF draw's shader compared first: 105.3 -> 108.0.
- `test_vr_game.py` cinema, hud, menus, stereo, scope, exit pass; firstperson's crouch check fails with the 100% save
  with or without these changes. Details in [DEVLOG.es.md](DEVLOG.es.md).

## 2026-11-25 (Windows: VR performance measured)

The Windows zips of vr-2026.11.24.3 and vr-2026.11.24.6 against fake_xr (`FAKE_XR_HZ=500`, uncapped;
`SAVAGEWIND_VR_STEREO=1`; 1718x1800 eyes; RTX 3090, driver 616.92; the 100% save at the Great Plateau; ~12 s in the
world, 15 s measured). Median of 3 runs (5 for .6 and its variables, interleaved).

| Case | Headset fps | Game fps | p95 | GPU % | Freezes |
|---|---|---|---|---|---|
| .3, hidden window | 88.4 | 176.8 | 6.57 ms | 79 | 0 |
| .6, hidden window | **113.8** | 227.5 | 5.16 ms | 77 | 0 |
| .6 + `SAVAGEWIND_VR_SHADOWS_ONCE=0` | 109.5 | 219.0 | 5.19 ms | 75 | 0 |
| .6 + `SAVAGEWIND_VK_BARRIER_FRAG=0` | 112.6 | 225.2 | 5.18 ms | 77 | 0 |
| .6 + `SAVAGEWIND_VR_MIRROR=1` | 113.6 | 227.3 | 5.15 ms | 75 | 0 |
| .3, visible window | 95.7 | 191.4 | 6.12 ms | 78 | 0 |
| .6, visible window | 112.6 | 225.4 | 5.19 ms | 76 | 0 |

- **.3 -> .6: 88 -> 114 headset fps (+29%) hidden, 96 -> 113 (+18%) visible**, about Linux's gain even though Windows
  already had the geometry in video memory. Shadows once a pair (O13) is worth ~4%, fragment-only barriers (O12) ~1%
  (noise), the mirror (O11) nothing on Windows. No freeze over 100 ms in any run.
- .6's log: `presents on a thread of their own (second queue)` on the RTX 3090 (Ampere).
- Flicker (O13), Hebra tower, 3 pairs of `shot <tag> 2`: the left eye jumps to ~3.5% changed pixels with and without
  `SAVAGEWIND_VR_SHADOWS_ONCE=0` (edges: TAA jitter after each capture's ~7 s stall), the right eye 0.5-1.1%: no
  shadow flicker. Details in [DEVLOG.es.md](DEVLOG.es.md).

## 2026-11-24 (VR performance: the mirror window, the HUD, the pass barriers)

Measured in fake_xr with the player's settings (1718x1800 eyes, RTX 3090, the 100% save at the Great Plateau,
uncapped headset), median of 3 runs: **85 -> ~111 headset fps** (171 -> ~222 game frames a second, one per eye), and
no more 2 s freezes. Where a frame went first: the GPU 94-96% busy (5.7 ms an eye), the translating thread 100%; at
1080-line eyes only 14% faster, so most of the cost is per frame, not per pixel.
- **The mirror window held the headset up.** On X11 `vkQueuePresentKHR` for the window sometimes took 2 s (more often
  while the window is moved, or when it is not shown), and the game's submissions, the headset's included, waited
  behind it: 2 s freezes in half the runs. In play the headset takes the game's own image, so the window is no longer
  acquired nor presented then (`vr_headless_frame`); it is still on the virtual screen (menus, loading), where the
  headset needs it. `SAVAGEWIND_VR_MIRROR=1` shows it in play (at most at the monitor's rate, the left eye's frames left
  alone). Presenting it from a second queue and thread was tried (VR uses them now, not on NVIDIA before Turing): the
  driver still held the submissions. Window shown: 83.2 -> 86.7 fps with the mirror limited, no freezes without it.
- **The HUD alone** (`vr_hud.frag`) is made only from the right eye's frame, which builds the headset's frame: 86.7 ->
  88.8.
- **Pass barriers:** each pass began behind an all-commands barrier, so no pass overlapped the one before (~200 passes
  a frame, most tiny). In VR they now hold back only the fragment work, as on Mali: a pass whose draws' vertex stage
  reads textures or memory, and any pass after copies, dispatches or memory stores, still waits for everything
  (`SAVAGEWIND_VK_BARRIER_FRAG=0`: as before). 88.8 -> 96; captures of the Plateau and Hebra the same.
- **The guest heap's geometry in video memory on Linux.** The Vulkan backend keeps a copy of the heap's geometry
  pages in video memory and refreshes the ones the CPU wrote (its "heap copy"), but only Windows said which pages were
  written (MEM_WRITE_WATCH), so on Linux every vertex and index fetch read system memory across PCIe. Linux 6.7+ now
  tracks them as Wine's GetWriteWatch does: the guest window registered with a userfaultfd in asynchronous
  write-protect mode, PAGEMAP_SCAN listing the written pages and protecting them again (`runtime/platform/linux.c`;
  `SAVAGEWIND_NO_WRITE_WATCH=1` or an older kernel: as before). The copy turns itself on for discrete GPUs: **95.6 ->
  107 headset fps** (GPU 96% -> ~80% busy); flat at 1280x720 the same 225 fps. Captures of the Plateau and Hebra the
  same; `test_vr_game.py` cinema, hud, stereo, menus, firstperson, scope, weapon and exit pass.
- **The right eye's sun shadows taken from the left frame, now by default.** It was off for flickering. The cause: the
  frame's eye was asked of `vr_frame_eye` (the frame the game is setting up), while the passes are translated a frame
  behind, so it was the left eye's shadows that were left out, in every frame (checked: 42 of 42 frames wrong at the
  shadow passes, 0 with the new `vr_translating_eye`, the oldest frame marked for present and not taken yet; when not
  marked yet, the eye after the last presented). TAA's per-eye history used the same wrong eye (consistently, so
  harmless; now 2949 of 2949 right). Flicker measured with `shot <tag> <n>` (a new fake_xr command for frames in a
  row) at Hebra: 0.07-0.16% of pixels changing between frames with or without. Draws into a pass that is not begun now
  return at once. 108 -> ~111-114 fps. `SAVAGEWIND_VR_SHADOWS_ONCE=0`: both frames draw them.
- Tried and left: no barriers for the terrain's G-buffer reads (+2%).
- `test_vr_game.py stereo` now starts facing the sun (the start view changed with the UI cameras' fix): its image
  comparison sometimes finds no shift (dx 0) though the eyes differ (captures checked); hud, menus pass. The costly compute dispatch (16x1x1, 0.3 ms) is a long straight-line shader with few threads:
  the game's own.
- `tools`: none of these are in a test yet; `test_vr_game.py` cinema, hud, stereo, menus, scope and exit pass.

## 2026-11-24 (the item windows and the inventory in VR; a quick menu's stick turn)

- **I04, the item windows small and off to a side** (a chest's item box far up to the left, the inventory's item
  description small inside a slot, the hearts and page names squeezed to the right): two causes. The VR camera hook
  (`vr_camera_lookat`) took the UI layouts' cameras (ui2d: the eye on the z axis looking at the origin, PauseMenuInfo's at
  0, 0, 989; others at 0, 0, 10 in play) for the world's and gave them the head: PauseMenuInfo's root pane was seen at
  1040, -252, -1810 instead of 0, 0, -989 (read from the game's memory, flat against VR). They are left alone now, and
  no longer set the game's view between the world camera's updates. And a UI projection made in play with the tall eye
  picture's shape was drawn in a 16:9 menu: in VR the UI's projections stay 16:9 (`rt_persp`), hud_patch fits them to the
  picture. Checked in fake_xr: the inventory as in flat play; `test_vr_game.py` hud and menus pass. Validated in the
  headset.
- **C35, a quick menu turned the view on closing:** the right stick picks in it, and the game's camera turned with the
  push; the menu is on the virtual screen, where the camera hook returns early, so the whole turn came in the first
  update after it closed. A menu seen up is remembered there, and the camera's turns are kept from the view while it is
  up and for 0.5 s after (`[vr] quick menu: the camera's turn of N deg kept from the view`). Validated in the headset.
- `test_vr_game.py` firstperson: walking back with the head turned now goes the way the real view looks; at the test
  save's spot that is off a ledge, and the crouch check that follows fails (Link falling, not crouched). Crouching on
  flat ground: the eye 1.55 -> 0.87 m.
- **U07, bombs thrown with the right trigger fell at his feet** (round ones always, square ones often): a rune's bomb
  in hand does not go through the grab action, so the carrying flag the trigger's throw looks at (+ 0xc40 bit 17) stayed
  off and the trigger sent A, the game's "Drop". Found by diffing the player object with a bomb out (fake_xr): + 0xd44
  reads "Bomb" while one is held ("None" otherwise; bit 13 of + 0xc48 with it). `vr_player_carrying` counts it: the
  trigger's release throws (R), round and square, both left his hands with nothing at his feet. Back to untested in
  the headset.
- **Then thrown to a side (headset):** the game's own throw goes along Link's facing, and in first person his body is
  turned to the view only as drawn. Without a swing (the hand under 0.8 m/s) the throw's ground part is now turned to
  the view's heading, its speed and rise kept (`[vr] throw: the game's, turned to the view`). fake_xr, the head turned
  90 deg: both bombs turned -88 deg to the view. With a swing the hand's own velocity still decides (I02).

## 2026-11-24 (headset tests: the start screen's controllers, the eyes apart turning the head)

- **The start screen in the headset took no controller input:** the VR controllers only reached the game. The
  configuration now reads them itself (`vr_menu_pad`: its own action sync, none of the game's mapping or gestures, which
  read the game and crashed the screen when tried) and passes them as a gamepad: A activates, B goes back, either stick
  moves, the triggers change tabs. ImGui's SDL backend clears the gamepad flag each frame without an SDL gamepad, so the
  configuration sets it again after it. Checked against fake_xr: A on PLAY starts the game, kept open or not.
- **Turning the head fast, the eyes apart (WiVRn):** each eye's frame located the head again, a frame apart, and the
  display time it asked for could differ by a refresh; a runtime that reprojects the pair as one head pose showed the
  eyes turned apart. The pair's eyes now come from the left frame's one `xrLocateViews` (with its anchor; the same
  `SAVAGEWIND_VR_PAIR_ANCHOR=0` restores both). fake_xr has `FAKE_XR_HEAD_SPIN=<deg/s>` (the head's yaw at the time
  asked for); `test_vr_stereo_sync.py` at 180 deg/s with eyes 0 m apart: before 8 samples (about 2.3 deg) sideways,
  now 0.
- **Turning smoothly with the stick, crossed eyes on near things:** Link's arms and what they hold are placed before the
  frame's camera, with the anchor the last camera left; a left frame taking a fresh heading drew them a game frame of
  the turn off its own view, in that eye only (the right one draws with the left's anchor). A pair's left frame now
  keeps the heading its skeleton used; the right frame takes the fresh one for the next pair. The `vr` command counts
  `anchor_mismatch`: 0 of 249 frames turning, 240 of 250 with `SAVAGEWIND_VR_PAIR_ANCHOR=0`.
- **Charged attacks (Quest layout):** holding the right trigger opened the weapons' menu after 0.6 s, so no charged
  attack. With the weapon already out the trigger now holds Y as long as it is held; with the shield out the left
  trigger is a guard held as long as wanted. Their menus by trigger stay for empty hands; the shoulders' grips open them
  anytime (BetterVR's way). `test_vr_gestures` 168 checks.
- **Then BetterVR's triggers by default:** the triggers held never open a menu, empty hands too (the right one takes the
  weapon out and charges); the quick menus are the shoulders' grips only. `SAVAGEWIND_VR_TRIGGER_MENUS=1` brings the
  trigger menus back for empty hands. The controls guide and C09 / C11 say so, in every language. 171 checks.
- **The quick menus by the shoulders (headset report):** the left shoulder's grip held with no weapon out sent the
  d-pad's right alone, which the game takes as the weapons' menu (the bows' needs the bow drawn: ZR with it); it now
  goes as the bow grip's menu. The no-head-shake filter no longer drops a quick menu's ZR. With snap or head-only
  turning the right stick's sideways push was kept for turning, so nothing could be picked in a quick menu; while one
  is open the stick goes to the game. 173 checks.
- **The frame rate shown in VR:** the overlay counted game frames, one eye each, so it read twice what the headset
  shows. In VR it counts the headset's frames (one a stereo pair; fake_xr: overlay 100.3 fps, headset 100.0 frames/s),
  and the Dynamic resolution aims at the headset's refresh, not the monitor's. A no longer parries while the raised
  left hand guards (Quest layout).

## 2026-11-24 (the configuration kept open while playing)

- **Settings > Keep open while playing** (on by default; not on macOS nor in Steam's Gaming Mode): PLAY lets the
  start screen's headset session go, the program restarts as Savage Wind Configuration alone (`SAVAGEWIND_LIVE=1`, VR
  off; the same process on Linux, so an AppImage stays mounted) and starts the game in a process of its own. Closing the
  configuration leaves the game running (on Linux it waits for it, hidden).
- **Live settings:** the game reads `savagewind.ini` every half second (by its content) and applies each changed setting
  of `set`'s table, as `set` does (`control_settings.c` control_settings_live): the VR page, render scale, upscaler,
  overlay... Not the window's size or fullscreen, nor what needs a restart. The configuration also reads the file again
  when the game's own menu writes it. Its VR page shows the headset's rows while the game runs in VR.
- **A VR tab in Savage Wind Configuration's Settings** (between Video and Audio): the VR page's rows, so they can be
  set before playing or, the configuration kept open, while playing. What is changed there is saved at once (at most
  five times a second) for the game to pick up.
- The control and companion sockets are no longer inherited (a restarted start screen kept the game's port).
- **Checked:** `tools/vr/test_vr_live_launcher.py` (fake_xr): PLAY, the configuration restarted as `--config`, the game
  in its own process with the headset (343 world frames in 2 s), a change to the file applied live (eye separation 85,
  eye resolution 1080), the configuration still open after the game exits. Windows builds but is not run here.

## 2026-11-24 (Steam Frame report: one eye out of step, crossed eyes)

- **The report:** the right eye often looked out of step with the left, and near things gave a crossed-eyes effect.
- **The cause:** each eye is drawn in its own game frame, half a step apart, and each frame rebuilt the anchor (tracking
  space -> the game's world) from that frame's camera: the turn stick's turn, the game camera's swing, Link's step and
  the eye's place ahead of his head all moved between the eyes, while the headset is only told each eye's tracked
  pose. A turn showed as one eye turned against the other; a step as a false depth.
- **The fix** (`vr_camera.c`): the right frame draws with its left frame's anchor, so both eyes look from one place; the
  hands and roomscale keep the fresh anchor. The compositor now gets each eye's tracked pose, not the one moved by the
  Eye separation option. `SAVAGEWIND_VR_PAIR_ANCHOR=0`: as before, to compare. Things that move in the world (Link
  included) still show their half step.
- **Checked** with a new test, `tools/vr/test_vr_stereo_sync.py` (eyes 0 m apart, so both pictures must match): with the
  right stick held, before 2-3 samples (0.6-0.9 deg) sideways between the eyes, now 0; still, 0 both ways.
  `test_vr_eye_res.py` (1080 / 2160 / 1440 live) and `test_vr_fixed_window.py` (1280x720 window) pass; the stereo,
  first-person, HUD, snap, motion, walk and arms scenarios pass (the stereo pair count failed once during capture
  stalls and passed on a rerun). Not yet seen in a headset.

## 2026-11-23 (arm rigging report)

- The player reports that the wrists follow the controllers, but Link's arm poses look unnatural when moving or turning
  the hands (B01: partly implemented, failed in the headset). In `vr_skel.c::solve_arm`, the elbow pole follows hand
  orientation (`SAVAGEWIND_VR_ELBOW_FOLLOW`, default 3), and the wrist target has a 5 cm offset along the controller's
  back. Compare movement and rotation separately in the hand simulator and headset before changing either; this is a
  likely place to investigate, not a confirmed cause.

## 2026-11-23 (live performance on the VR page; ladders)

- **Eye resolution live (V08):** the game draws at the eyes' size whatever the window (the Steam Frame fix), so
  changing the size is enough: a change of Options > VR > Eye resolution (or `set vr_eye_resolution`) applies the
  next frame and the headset's swapchains follow (`tools/vr/test_vr_eye_res.py`: 1080, 2160 and back to 1440 in
  play, the eyes never stopping). The VR options' range for `set` is now 0-4096 (it stopped at 400).
- **Performance on the VR page (V18):** the FPS overlay, Render scale, Upscaler, its mode (Dynamic included: smaller
  when frames are slow) and Anti-aliasing, the Video pages' own rows, next to the eye resolution.
- **Ladders (C34):** on one (the move flags' bit 4 alone) the walk stick is turned from the view's way to Link's, as
  BetterVR's hook_FixLadder, so forward climbs whatever the player looks at. The turn's sign checked on flat ground
  (`tools/vr/test_vr_ladder.py`, `SAVAGEWIND_VR_LADDER_TEST=1`: forward walks along Link, cos 1.00, not the view;
  without it, toward the view, cos 1.00); not yet on a real ladder (none near the test save).
- **G14** is likely not needed: BetterVR sets the guard's direction from Link's facing, and here Link turns to the
  view to guard. To check in the headset.

## 2026-11-23 (B10: the arm rig)

- **The player's report:** in the headset the wrists followed the hands but the arms took unnatural poses. A view of
  Link from outside while the arms are solved in first person (`SAVAGEWIND_VR_DEBUG_CAM="<back> <right> <up>"`) and a
  pose test (`tools/vr/test_vr_arm_rig.py`: ten poses, the right wrist turned -150..150 deg) measured the first rig:
  the elbow's hint followed the controller's whole turn; the elbow was kept under the shoulder; the shoulders never
  moved; the forearm's roll was taken as solved each frame (no continuity, no limit).
- **The new rig** (`SAVAGEWIND_VR_ARM_RIG=0`: the first, to compare): the elbow's hint as BetterVR's (down, out, a
  little back with the body) plus where the controller points from - its direction only, never its roll - smoothed
  over 0.1 s; the elbow may rise over the shoulder with the hands overhead; the clavicles turn with a hand raised or
  reaching far from chest height up (at most 20 deg, never with the arms hanging); the forearm's roll kept continuous
  from frame to frame (a pause keeps it), eased into 110 deg either way and back to the principal angle past 210.
- In the simulator: every pose solved (0.000 m), the elbow within 5 cm over the whole wrist turn, the roll's steps
  under 40 deg (no flip), the clavicles 20 deg and the elbows over the shoulders overhead, no tunic in the view.
  Not yet seen in the headset.

## 2026-11-23 (the arms' reach; HUD markers and the stamina wheel on the floating HUD)

- **P11, swimming:** played at Zora's Domain (`tools/vr/play_water.py`, flags 0x410): the eye picture shows the water
  surface from just above it, Link's head followed down.
- **Left for later:** C34 (ladders: the move flags 0x10 / 0x12 tell a ladder, but the stick's turn on one can't be
  checked without placing Link on a ladder in the simulator) and G14 (the guard aimed by the hand: the game's guard
  direction is not found yet).
- **Arms:** the first-person eye 4 cm further ahead (22 cm) took the hands 4 cm further from Link's shoulders, past the
  12 cm the IK may stretch an arm at full reach (`test_vr_game.py` arms: 2.3 cm short). The stretch is now 16 cm:
  0.000 m short.
- **H10, markers on the floating HUD:** the UI's world-to-screen (1.9.0 main+0xe1dde0, found from sead's
  Projection::cameraPosToScreenPos at main+0xeb44f8, its only caller in play) is patched where it loads the result
  (site `ui_project`, main+0xe1de34). Labels and markers are drawn for the eye's whole field (45-50 degrees each
  side) but the HUD panel covers about 30: they went toward the centre (an enemy 30 degrees off showed at 17). Each
  now goes where the eye's line to it crosses the panel, as BetterVR's hook_PlaceScreenMarker (HUD following the
  gaze; `SAVAGEWIND_VR_UI_PROJECT=0` turns it off). A new site: the game code is built again on the first start.
- **H11, the stamina wheel:** its caller (main+0xe1ad80) asks for a spot 1.8 m above Link's feet, in his head in
  first person; there it stays under the hearts (layout -482, +170, as BetterVR). Checked against fake_xr by the
  counters (`vr`: ui_stamina, ui_projected); the wheel itself not yet seen in a capture (the test save's stamina full).

## 2026-11-22 (Steam Frame: the world pixelated and the eyes not fusing)

- A tester on Steam Frame standalone (the aarch64 AppImage): the world stayed coarse whatever the eye resolution or
  render scale (menus sharp), and the eyes did not fuse (eyestrain). The cause: the system keeps the game's window at
  its own size there, and the game drew the world at that size, with the camera's frustum given the window's shape
  instead of the eye picture's. Now the game draws at the eyes' size whatever the window (device.c, the window shows
  that image scaled to fit), the frustum takes the eye picture's shape (xr_vulkan.c), and with the HUD on the picture
  the eyes get a copy of the game's image, not the window (exec.c).
- Checked against fake_xr with the window held at 1280x720 (`tools/vr/test_vr_fixed_window.py`): the game draws
  1374x1440, the eyes' swapchains are 1374x1440 and the stereo shift is the same as with a headset-shaped window
  (dx -3); before, 1280x720 and the shift doubled (dx -6). Not yet seen on the Frame itself.
- The suite after merging the release's branch: 19 of 20 pass (crouch, the pause menus and `swing_hits` failed once
  and passed on a rerun). Left: `test_vr_game.py` arms, each wrist 2.1-2.3 cm from its controller against a 2 cm
  limit, the same without this change (it passed before the merge).
- The work left uncommitted (Snapdragon GSR, the overlay on a held menu button, 1440-line eyes) reviewed, its eye
  resolution option fixed (picking 1080 stored the 1440 default), translated into the 12 other languages, committed.

## 2026-11-22 (Savage Wind Beta 1 fixes, BetterVR 1.0.2, the PC Maker; release vr-2026.11.22)

- **Upstream:** Savage Wind's "Beta 1 fixes" (2ae4b39: lock-on and menu bindings, the ultrawide map, compute shaders
  reading copies of their constant buffers, the "Magnesis: pull closer" row, a crashed compile retried) merged.
- **BetterVR 1.0.2:** on a horse with nothing in hand the right trigger soothes it (L) instead of drawing a weapon
  (I08; `test_vr_gestures` 162 checks). Its other two changes are not needed here: the rune targets' flicker is a
  Cemu FPS++ patch, and the hair / headgear seen looking up cannot happen with the head hidden by its bones. Its
  material flags (view option bit 4, the selectable Z pre-pass) are the lead for B08, the head's shadow.
- **The PC Maker** (another agent's branch, 098e8ba) merged, with two fixes: under SAVAGEWIND_HIDDEN no error dialog
  holds a test run open (the runtime's and the AppImage launcher's), and the launcher passes TERM / INT / HUP on to
  the game and keeps the terminal's stdin (checked with a stand-in runtime).
- **The VR options in every language:** the 119 texts of Options > VR and its controls guide translated into the 12
  other menu languages; `tools/i18n_check.py` passes again (the Tests workflow had failed on main since 2026-11-21).
- **Release:** `vr-release.yml` also builds the Windows zip (make dist VR=1 on a Windows runner) and runs the x86_64
  AppImage from its FUSE mount before attaching it. Its first builds found three packaging bugs, all fixed: the
  OpenXR loader's libjsoncpp was never bundled (vr-2026.11.21 needs the system's), the Steam Frame AppImage carried
  an x86-64 loader, and the new launcher replaced the AppImage runtime's keepalive fd 3 (the AppImage unmounted
  under the game). The published x86_64 AppImage passes the 16 VR scenarios (82 checks).
- **Tests (Linux, RTX 3090, fake_xr):** every unit test (VR, Maker, bindings); `test_vr_game.py` 16 scenarios on the
  new build (two checks that failed while another game ran on the same GPU pass alone). The published
  vr-2026.11.21 AppImage passes the same 16 scenarios (83 checks) run through its own data folder.

## 2026-11-21 (the feature table as the control list; English and Spanish docs; the Linux build)

- [FEATURES.md](FEATURES.md) / [FEATURES.es.md](FEATURES.es.md) are generated from [features.py](features.py): each
  feature has three checks, implemented, validated by the AI's automated tests, validated by a human in a headset.
  The human column comes only from the player's reports.
- The docs are in English by default with Spanish next to them (`*.es.md`); the outdated ones were brought up to date
  (README, setup, the plan, the gap list, the checklist, now a short "how to test and report").
- Linux: `tools/linux/package.sh` builds the AppImage with VR (`make appimage VR=1`): the OpenXR loader bundled, it
  starts in VR. `.github/workflows/vr-release.yml` builds it and attaches it to a release.

## 2026-11-21 (BetterVR parity, first batch)

- **Throwing by hand:** the grab-throw action's throw (main+0xc76d60, called by its calc_ main+0xc76f54 at the
  animation's throw frame; vtable 0x1cd4e20, found through its ThrowSpeedF / ThrowSpeedY / SquatThrowSpeedF
  parameters) builds the velocity at sp + 4. The new patch site `grab_throw` (0xc76e20) hands it to `vr_grab_throw`:
  with the right trigger let go during a swing, the hand's velocity goes there (its fastest of the last 0.1 s, in the
  world, at most 1.2x the game's). "Carrying" is bit 17 of player + 0xc40 (set by the lift action, main+0xc7721c,
  animation GrabUp; cleared by the throw). Generated and built (233 sites); not run in the game: the test save has
  nothing to lift (`tools/vr/test_vr_throw.py` is ready for a save that does).
- **What you carry, in the hand(s):** in first person the model over his head is drawn at the right hand (small
  things, BetterVR's list) or between the hands, scaled down. Same test limit.
- **The bow's string** follows the drawing hand: the bow's String_L_2 / String_R_2 bones (5 and 9 on
  Weapon_Bow_001). `test_vr_bow` passes; in the eye's capture the string runs from the bow's tip to the hand.
- **No head shake:** Y or ZR with no weapon / bow on Link (the models vr_skel.c sees in his hands and on his back) are
  kept from the game.
- **Options:** comfort presets, stick dead zone (`test_vr_input_map`: 61), the controller rotation fix (BetterVR's,
  for the Oculus runtime), the body slots' guide (with Always, fake_xr gets 5 more discs a frame), the bow arc's modes,
  a Controls guide page. Steam Frame, Pico and Vive Cosmos bindings.
- **Climbing:** the eye sat ahead of Link's head, inside the rock; it now goes 0.3 m behind his head. Climbing the
  Plateau's big rock in the simulator (flags 0x90): `climb_eye` 0.96, the wall seen from outside.

## 2026-11-21 (whistle with both triggers; options that do nothing in VR)

- **Whistle:** both hands within 28 cm of the mouth and both triggers held. With both hands there the triggers do
  nothing else.
- **Options:** the Video rows that do nothing in VR are hidden; new Options > VR > Eye resolution (1080 to 2160, from
  the next start). Checked: it starts in VR and gets past the splash (the start crash fix, with VR really on).

## 2026-11-20 (parry with the trigger, bow and arrow menus, the arrow on the bow, slow events)

- **Slow events:** the event pacing check (`event_at_30`, framepace.c) read an environment variable on every draw
  during events (18.8 ms a frame). Read once now: the new game's wake-up went from 88 to 260-318 fps (fake_xr).
- Left trigger: guard and, at the press, a parry; A while guarding does nothing; B held no longer runs.
- Right grip held: the bows' menu; left grip held with the bow out: the arrows' menu; no arrow on letting go.
- The nocked arrow laid on the line between the hands (66° off before, 0° now in fake_xr).

## 2026-11-20 (Quest layout; the new item window; options in the game)

- The Quest layout became the default (A accept, B cancel, right stick up jump, triggers the hands' items, grips the
  bow / grab, Y rune, X map, menu pause). `SAVAGEWIND_VR_CONTROLS=bettervr` keeps BetterVR's.
- The new item window: not reproducible here; the log now says why an event goes to the virtual screen.

## 2026-11-20 (L3 to run, events at 30 fps, the bow, Quest icons)

- L3 held runs; the scope moved to R3 held. The virtual screen in stereo runs two game frames per headset frame
  (events shown flat went from 45 to 87-110 fps).
- A bow taken out by the gesture stays the bow while nothing else shows in the hands (the trigger had drawn the
  sword). Each right trigger press is logged.
- The game's button prompts drawn as the Quest controller's buttons.

## 2026-11-20 (Magnesis by hand, parry sensitivity)

- Magnesis follows the hand's travel since the grab (0.6 m sideways: a radian round Link; up: as high as it is far),
  the stick only near / far, the game camera's turns kept from the view. Checked in the simulator.
- Parry: a push of 10 cm in 0.2 s at 1 m/s (was never firing at 18 cm / 0.15 s / 1.6 m/s; before that, too eager).

## 2026-11-20 (lock-on turned the view 180°, Magnesis did not grab, things shaking)

- In first person the game camera's own fast turns (lock-on recentre) are kept from the view.
- Runes are aimed with the reticle (the head): Magnesis takes its target along the picture's centre and Link's hand.
- The stereo pair's game time split evenly (95/5 kicked physics constraints: chains and stacks shook).

## 2026-11-20 (flickering shadows in the left eye, attacks and crouching on their own)

- The right eye reusing the left one's shadow maps made the left flicker: off by default.
- An attack now also needs the hand to really travel (2 m/s slash, 1.5 m/s stab) and a melee weapon in hand.
- The eye follows Link's head only while crouched, swimming or riding (an attack's head dip looked like crouching).

## 2026-11-20 (Link did not turn with the view; the stick back walked forward)

- In stereo the right frame got 0.001 of a console frame: Link did not turn to the stick and things drifted. Fixed in
  vr_pair.h (later made 50/50).

## 2026-11-20 (Quest buttons in menus; Link walking on his own)

- In menus the buttons are the game's letters, as BetterVR's menu bindings; no gestures there.
- Roomscale only follows Link's step toward the head and gives up when he cannot get there.

## 2026-11-20 (played in the simulator: whistle, quick menu, throwing, Magnesis, swimming, gliding, riding)

- Swimming (flags 0x410), gliding (0x8002) and riding (0x100000) confirmed on the Switch game. Tools:
  `tools/vr/play_misc.py`, `play_magnesis.py`, `play_water.py`, `play_horse.py`.
- Review of another AI's (Codex) changes: correct.
- The `vr` command reports each hand's pose action active / valid / tracked.

## 2026-11-19 / 20 (first headset runs: Meta Quest 3 through Virtual Desktop, RTX 3090)

- The GPU "device lost" crash fixed (an OpenXR queue lock on every submitting call); FOCUSED, 90 stereo pairs a second.
- The hand simulator (`tools/vr/handsim.py`), the hybrid mode on the real headset, the player's movement flags
  (PlayerInfo + 0x60, + 0xcec).
- Forearm twist, the elbow following the hand, Link's body turned to the view, the eye following his head, the scope
  on the virtual screen, no depth of field or lens flare in VR.
