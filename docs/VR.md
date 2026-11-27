# VR mode (work in progress)

*En español: [vr/README.es.md](vr/README.es.md) (a guide for players; this page, the technical reference, is in English).*

A PC-VR mode for Savage Wind through OpenXR: stereo, 6DOF head tracking, then motion controls. It is built only with
`make VR=1`; VR release packages start in VR by default, and `SAVAGEWIND_VR=0` starts flat. A build without `VR=1` has
none of it. Development builds stay flat unless `SAVAGEWIND_VR=1` is set.
The plan: [VR-PLAN.md](VR-PLAN.md). Every feature with its state and what the automated tests and a person in a
headset have checked: [vr/FEATURES.md](vr/FEATURES.md). The log: [vr/DEVLOG.md](vr/DEVLOG.md).

## Status

| Phase | What | State |
|---|---|---|
| 0 | Build flag, OpenXR loader, pose / projection math, the `fake_xr` test runtime, CI | done |
| 1 | OpenXR session on the game's Vulkan device; the flat game on a virtual screen in the headset | done |
| 2 | Head tracking (one view), the game's projection from the headset's field of view | done |
| 3 | Stereo: a frame per eye, the game's clock once a pair | done |
| 4 | HUD apart from the scene, menus on the virtual screen | done |
| 5 | VR controllers as the game's controller | done |
| 6 | Comfort: snap / smooth / head-only turning, its speed, roomscale, height, HUD placement, cutscene modes, vignette | done |
| 7 | Link's arms follow the hands | done in first person: two-bone IK on his skeleton (and his armor's); his head hidden |
| 8 | Motion combat | done: slashes, stabs and the shield by gesture (BetterVR's analysis); the weapon hits where the hand holds it, whenever it is swung, harder the faster; both hands on large swords and spears; the bow aimed by hand with its arc and its string; Magnesis by hand; throwing a lifted object with the hand |
| 9 | Performance | per-eye frustum and TAA; the sun's shadow maps once a pair is there but off by default (it flickered) |
| 10 | Release | the installer, the Windows zip and the Linux AppImage (`make appimage VR=1`, `.github/workflows/vr-release.yml`), the Options > VR page |

Upstream: Savage Wind's `test` branch up to 17f715d (2026-11-19: Alpha 6, DLSS fetched at run time, FSR 3 / 4, XeSS,
the light TAA, HDR game mode, lower requirements, macOS on Vulkan) is merged in. With VR on, the window stays SDR
whatever Video > HDR says (the headset takes the window's SDR image); the light TAA keeps each eye's history in
stereo, as the full one does. The temporal upscalers (DLSS, FSR 3 / 4, XeSS) and frame generation are not tested in
the headset yet: leave Video > Upscaler off in VR for now.

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
For each controller it also reports `left_hand_active` / `right_hand_active` (OpenXR pose action bound and active),
`*_hand_valid` (position and orientation valid) and `*_hand_tracked` (both actively tracked). A focused session and
`controllers: true` only mean that actions synchronized; they do not establish that either grip has a tracked pose.
These fields report the real controllers even with `SAVAGEWIND_VR_SIM_HANDS` set.

First person (`SAVAGEWIND_VR_VIEW=first`, the default, as in BetterVR): in play the neutral head is put in Link's head
(his real position, 1.55 m up and 0.22 m ahead), looking the game camera's way; the head's turn and movement go on from
there, the right stick still turns the heading and the left stick walks Link the way the view looks (the game walks him
relative to the view it draws). His head is hidden (face, hair, hat: `runtime/vr/vr_skel.c`, as BetterVR's
skeleton.cpp) and his arms follow the controllers: each wrist is solved onto its controller's grip by two-bone IK from
his shoulder (`runtime/vr/vr_ik.h`), the elbow bending down and out, the hand turned to the controller with a separate
fixed wrist correction for each side; sleeves and gloves (his armor's own models) follow. Set
`SAVAGEWIND_VR_HAND_ROLL=0` to keep only pointing direction and leave wrist roll to the animation. Both act on his skeleton's world matrices right before the
game makes the skinning matrices (main+0xf978e8; how it was found: [vr/re-notes.md](vr/re-notes.md)).
`SAVAGEWIND_VR_HIDE_HEAD`, `SAVAGEWIND_VR_ARMS` (0: off). `third` keeps the view on the game's camera, as below.

Phase 2: in play (not paused, no event, no title, map or loading screen) the head moves the game's camera
(`runtime/vr/vr_camera.c`, from `rt_lookat` in `runtime/hle/freecam.c`): the game's camera keeps its place and heading
(it still follows Link and turns with the stick), the head adds its turn and its movement since the last recentring and
gives the pitch and roll itself. The world camera's projection (`rt_persp` in `runtime/nvn/output_scale.c`) becomes a
symmetric frustum that holds both eyes' fields, in the window's shape; the window takes the shape of the headset's eye
images at start (`SAVAGEWIND_VR_HEIGHT` lines high, 1440 by default; for the run only), and the frame goes to the headset as a
projection layer with that frustum and the pose the camera was given. One picture for both eyes until phase 3; the HUD
is in it. Menus and loading go back to the virtual screen; events too with `SAVAGEWIND_VR_EVENTS=screen` (by
default they stay in 3D, as BetterVR's table says). `vr recenter` (or the first frame in play) makes the
head's pose the game camera's. Measured with fake_xr (RTX 3090, plateau, uncapped): 211 to 218 fps against 219 to 223
flat with the same settings, the longest frame 6 ms in both.

Phase 3: stereo. In play the game's frames come in pairs, left eye then right eye (`SAVAGEWIND_VR_STEREO=0`: one
picture for both, phase 2). The game's clock takes 95% of both frames' time in the left frame and 5% in the right one,
so both eyes see nearly the same moment (0.5 ms apart at 90 Hz) and the world keeps its speed. (The right frame used
to get 0.001 of a console frame: Link then did not turn to the stick - he ran on the way he faced whatever the stick or
the view - and things at rest drifted; 2% was already enough.) Each eye has its
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
that moves with the head (VIEW space), 2.0 m wide (1.6 m base size at 125%) 1.2 m ahead, a little below the line of sight
(`SAVAGEWIND_VR_HUD_WIDTH`, `SAVAGEWIND_VR_HUD_DISTANCE`; `SAVAGEWIND_VR_HUD=0` leaves it in the eyes' pictures). The
pause menu, the map, events and loading stay on the virtual screen; out of them play comes back to the eyes. Cost:
none measurable (220-225 fps with it).
Labels and markers the game places on the screen (names, lock-on, map pins) go where the eye's line to the thing
crosses that panel, not where a flat screen would have them (the UI's world-to-screen, site `ui_project`,
main+0xe1de34, `vr_ui_project` in runtime/vr/vr_camera.c; `SAVAGEWIND_VR_UI_PROJECT=0`: as the game has them). In
first person the stamina wheel stays under the hearts (its spot, 1.8 m above Link's feet, is in his head there).

Phase 5: the VR controllers are the game's controller (`runtime/vr/vr_input.c`, mapping in `vr_input_map.h`). An
OpenXR action set with bindings for Oculus / Meta Touch, Valve Index, Windows Mixed Reality, HTC Vive and the simple
controller; each poll of the game's input reads them and adds them to the gamepad's and keyboard's (a gamepad keeps
working). `SAVAGEWIND_VR_CONTROLLERS=0` turns them off.

The Quest layout (the default since 2026-11-20, `SAVAGEWIND_VR_CONTROLS=quest`; BetterVR's body slots and gestures
below stay):

| VR controller | Game | VR controller | Game |
|---|---|---|---|
| right A | A (accept, interact; nothing while guarding) | right B | B (cancel, put away; a tap: it never runs) |
| right stick up | jump (X) | right trigger | the right hand's: attack (Y), draw the bow (ZR), the rune (A); held 0.6 s: the weapons' quick menu |
| left trigger | guard / lock on (ZL) and, at the press, a parry (A a moment after); held 0.7 s: the shields' quick menu | right grip (away from the slots) | the bow out (again: put away); held: the bows' quick menu (the bow drawn, then d-pad right; let go: the draw cancelled) |
| left grip (away from the slots) | grab / interact (A); with the bow out, held: the arrows' quick menu | left Y | the rune (L); held: the runes' menu |
| left X | the map (-) | menu button | tap: pause (+); held 0.6 s: Savage Wind's overlay |
| left stick click | crouch; held: run | right stick click | inventory; held: the scope |
| both hands at the mouth + both triggers | whistle | | |

Held past 0.7 s the left trigger opens the shields' menu instead of guarding: for a long guard, raise the left hand
with the shield (the raised-hand guard). `SAVAGEWIND_VR_CONTROLS=bettervr` gives BetterVR's buttons:

BetterVR's controls (`SAVAGEWIND_VR_CONTROLS=bettervr`; `runtime/vr/vr_gestures.h`, ported from BetterVR's controls.cpp):

| VR controller | Game | VR controller | Game |
|---|---|---|---|
| left stick | move; click: crouch, held: run | right stick | camera (snap or smooth); click: inventory, held: scope |
| left trigger | lock on / shield (ZL) | right trigger | use what is in hand: attack, draw the bow, activate the rune (A) |
| left Y | the rune (L); held: the runes' menu | right B | jump (X) |
| left X held | the map | right A | grab / interact (A); held: run (B); climbing or gliding: let go (B); riding: A (with the grips: right A, left B) |
| grips | grab / interact (A), or a body slot's item | menu | the inventory |

In a menu (the pause menu's pages, the map, the start screen, a dialogue) the buttons are the game's by their letters,
as BetterVR's menu bindings: A choose, B back (and speeds up a dialogue's text), X hold, Y sort, the triggers the pages
(L / R), the right stick's click closes the inventory (+) or the map (-), the menu button +; no gestures, the grips do
nothing. A button held when a menu opens or closes is left out until it is let go (the click held for the map does not
close it). With the VR controllers in use (Video > Button icons on Automatic) the game's button prompts show the Quest
Touch controls that do each thing (runtime/nvn/glyphs.c, drawn, nothing of Meta's): in play its A is A, its X (jump)
B, its Y (attack) and ZR the right trigger (RT), its B (run, put away, cancel) L3, its L (the rune) Y, ZL LT, - (the
map) X, + the menu button; in a menu or a dialogue the letters are themselves and L / R the triggers. (BetterVR's
layout: the right A held runs.)

Body slots (a grip pressed and let go over them; held 0.5 s: the quick menu, open while held, the choice taken out on
letting go): the right shoulder, the melee weapon (again: put it away; held: the weapons); the left shoulder, the bow
(held: the shields with a weapon out, else the bows); the left hip, the rune (held: the runes). Both grips at the mouth:
whistle. A grip held 0.5 s at the right hip drops what that hand holds (the right hand its weapon, the left, reaching
across, its bow or shield): the gesture opens that quick menu and uses the quick selector's native drop on the item in hand
(runtime/quick_drop*.c, on for it even with the `quick_drop` setting off; `SAVAGEWIND_VR_DROP=0`: no drop gesture). The right trigger held at the right shoulder with a weapon out throws it when let go. A fast left-hand thrust
while guarding: parry. Each slot reached and each gesture vibrates the controller; each swing that attacks too.
`SAVAGEWIND_VR_LEFT_HANDED=1` swaps the hands' parts (not the sticks). `SAVAGEWIND_VR_CONTROLS=classic` keeps the
earlier mapping:

| VR controller | Game | VR controller | Game |
|---|---|---|---|
| left stick | move | right stick | camera |
| left stick click | crouch | right stick click | look |
| X / Y | X (jump) / Y (attack) | A / B | A / B |
| left grip | L (runes) | right grip | R (throw) |
| left trigger | ZL (shield, lock on) | right trigger | ZR (bow) |
| menu: a tap | + (pause) | menu: held 0.6 s | Savage Wind's overlay (X still opens the map) |
| left grip + right stick | d-pad (weapons, shields, bows, runes) | | |

What Link holds is read from the game: the weapon models at his hands (runtime/vr/vr_skel.c: a sword or a bow at a
hand's Weapon_ bone); a rune out is still tracked from the gestures. With a rune out, a grip held away from the slots
works Magnesis by hand: the hand's rise and fall moves the held object up and down, a push or pull moves it away or
near (BetterVR's magnesis gestures). The game's rumble reaches the controllers (runtime/hle/vibration.c, each Joy-Con
on its hand). During the first minute with Link's arms active, the log records each controller's grip orientation about
every five seconds as `[vr-hands]` lines; attach `savagewind.log` and a game-window capture if a wrist still looks
twisted. What to check in a headset: [vr/CHECKLIST.md](vr/CHECKLIST.md); the development log (what was done, how it was tested, what is left): [vr/DEVLOG.md](vr/DEVLOG.md).

(Index: the left A / B are X / Y and the left trackpad pressed is the menu; WMR and Vive: the touchpads pressed are A
and X, the right menu B, and on the Vive the touchpads are the sticks.)

Phase 8, motion combat (`runtime/vr/vr_melee.h`, `vr_input.c`): the right hand's slashes and stabs are told apart from
the rest of its motion by BetterVR's swing analysis (by Acudofy, ported to C, MIT: `third_party/BetterVR`), and each
one beginning presses the game's attack (Y); the held weapon's actor and hit sensor follow the hand during the swing.
The left hand raised in front of the chest or face holds the guard (ZL, the shield up) when it holds a shield and the right hand a one-handed weapon (BetterVR 1.0.1: with nothing, a bow or a two-handed weapon the raised hand does not lock on). The attack button also remains available.

Phase 6, comfort: `SAVAGEWIND_VR_TURN=snap` makes the right stick's sideways push turn the view one step at once
(`SAVAGEWIND_VR_SNAP_DEGREES`, 45) instead of the game's smooth camera turn; the turn sits on top of the game camera's
heading, so Link walks the way the view looks. `SAVAGEWIND_VR_TURN=head` disables horizontal stick turning in first
person while keeping vertical input for aiming and Magnesis. `vr recenter` puts the view back on the game camera.

Baseline before any VR rendering: [vr/baseline-flat.md](vr/baseline-flat.md) (RTX 3090, 1080p: 167 to 229 fps, the
render thread the limit at 4 to 6 ms a frame).

## Playing it

From a release ([Releases](https://github.com/guideahon/BOTW-VR-RECOMP/releases): `Savage Wind VR.exe` in the Windows
zip, or the Linux AppImage, which starts in VR; [vr/SETUP.md](vr/SETUP.md)), or from source (Savage Wind's own
releases have no VR):

1. Build Savage Wind from source as docs/BUILD-windows.md says, with `make VR=1 -j`, and your own game files in
   `extracted/` (`make gen`, then `make VR=1 -j` again).
2. Start your headset's OpenXR runtime (SteamVR, Meta Quest Link / Air Link, Virtual Desktop's, ALVR through SteamVR,
   WMR) and make it the active OpenXR runtime.
3. Start the game with `SAVAGEWIND_VR=1`:

   ```
   set SAVAGEWIND_VR=1
   build\savagewind.exe extracted --play
   ```

The log says what was found (`[vr]` lines).

Most of the VR mode's options are on the game's own Options screen (pause menu > System > Options > **VR**; also from
the title screen's Options): view, cutscenes, turning (smooth or snap, its angle and speed), walking direction,
left-handed, vibration and its strength, motion combat, easy swings, whether a swing plays Link's attack, hits by
swinging, hits where the hand is, the raised-hand guard, the two-handed grip, the drop gesture, the bow by hand and
its trajectory. They apply at once and are kept in settings.ini (`vr_*` keys: runtime/settings.h `VR_OPTIONS`;
runtime/vr/vr_options.c reads them). An environment variable, if set, wins over its row (for tests and scripts).
Options > VR also has a **Comfort preset** row (BetterVR's Gentle, Balanced and Immersive: turning, the vignette,
cutscenes' black bars, the bow's arc and one hit per swing at once; any change after it reads Custom) and a **Controls
guide** page (each button and gesture of the Quest layout). More controllers are bound: Pico 4 / Neo 3, HTC Vive
Cosmos and Valve's Steam Frame (BetterVR's bindings; a runtime that does not know a profile leaves it out).

All the settings, as environment variables:

| Variable | Default | |
|---|---|---|
| `SAVAGEWIND_VR_VIEW` | first | `first`: the eye in Link's head, as in BetterVR; `third`: on the game's camera behind him |
| `SAVAGEWIND_VR_EYE_HEIGHT`, `_EYE_FORWARD` | 1.55, 0.22 | first person: the eye above Link's feet and ahead of his head (m) |
| `SAVAGEWIND_VR_TURN` | smooth | `snap`: the right stick turns the view in steps; `head`: horizontal turning only by moving your head in first person |
| `SAVAGEWIND_VR_TURN_SPEED` | 1 | smooth turning's speed (BetterVR's slow 0.6, fast 1.5) |
| `SAVAGEWIND_VR_CONTROLS` | quest | `bettervr`: BetterVR's buttons (B jump, A held run, the grips grab); `classic`: the earlier mapping, without body gestures |
| `SAVAGEWIND_VR_LEFT_HANDED` | 0 | `1`: the hands' parts swapped |
| `SAVAGEWIND_VR_WALK` | head | `controller`: the left stick walks the way the left controller points |
| `SAVAGEWIND_VR_RUMBLE`, `_RUMBLE_GAIN` | 1, 1 | the game's rumble on the controllers (`0`: off); its strength |
| `SAVAGEWIND_VR_THIRD_DISTANCE` | 100 | third person: the game camera's distance from Link's head, % of the game's own (BetterVR's ThirdPlayerDistance; the camera may then pass through walls the game kept it from) |
| `SAVAGEWIND_VR_HEIGHT_OFFSET` | 0 | first person: the eye this many cm higher (or lower), for another height or playing seated (BetterVR's PlayerHeightOffset) |
| `SAVAGEWIND_VR_WORLD_SCALE` | 100 | physical head movement and eye positions in the game world, percent (50–150; Options > VR > World scale) |
| `SAVAGEWIND_VR_IPD_SCALE` | 100 | headset-reported eye separation, percent (50–150; Options > VR > Eye separation) |
| `SAVAGEWIND_VR_HUD_FOLLOWS_GAZE` | 1 | `0`: the HUD stays ahead of the body, turned the way the head was when centred (look down at it); else it turns with the head (BetterVR's UiFollowsGaze) |
| `SAVAGEWIND_VR_ONE_HIT` | 1 | `0`: a swing can hit the same enemy again while it lasts, each hit half as strong (BetterVR's OneHitPerSwing off) |
| `SAVAGEWIND_VR_ROOMSCALE` | 1 | first person: walking in the room walks Link (the walk stick pushed toward the head while it is more than 10 cm from his eye; the eye stays at the head meanwhile, following only his steps toward it); when he cannot get there (no 2 cm closer in 0.7 s: a wall, a rock, a current) or the head is over 60 cm off, the view is put back over him and he stops; `0`: off |
| `SAVAGEWIND_VR_TWO_HANDS` | 1 | both hands on a large sword or a spear by the other hand's grip on it; `0`: off |
| `SAVAGEWIND_VR_DROP` | 1 | the drop gesture (a grip held at the right hip); `0`: off |
| `SAVAGEWIND_VR_SWING_HITS` | 1 | first person: the held weapon's attack sensor is on while it is swung, the damage by the swing's power; `0`: only in the game's attacks |
| `SAVAGEWIND_VR_SWING_ATTACK` | 1 | a swing also presses the game's attack (Y: Link's attack, its combo and sound); `0`: the swing only hits (BetterVR's way). Only a swing whose hand really travels (peak 2 m/s for a slash, 1.5 m/s for a stab: a wrist flick is under 1) with a melee weapon in Link's hand presses it or turns the weapon's sensor on; the others count in the `vr` field `swings_ignored` |
| `SAVAGEWIND_VR_WEAPON_HITS` | 1 | first person: the held weapon's actor (its physics, its hits) follows the hand; `0`: the animation's place |
| `SAVAGEWIND_VR_BOW_ARC` | 1 | `0` / `off`: no arc of the shot while the bow is drawn; `dot`: only the aiming dot (10 m out, BetterVR's "only the aiming dot"); `faint`: every third dot (Options > VR > Bow trajectory) |
| `SAVAGEWIND_VR_BOW_STRING` | 1 | first person, the bow drawn: its string's nock bones (String_L_2 / String_R_2 of the bow's model) at the string hand, at most 0.9 m from the grip (BetterVR's hook_PoseBowString); `0`: the draw animation's string |
| `SAVAGEWIND_VR_HAND_THROW` | 1 | carrying something lifted (the player's + 0xc40 bit 17, set by the grab action), the right trigger pressed and let go throws it (R); in first person the game's throw (main+0xc76d60, tools/patches.py `_grab_throw`) gets the hand's fastest velocity of the last 0.1 s, boosted up to 1.8x from 4 to 8 m/s, at most 1.2x the game's (BetterVR's hand throw); `0`: the game's velocity |
| `SAVAGEWIND_VR_CARRY_IN_HANDS` | 1 | first person, carrying something: its model is drawn at the right hand at half size (BetterVR's one-hand list: bombs, small pots, Octorok rocks, letter bottles, Cuccos, skull heads, ice balls) or between the hands at 0.7 (the rest); its physics stays over his head until the throw; `0`: over his head |
| `SAVAGEWIND_VR_NO_SHAKE` | 1 | Y or ZR pressed with no melee weapon / no bow on Link (none of the kind among the weapon models in his hands or on his back) is kept from the game: no "nothing to draw" head shake, the rune not put away (BetterVR's CanDrawFromBack); `0`: passed on |
| `SAVAGEWIND_VR_CLIMB_EYE`, `_CLIMB_BACK` | 1, 0.15 | first person, climbing: move the eye behind Link's head in the direction his body faces, away from the wall; the backoff is 0.15 m by default (0.05–0.5 m), eased in and out over 0.2 s; `0` for `CLIMB_EYE` disables it |
| `SAVAGEWIND_VR_DEADZONE` | 12 | the sticks' dead zone, percent (Options > VR > Stick dead zone) |
| `SAVAGEWIND_VR_ANGULAR_FIX` | auto | `on`: the hands' angular velocity turned back into the tracking space (BetterVR's fix: Meta's Oculus runtime reports it in the controller's frame, tilted 45 degrees); `off`; `auto`: on with the runtime named "Oculus" (Options > VR > Fix controller rotation) |
| `SAVAGEWIND_VR_SLOT_GUIDE` | near | the body slots shown as pale discs (BetterVR's world guide; Options > VR > Body slot guide): `near`, a slot within 35 cm of a hand (larger with the hand over it); `always`; `off` |
| `SAVAGEWIND_VR_ARROW_ON_BOW` | 1 | first person: the nocked arrow laid on the line from the string hand to the bow hand while drawing (its model, not only its flight); `0`: as the string hand's wrist turns it. `SAVAGEWIND_VR_LOG_ARROWS=1` logs its angle |
| `SAVAGEWIND_VR_BOW_AIM` | hand | first person: arrows fly along the line from the string hand to the bow hand; `head`: where the view looks (the game's aim) |
| `SAVAGEWIND_VR_RUNE_AIM` | (ignored) | the runes are aimed with the reticle (the head): while one is aimed each eye's field is symmetric and Link's arms are left to the game, since Magnesis takes its target along the picture's centre and Link's hand (2026-11-20). `SAVAGEWIND_VR_KEEP_VIEW` (1): in first person the game camera's own fast turns (ZL's recentre) do not turn the view; `0` off. `SAVAGEWIND_VR_PAIR_SECOND` (0.5): the right frame's share of a stereo pair's game time. `SAVAGEWIND_VR_MAGNESIS_HAND` (1): while Magnesis holds something it follows the main hand's moves since the grab, scaled by its distance (0.6 m to a side: a radian round Link; up: as high as it is far; runtime/vr/vr_magnesis.c, `SAVAGEWIND_VR_TRACE_MAGNESIS=1` logs it), the right stick only nearer / further, the game camera's turns after it kept from the view; `0`: the game's controls |
| `SAVAGEWIND_VR_ARMS`, `SAVAGEWIND_VR_HIDE_HEAD` | on in first person | `0`: Link's arms not moved / his head shown |
| `SAVAGEWIND_VR_HAND_ROLL` | 1 | `0`: keep only the controller's pointing direction; leave wrist roll to the animation |
| `SAVAGEWIND_VR_FOREARM_TWIST` | 1 | `0`: the hand's roll stays at the wrist; otherwise the roll about the forearm goes to the forearm (Wrist_Assist in full, Arm_2 / Elbow by half), so a hand turned palm up rolls the forearm instead of folding at the wrist |
| `SAVAGEWIND_VR_ELBOW_FOLLOW` | 3 | how strongly the elbow goes where the controller's turn puts the forearm (`0`: always down and out) |
| `SAVAGEWIND_VR_ELBOW_DROP` | 0.10 | m the elbow stays at least below the hand and the shoulder (no raised "chicken wing" elbows across the view) |
| `SAVAGEWIND_VR_DOF` | 0 | `1`: depth of field as Video > Depth of field says; otherwise it is off in the headset's view of play (BetterVR 1.0.1) |
| `SAVAGEWIND_VR_LENS_FLARE` | 0 | `1`: the sun's lens flares as Video > Lens flare says; otherwise off in the headset's view of play (BetterVR 1.0.1) |
| `SAVAGEWIND_VR_SIM_HANDS` | off | (tests on a real headset) a file of hand poses relative to the head (`hand left x y z yaw pitch roll` / `handq ...`) used instead of the controllers for Link's arms; `tools/vr/handsim.py --runtime-json <runtime json>` writes it. The gestures and motion combat still read the real controllers |
| `SAVAGEWIND_VR_BODY_FOLLOW` | 1 | first person: Link's body (with his armor and the weapons on his back) turned to where the head looks, about where he stands (BetterVR's Skl_Root override), not while he climbs, swims, glides, rides or tumbles (the player's movement flags, `vr` reports them as `move_flags`); running sideways his body faced the run and his arms fell up to 20 cm short of the controllers. `0`: his body as the game turns it |
| `SAVAGEWIND_VR_FRESH_ANCHOR` | 1 | the tracking-to-world anchor the arms, bow and weapons use is carried by how far Link moved since the last camera (BetterVR's fresh camera reference: his skeleton is calculated before the frame's camera); `0`: the last camera's |
| `SAVAGEWIND_VR_HEAD_FOLLOW` | 1 | first person: the eye follows Link's head bone down and up while his movement flags say crouched (0.68 m lower), swimming or riding - not an attack's dip of the head (33 cm, which read as crouching) - smoothed over a quarter second, a walk's bob (under 15 cm) left out; `0`: the eye's height fixed |
| `SAVAGEWIND_VR_SCOPE_3D` | 0 | `1`: the Sheikah Slate's scope in the eyes like play; otherwise it is shown on the virtual screen as the game draws it (its zoom, lens and frame; BetterVR 1.0.1). Detected by its lens's shaders (nvn/vulkan/exec.c `scope_mark`: three shaders drawn only with the scope; two more it draws are also drawn while a rune is aimed, so they are not used) |
| `SAVAGEWIND_LOG_NEW_FS` / `SAVAGEWIND_WATCH_FS` | off | (development) each fragment shader the first time it draws, with the frame / the draws of the listed shader hashes (comma separated), at most a line a second each: how the scope's lens was told apart |
| `SAVAGEWIND_VR_SNAP_DEGREES` | 45 | the step |
| `SAVAGEWIND_VR_HEIGHT` | 1440 | the eye images' height (the width follows the headset's shape); Options > VR > Eye resolution (1080, 1440, 1800, 2160; applied live); with Video > Render scale above 100% the game draws them sharper. In VR the Video page leaves out the rows that do nothing there (fullscreen, monitor, window size, wide-screen fill, HUD at edges, field of view, frame rate, VSync) |
| `SAVAGEWIND_VR_ASYMMETRIC` | 1 | `0`: each eye's picture covers a symmetric field (the wider side mirrored) instead of the eye's own off-centre one |
| `SAVAGEWIND_VR_DEPTH` | off | `1`: each eye also submits the game's scene depth (`XR_KHR_composition_layer_depth`) when the runtime offers its format. Off by default: a Quest 3 run through Virtual Desktop with it on lost the Vulkan device as the world came up (2026-11-19) |
| `SAVAGEWIND_VR_TAA` | 1 | `0`: in stereo, SMAA in place of Anti-aliasing TAA for the run (the earlier behaviour) |
| `SAVAGEWIND_VR_HUD_ONCE` | 1 | `0`: the left eye's frame draws the game's HUD too (by default it stops drawing into the composite's image once its HUD-less copy is made: the eye gets that copy, the HUD layer comes from the right frame) |
| `SAVAGEWIND_VR_SHADOWS_ONCE` | 0 | `1`: the right eye reuses the left eye's sun shadow maps instead of drawing them again (~4% more headset fps). Off by default: the game fits the cascades to each frame's own camera, so the right eye read the left's maps through its own matrices - shadows moved or covering whole objects in that eye (2026-11-25) |
| `SAVAGEWIND_VR_SYNC` | 0 | `1`: (in progress, not for play, [vr/SYNC_STEREO.md](vr/SYNC_STEREO.md)) synchronous stereo: one game frame a pair, its command lists translated again for the right eye with the right eye's camera put into the constant buffers; `SAVAGEWIND_VR_SYNC_CHECK=1` logs the view made from eye/at/up against the game's; `SAVAGEWIND_VR_SYNC_MONO=0` redraws every pass for the right eye; `SAVAGEWIND_VR_SYNC_LOG=1` logs the targets marked and left out; `SAVAGEWIND_VR_SYNC_NOREPLAY=1` (a test) shows the left picture in both eyes; `SAVAGEWIND_VR_SYNC_DUMP=<w>` logs the constants of the marked draws that sample a w x w texture (896: the shadow pass) and writes their GLSL as vrdump_<shader>.glsl in the working directory |
| `SAVAGEWIND_VR_HIDE_WINDOW` | 1 | `0`: keep the game's window on the monitor while the headset session runs (by default it is hidden then, and shown again when the session ends; Options > VR > Hide the window on the monitor) |
| `SAVAGEWIND_VR_EXPERIMENTAL` | 0 | `1`: experimental features may run (Options > VR > Experimental features): today Rendering: Multiview |
| `SAVAGEWIND_VR_RENDER` | auto | How the eyes are drawn, as Options > VR > Rendering (`vr_render_mode`; read at start): `auto` stereo for now (multiview once both eyes match everywhere); `stereo` a game frame per eye; `multiview` both eyes in one pass, experimental - only with `vr_experimental` / `SAVAGEWIND_VR_EXPERIMENTAL=1` (Options > VR > Experimental features), else stereo: some effects can show in one eye only (stereo on a driver without it); `mono` one centred picture for both eyes, no stereoscopic depth (never automatic). The log's `[vr] rendering:` line says which ran. The older variables below win over it |
| `SAVAGEWIND_VR_MV` | (option) | `1` (with `SAVAGEWIND_VR_SYNC=1`): multiview, both eyes drawn in one pass, TAA per eye - 147.5 headset fps against 98.5 in stereo with TAA on the test machine ([vr/PERFORMANCE.md](vr/PERFORMANCE.md)); `0`: stereo; `SAVAGEWIND_VR_MV_SHOW=1` the left eye shows the right layer too, `SAVAGEWIND_VR_MV_DEBUG=1..4` test patches of the right eye's constants (2: zeroed), `SAVAGEWIND_VR_MV_DUMPVS=1` multiview vertex shaders' GLSL |
| `SAVAGEWIND_VK_MV_TEST` | 0 | `1`: (multiview in progress, [vr/PERFORMANCE.md](vr/PERFORMANCE.md)) every vertex and fragment shader the game uses is also translated as its multiview variant and compiled to SPIR-V, unused; `[mv-test]` lines count them and the failures (their GLSL in mvtest_<n>.glsl) |
| `SAVAGEWIND_VR_SYNC_CENSUS` | 0 | `1`: (in progress, [vr/SYNC_STEREO.md](vr/SYNC_STEREO.md)) every 4 s the constant buffers the draws read are searched for the camera's matrices; `[vr-sync] census` lines in the log |
| `SAVAGEWIND_VR_WINDOW` | left | the desktop window in stereo: the left eye only; `both`: each frame as drawn (the window flickers between the eyes) |
| `SAVAGEWIND_VR_STEREO` | 1 | `0`: one picture for both eyes (lighter, no depth; as `SAVAGEWIND_VR_RENDER=mono`) |
| `SAVAGEWIND_VK_NO_MULTIVIEW` | 0 | `1` (a test): run as on a driver without multiview (Automatic and Multiview fall back to stereo) |
| `SAVAGEWIND_VR_HUD` | 1 | `0`: the HUD stays in the eyes' pictures |
| `SAVAGEWIND_VR_UI_PROJECT` | 1 | `0`: labels, markers and the stamina wheel where a flat screen has them, not on the floating HUD panel |
| `SAVAGEWIND_VR_ARM_RIG` | 1 | `0`: the first arm rig (the elbow's hint from the controller's whole turn, no clavicle, the forearm's roll unlimited), to compare |
| `SAVAGEWIND_VR_DEBUG_CAM` | (none) | `<back> <right> <up>` (m): the picture drawn from there looking at the eye, the arms still solved in first person (debugging the rig; `SAVAGEWIND_VR_HIDE_HEAD=0` shows his head) |
| `SAVAGEWIND_VR_LOG_TWIST` | (none) | `1`: the forearm's roll in the log as it changes ([vr-twist]) |
| `SAVAGEWIND_VR_LADDER_TEST` | (none) | `1`: the ladders' stick turn everywhere, his body left as the game turns it (checking its sign on flat ground) |
| `SAVAGEWIND_VR_HUD_WIDTH`, `_DISTANCE` | 1.6, 1.2 | HUD base width and distance in metres; the default Options size is 125%, making the panel 2.0 m wide (the Options page's HUD size and distance win) |
| `SAVAGEWIND_VR_SCREEN_WIDTH`, `_DISTANCE` | 3.0, 2.5 | the virtual screen of menus, events and loading (m) |
| `SAVAGEWIND_VR_VIGNETTE` | off | the comfort vignette (Options > VR): `low`, `medium`, `high`: while a stick walks or turns Link (or a snap turn), the edges of the view go dark, a quad over the eyes' pictures in the head's space; roomscale walking does not trigger it |
| `SAVAGEWIND_VR_PLAY_POSITION` | seated | first person (Options > VR > Play position, BetterVR's): `seated`, the centred head at Link's eyes (1.55 m above his feet); `standing`, the player's real height above the floor (the runtime's STAGE space) is his eyes', so a shorter or crouching player sees from lower |
| `SAVAGEWIND_VR_SHOW_CONTROLLERS` | 1 | `0`: no marker at the controllers (Options > VR > Show the controllers: a 6 cm pale blue disc at each grip, over everything, in menus, on the virtual screen, while loading and in third person; never in first-person play, where Link's arms are the hands) |
| `SAVAGEWIND_VR_CUTSCENE_BARS` | 0 | `1`: events on their own camera (not in first person) on the flat virtual screen, framed as the game frames them with its black bars (BetterVR's UseBlackBarsForCutscenes); first-person ones stay in 3D |
| `SAVAGEWIND_VR_EVENTS` | auto | events (cutscenes): `auto` as BetterVR's table says for each (first person or the event's camera in 3D), `first`, `third` (the event's camera), `screen` (the flat virtual screen) |
| `SAVAGEWIND_VR_CONTROLLERS` | 1 | `0`: the VR controllers are not read (a gamepad only) |
| `SAVAGEWIND_VR_MOTION` | 1 | `0`: no motion combat (slashes, stabs and the raised-hand guard) |
| `SAVAGEWIND_VR_SWING` | normal | `easy`: BetterVR's easier swing profile (slower swings count) |
| `SAVAGEWIND_VR_SHIELD` | 1 | `0`: raising the left hand does not guard |

Stereo needs the game at about twice the headset's rate (180 fps for 90 Hz): lower Video > Render scale, the draw
distance or the shadows if the headset's runtime reports dropped frames, or run the headset at 72 Hz.

Meta Quest 3 through Virtual Desktop: Virtual Desktop Streamer registers its own OpenXR runtime (VDXR) as the active
one; connect the headset in Virtual Desktop first, then start the game from its desktop view. A captured
VirtualDesktopXR 1.1.0 session on Quest 3 offered `XR_KHR_vulkan_enable2` and `XR_KHR_composition_layer_depth`, but did
not list `XR_FB_space_warp` or `XR_FB_foveation`. The build now copies the game's depth image into a per-eye
`XR_KHR_composition_layer_depth` swapchain when its format is offered; fake_xr validates the depth chain for both eyes.
It is opt-in (`SAVAGEWIND_VR_DEPTH=1`): with it on, a Quest 3 run through Virtual Desktop lost the Vulkan device as
the world came up (`device fault: address write invalid 0`), so it waits for a headset run that shows it working. With the headset not connected the game says so and stays flat
(`found no headset (xrGetSystem -35)`). The Quest's controllers are read through the Touch bindings. Measured against
fake_xr on an RTX 3090 (Great Plateau, uncapped, an eye about as wide as high): `SAVAGEWIND_VR_HEIGHT` 1080 gives
about 110 stereo pairs a second, 1440 about 100, 1800 about 90; heavier places run about a quarter slower, so 72 or
80 Hz in Virtual Desktop leaves room. These are fake_xr measurements, not Quest measurements.

Since 2026-11-19 the mod has been played in a Meta Quest 3 through Virtual Desktop (VDXR), RTX 3090: FOCUSED at 90 Hz,
90 stereo pairs a second; what was reported and fixed is in [vr/DEVLOG.md](vr/DEVLOG.md), and what is confirmed in
the headset is the Human validated column of [vr/FEATURES.md](vr/FEATURES.md).

## Not done yet

Against BetterVR, feature by feature: [vr/BETTERVR-GAP.md](vr/BETTERVR-GAP.md). Every feature of this mod, BetterVR
and Savage Wind in one table, with three checks (implemented, validated by the automated tests, validated in a
headset): [vr/FEATURES.md](vr/FEATURES.md).

In the order they matter, with what each needs. "Needs the game's code" means functions of the 1.9.0 code still to be
found (reverse engineering, [vr/re-notes.md](vr/re-notes.md)); "needs a headset" means it can only be judged wearing
one.

**Next: validation in a headset**, row by row of the feature table ([vr/CHECKLIST.md](vr/CHECKLIST.md): test and
report by ID). The tables below are the implementation's history and what each part needed.

**Interaction (phases 7 and 8)**

| What | Needs | State |
|---|---|---|
| Link's arms follow the hands (two-bone IK from the shoulders, BetterVR's skeleton.cpp) | nothing more | done in first person (vr_skel.c, vr_ik.h); each wrist has a fixed per-hand correction; `SAVAGEWIND_VR_HAND_ROLL=0` restores animation roll |
| The weapon in the hand, following it | nothing more: it hangs from Weapon_R, which follows the hand | done with the arms (to be checked in a headset) |
| The weapon hits where it moves, scaled by the swing's power | the game's code: the weapon's attack sensor | where it is: done (the held weapon's actor, so its physics and attack sensor, follows the hand: main+0x1525fb8, `SAVAGEWIND_VR_WEAPON_HITS=0` leaves it to the animation); when: done too, the sensor turns on while the motion analysis sees a swing (main+0x7b1690, armed once a swing, the damage x0.2 for a brush to x1.35 for a full swing, trees cut), off after it; `SAVAGEWIND_VR_SWING_HITS=0` leaves it to the game's attacks |
| Grabbing weapons from the back, throwing, whistling, parrying by gesture | nothing more | done (vr_gestures.h; what is in hand tracked, not read) |
| What is in Link's hands, read from the game | nothing more | done for weapons, bows and shields (the models at his hands); a rune still tracked |
| Magnesis by hand motion | nothing more | done: the held object follows the main hand's moves, scaled by its distance, the right stick pushes or pulls it (vr_magnesis.c; BetterVR's wrist-turned stick was too touchy sideways and barely rose in a Quest 3; checked with the hand simulator); twisting the wrist to spin it is not done |
| Dropping by gesture (a hand at the right hip, held) | the game's code: the weapon drop | done through the quick selector's native drop (the item in hand, found by its equipped flag): weapon and bow checked in the game; shield uses the same path (d-pad left), checked in the unit test. The Sheikah Slate is protected from dropping, as in BetterVR. |
| The game's rumble on the controllers (BetterVR's patch_CTRL_Rumble) | nothing more | done (runtime/hle/vibration.c to vr_input.c) |
| Walking the way the controller points (BetterVR's walking direction) | nothing more | done (`SAVAGEWIND_VR_WALK=controller`) |
| Bow: the right hand draws, the left aims (BetterVR's bow.cpp) | the game's code: the arrow's launch | done: the arrow flies along the line from the string hand to the bow hand (main+0x931590, `SAVAGEWIND_VR_BOW_AIM=head` keeps the head's aim); while the bow is drawn (an arrow nocked, the main hand's trigger held), its arc is drawn as yellow dots on the eyes' pictures (runtime/vr/xr_vulkan.c `arc_draw`: the shape measured from the game's arrows, not the bow's own speed; over the scene, no depth) |
| Runes aimed with the hand | the game's target pick and its CPU checks | changed (2026-11-20): the runes are aimed with the reticle (the head), as BetterVR does; the hand-aimed pick worked for Stasis but not Magnesis, whose CPU side wants the view's centre ([vr/re-notes.md](vr/re-notes.md)); `SAVAGEWIND_VR_RUNE_HAND_DEBUG=1` keeps the hand's pick for research |
| The shield aimed with the hand | the game's guard direction | probably not needed: the game reads the view it draws, so guarding faces the view (feature G14, to check in a headset) |
| Two-handed weapons held with both hands | the weapon's actor (its bind, done for the hits) | done: with a large sword or a spear in the main hand and the other hand empty, that hand's grip pressed within 0.25 m of the weapon's line takes hold until let go; the weapon then turns about the main hand so its blade points at the other hand (its actor's matrix: what is drawn and what hits), that grip does not grab, hits x1.2 (x0.85 with one hand); the other hand's arm reaches its controller as always (not pinned to the hilt) |
| Actors' job-push skips in stereo (BetterVR's ActorJobs fix: LinkTag / EventTag actors, shrine doors) | nothing more | done: the count the game sets to 2 game frames (main+0x1531ffc) is 4 in stereo play, as BetterVR does; `SAVAGEWIND_VR_JOB_SKIP=0` keeps 2 (docs/vr/re-notes.md); to be checked at a shrine door |
| Hands shown when Link's are hidden (menus, first person) | nothing from the game (a layer at the grip pose) | done as markers: a disc at each controller where Link's arms are not the hands (menus, the virtual screen, loading, third person; Options > VR > Show the controllers); no hand model |

**Comfort (phase 6)**

| What | Needs | State |
|---|---|---|
| A vignette while moving or turning | nothing from the game | done: Options > VR > Comfort vignette (off, low, medium, high; `SAVAGEWIND_VR_VIGNETTE`): while a stick walks or turns Link, or a snap turn, the edges darken (a quad in the head's space over the eyes' pictures, runtime/vr/xr_vulkan.c `vignette`); roomscale walking does not trigger it |
| Seated or standing height, the floor at its height | nothing from the game | done: Options > VR > Play position (BetterVR's): Seated (the centred head at Link's eyes) or Standing (your real height above the floor, from the runtime's STAGE space); and BetterVR's height offset (Options > VR > Height, -30..+30 cm) |
| Turning with the head only (no stick turn) | nothing from the game | done: `SAVAGEWIND_VR_TURN=head` / Options > VR > Turning > Head only; vertical stick input remains available |
| First person (BetterVR's FirstPersonMode) | the game's code: hiding Link's head | done but his head (the eye at his position, 1.55 m up; `SAVAGEWIND_VR_VIEW`) |
| Hiding Link's head and hat in first person | nothing more | done (his head bones and head armor squeezed into his chest) |
| No ragdoll view in first person (BetterVR's PreventFirstPersonRagdoll: blocks the large / medium / small damage states) | the player AI's state changes (1.9.0: `DamageS` / `DamageM` / `DamageL`, main+0xce1a44 asks for them through main+0xc64694) | not needed so far: the eye here is Link's position plus the eye's height, always upright (not his head bone), so a knock-back moves the view but never tumbles it; to check in a headset |
| Roomscale: walking in the room walks Link (BetterVR's) | his walking | done the simple way: the walk stick is pushed toward the head (the game walks him, with its collisions) and the neutral head follows him so the eye stays put; BetterVR moves his body itself with its own casts against the world (not ported); option Roomscale |
| Cutscenes in first person or third (BetterVR's event modes) | the event's name (runtime/hle/framepace.c) | done: `SAVAGEWIND_VR_EVENTS` = `auto` (BetterVR's table of 1497 events, runtime/vr/vr_event_table.h from its patch_Settings_Cutscenes.asm: first person or the event's camera; still and animated hands where it says), `first`, `third` (the event's camera in 3D, the head free), `screen` (the virtual screen, as before); first person needs Link in the world (the new game's opening falls back to the event's camera); an event that must run at the console's 30 fps (Master Kohga's entrance, runtime/hle/framepace.c `events_at_30`: it froze at higher rates) keeps its 30 in VR and is shown on the virtual screen meanwhile |
| Smooth locomotion relative to the hand instead of the head | nothing from the game | done (Options > VR > Walking direction) |

**Picture and performance (phase 9)**

| What | Needs | State |
|---|---|---|
| Shadows and other passes that do not depend on the eye drawn once a pair | exec.c: knowing which passes those are | the sun's shadows done but off by default: the right eye's passes into the cascades and their half-size copy are not begun with `SAVAGEWIND_VR_SHADOWS_ONCE=1` (exec.c begin_pass) - it made the left eye's shadows flicker (2026-11-20: 2.8% of its pixels jumping frame to frame, 0.4% without): with Video > Shadow resolution at Savage the GPU time a frame went from 5.29 to 4.38 ms (-17%; the right eye's frames from ~5 to ~3 ms), at Console ~0.3 ms a pair; other passes still twice: a stereo profile (`gpuprof` on two frames, 2026-11-12) leaves the main scene per eye and only small one-draw passes (512x512, 3 x 448x448, 96x96: ~0.3 ms) that might not depend on the eye, unchecked; the costly 64x64 pass of 411 draws (2.4 ms) runs now and then, not each eye |
| An asymmetric frustum per eye (about 15% fewer pixels) | rt_persp_offset's sign and scale worked out | done in stereo: each eye's own off-centre field, widened to the window's shape about its centre (vr_camera.c; the game's projection gets the half-size, output_scale.c rt_persp_offset the centre, vr_persp_offset); against fake_xr's Quest-like eyes the angle each picture covers is 35% smaller, so the same pixels are that much denser (sharper); `SAVAGEWIND_VR_ASYMMETRIC=0`: symmetric |
| Multiview (both eyes in one pass) | the Maxwell shader translator and the passes made view-aware | research |
| Fixed foveation and runtime reprojection with motion vectors (`XR_FB_foveation`, `XR_FB_space_warp`) | exec.c, mvec.c; a headset whose runtime offers these extensions | not started: the captured Quest 3 / VirtualDesktopXR 1.1.0 session did not offer either extension; revisit on a runtime that supports them. `XR_KHR_composition_layer_depth` is implemented separately; its real reprojection effect still needs headset validation |
| TAA in stereo (per-eye history) | mvec.c: a history per eye | done: Anti-aliasing TAA now works in stereo (it was replaced by SMAA): each eye keeps its own history (exec.c mvec_taa, its alternating images 37-44 for the right eye) and its previous frame is its own, two back (mvec.c), the jitter sequence advancing once a pair, and each eye's off-centre offset taken out of the jitter TAA reads from the projection (mvec.c mvec_camera_now: left in, ~125 and ~60 pixels of "jitter" turned TAA's picture black); `SAVAGEWIND_VR_TAA=0`: SMAA as before |
| The desktop window showing one eye, not both in turn (it flickers) | the window image before it is presented | done: each right frame, once copied to the headset, gets the left frame's picture (runtime/vr/xr_vulkan.c window_eye); `SAVAGEWIND_VR_WINDOW=both`: as before |

**Settings and release (phase 10)**

| What | Needs | State |
|---|---|---|
| A VR page in the game's Options (in place of environment variables) | the options table (runtime/ui) and the translations | done (runtime/vr/vr_options.c; the installer writes savagewind.ini) |
| An installer | nothing more | done: `Install BOTW VR.bat` (tools/vr/installer.py: your folders, the tools, the build, the options, a shortcut) |
| A release build of this fork (no source build needed) | packaging; the setup screen | done: `make dist VR=1` (see Building), checked from a clean folder; published as pre-releases on GitHub (vr-2026.11.12; vr-2026.11.21 with the Linux AppImage) |
| Upstream: the generic hooks (projection, present, frame pacing) proposed to Savage Wind | its maintainers' interest | not started |
| macOS | no OpenXR runtime there | not planned |

## Building

The loader is the system's: MSYS2 `mingw-w64-ucrt-x86_64-openxr-sdk`, Debian / Ubuntu `libopenxr-dev`. The headers are
in `third_party/openxr/`.

```
make VR=1 -j
SAVAGEWIND_VR=1 build/savagewind.exe extracted --play
```

`OPENXR_LIB` points at another loader (`make VR=1 OPENXR_LIB="-L/path -lopenxr_loader"`).

On Linux (checked on Ubuntu 24.04, NVIDIA 615, RTX 3090) without SDL3 / FFmpeg / shaderc installed:
`tools/linux/build_deps.sh x86_64` builds them into `build/linux/x86_64/deps` (it needs SDL's X11 / Wayland / audio
`-dev` packages, among them `libxss-dev`; `clang`, `lld`, `libopenxr-dev`). `DEPS` must be a path without spaces (a
symlink will do):

```
make gen VR=1
make VR=1 DEPS=$PWD/build/linux/x86_64/deps -j
LD_LIBRARY_PATH=$PWD/build/linux/x86_64/deps/lib SAVAGEWIND_VR=1 build/savagewind extracted --play
```

With the same `LD_LIBRARY_PATH` every game test below runs there against fake_xr (2026-11-19: `test_vr_game.py`'s
14 scenarios, `test_vr_events.py`'s five modes and the 18 other `test_vr_*.py`, all passing).

### Version

BOTW VR has its own version, apart from Savage Wind's (the recomp it is built on): `runtime/vr/botwvr_version.h`,
`<phase>-<release>` ("alpha-2026.11.26.2", shown as "alpha 2026.11.26.2"). The release is the date and that day's
number, as the tag `vr-<release>`: set `BOTWVR_RELEASE` in the commit the tag is made on (`vr-release.yml` refuses a
tag that does not match). The phase stays alpha until every serious bug is called fixed, then beta. The release files
are `BOTW-VR-<version>-Windows.zip`, `-Linux-x86_64.AppImage` and `-Linux-SteamFrame-aarch64.AppImage`; logs start
with `BOTW VR alpha 2026.11.26.2 (Savage Wind 0.6.0-alpha.6)`.

### Release package (Windows)

`make dist VR=1` is the upstream release (`make dist`, docs/BUILD-windows.md: `make deps-windows`,
`tools/fetch_zig.sh`, `make gen-helper` first) with VR built in. Its zip, `build/dist/BOTW-VR-<version>-Windows.zip`,
holds no game code (the player's own dump is set up and the game code built on their PC at the first start, as
upstream), defaults every game executable to VR, and adds:
- `Savage Wind VR.exe` (tools/vr/vr_launcher.c): the game started from its own folder with `SAVAGEWIND_VR=1`; it
  waits for the game and returns its exit code (a Steam / Virtual Desktop shortcut sees the game's run);
- `savagewind.exe` also starts in VR if launched directly; set `SAVAGEWIND_VR=0` to start it flat;
- `libopenxr_loader.dll` (the ucrt64 one, found as the game's other DLLs are);
- `VR/`: this file, the setup guides and the checklist.
It ships no SW Update: an upstream update would replace the VR build with the flat one. A VR build ignores one left in
the folder by a Savage Wind zip (`post_update_shipped`), so it never offers updates on Windows.

Checked from a clean folder: the zip extracted to `D:\SW VR test` (a path with a space), its first start set up from
a dump (`SAVAGEWIND_LAUNCHER_EXTRACT=<dump>`: copy, game code built with the bundled zig, shaders precompiled), then
`tools/vr/test_vr_game.py --exe "<folder>\Savage Wind VR.exe"` against fake_xr (an absolute `--out`: the launcher
starts the game in its own folder). A checkout path with a space broke `make dist` (DEPS passed as an absolute path,
tools/zig_slim.sh reading zig's dependency lists): both fixed.

### Updates (Linux AppImages)

Savage Wind Configuration checks this repository's releases, never Savage Wind's (`runtime/update_appimage.c`): tags
`vr-<release>`, ordered by date and the day's number (`uc_vr_version_cmp`), and the asset ending
`-Linux-x86_64.AppImage` or `-Linux-SteamFrame-aarch64.AppImage`. Savage Wind's update key is not used. The releases
aren't signed yet, so Update only names the new file and links the releases page. To have it install them, put the
public half of a key of BOTW VR's own in `runtime/vr/botwvr_update_key.h` and upload each AppImage's `.sig`
(`tools/sign_update.py`); unsigned releases are then passed over, as in Savage Wind. `make test-update-core` and
`make test-update-appimage` cover both cases.

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
| `runtime/vr/vr_sync.c` | synchronous stereo, in progress ([vr/SYNC_STEREO.md](vr/SYNC_STEREO.md)): the census of the camera in the constant buffers, each frame's two cameras, the right eye's patch of them (exec.c `vk_sync_replay`, device.c `backend_present`) |
| `runtime/vr/vr_input.c`, `vr_input_map.h` | the controllers: actions, bindings, the game's buttons and sticks, snap turning, the hands' poses |
| `runtime/vr/vr_melee.h` | motion combat: BetterVR's swing and stab analysis, the shield gesture (header only, unit tested) |
| `runtime/vr/vr_control.c` | the `vr` command (`vr recenter` too) |
| `runtime/vr/vr_math.h` | quaternions, poses, the tracking space to game world mapping (`vr_anchor`), recentring, the LookAtCamera eye / at / up of a view, projections from `XrFovf`. Its conventions are at the top of the file |
| `tools/vr/fake_xr/` | `fake_xr`, a scripted OpenXR runtime for tests (below) |
| `tests/vr/`, `tools/vr/test_*` | the tests |

## Tests

| Command | Needs | What |
|---|---|---|
| `make test-vr` | nothing | `test_vr_math` (the math against values worked out by hand), `test_vr_pair` (the stereo pair's state machine in every order of clock and cameras: eyes alternate, a left frame takes the pair's time, a right one none), `test_vr_input_map` (each controller input's game button or stick: dead zone, d-pad chord, menu tap and hold, snap turning), `test_vr_melee` (slashes and stabs against slow turns, reaches, twists, returns to guard, tracking loss; the easy profile; the shield gesture), `test_vr_gestures` (BetterVR's body slots with the head turned or pitched, equip and put away, quick menus held, grab, whistle, throw, the trigger's use, run, the rune's menu, stick clicks, parry), `test_vr_ik` (two-bone IK: targets met, lengths kept, the elbow toward the pole, stretching out of reach, bone matrices turned) and `test_fake_xr_session` (fake_xr's lifecycle and its errors, driven like the loader does) |
| `make test-vr-vulkan` | a Vulkan driver (any GPU; Mesa's lavapipe in CI) | `test_fake_xr_vulkan`: the instance and device through XR_KHR_vulkan_enable2, a swapchain, frames cleared to known colours and checked in fake_xr's captures, and two projection views with depth swapchain images at an independent resolution. Says "skipped" without a Vulkan driver |
| `make test-vr-loader` | the system's OpenXR loader | `test_fake_xr_loader` (a whole session through the loader) and `test_vr_init` (VR off, a runtime, no runtime) |
| `python tools/vr/test_vr_game.py --save <scratch save> --out <dir>` | your game files, a `VR=1` build, a GPU | the game against fake_xr: `cinema` (session focused, frames keep coming, the window and swapchain in the headset's shape, the headset image lit and within a few levels of the window's own screenshot), `headtrack` (mono: fake_xr turns the head 30 deg, looks up 20 deg, steps 50 cm right; the drawn view, read by `camera` from the TAA matrix, follows each to within 2 deg / 10 cm and comes back), `stereo` (one headset frame a pair; the right eye's picture is the left one moved left, with no material up/down shift beyond one 0.005-tangent sample; eyes 25 cm apart; compared as the headset shows them, each placed by the field it was submitted with), `depth` (opt-in `SAVAGEWIND_VR_DEPTH=1`: fake_xr accepts the depth swapchains attached to both projection views while world frames keep coming), `hud` (the headset gets the eyes and a HUD layer; the HUD layer is see-through but for the HUD, and where it has the HUD the eyes' pictures show the world), `menus` (the pause menu opened and closed 20 times, `--menu-rounds`: each time the eyes come back), `controllers` (fake_xr's Touch right stick turns the game's camera, its left stick walks Link), `firstperson` (the eye in Link's head, his head hidden; with the head turned 90 deg the left stick walks the view's way; crouched the eye goes down with his head and back up), `arms` (fake_xr's hands held ahead and moved: each wrist solved onto its controller within 2 cm; a picture from the eye; both controller grip orientations must be logged), `weapon` (the shoulder grab puts a melee weapon in the right hand, read from the game; it follows the controller; a picture), `walkdir` (`SAVAGEWIND_VR_WALK=controller`: the controller turned left walks Link left), `gestures` (third person: the right grip at the right shoulder takes the weapon out and puts it away, the controller vibrates; pictures of each), `snap` (a push right turns the view 45 deg, one step however long it is held), `scope` (the scope up: the virtual screen; put away: the world view again; Stasis aimed stays in the eyes), `motion` (fake_xr's hands: a cut is one slash, a thrust one stab, the slow returns none, the left hand raised with no shield in it does not guard), `lost` and `exit` (VR stops, the game goes on flat) |
| `python tools/vr/test_vr_runes.py --save <scratch save> --out <dir>` | as above | the left-waist gesture selects a rune and the right trigger reaches the game as A, not R or ZR; saves screenshots before and after for visual review |
| `python tools/vr/bench/bench.py --case <name> --exe <exe> --save <scratch save> [--ab N --exe-a A --exe-b B]` | as above, Python with numpy | the headset's and the game's frame rate against fake_xr (no limit), p95, GPU use, freezes; A/B interleaved; method and rules in [vr/PERFORMANCE.md](vr/PERFORMANCE.md) |
| `python tools/vr/bench/stereo_depth.py --case <name> --exe <exe> --save <scratch save> [--yaw 37] [--spin] [--walk]` | as above, numpy and Pillow | both eyes captured with the hands ahead: each patch's shift between the eyes and its depth (the hands ~0.54 m, far ~0 px) |
| `python tools/vr/bench/profile.py --case <name> --exe <exe> --save <scratch save>` | as above | the translating thread's samples by function and line |
| `python tools/vr/handsim.py --save <scratch save> [--out <dir>] [-i] [cmd ...]` | as above | the hand simulator: the game in VR against fake_xr, the head and each hand set while it runs (fake_xr's `FAKE_XR_LIVE` file), positions and rotations: presets (`pose ahead`, `palms_up`, `quest_rest` - the real Quest 3 pose at rest - ...), `hand` / `handq` / `rel`, smooth `move`s, `tap grip right`, `draw` (the weapon out), `sweep` a hand's yaw / pitch / roll with a picture a step; `shot` (the window), `xshot` (each layer fake_xr was given), `arms` (the wrists' distance to the controllers, the wrist's bend and the forearm's roll in degrees) |
| `python tools/vr/test_vr_rune_aim.py --save <scratch save> --out <dir> [--stereo]` | as above | the runes aimed by hand: with L held (Stasis), fake_xr's right hand points ahead, up and down, the head still; the target pick's point follows it (three bands of v, each eye its own in stereo) and reads the sky pointing up (depth 1.0), the ground pointing down; `SAVAGEWIND_VR_RUNE_AIM=head`: the eye's axis (mono: the centre), the hand not followed; the ray's dots drawn by hand only |
| `python tools/vr/test_vr_frustum.py --save <scratch save>` | as above | each eye's own frustum: the left eye's picture, mapped to view angles by the field it was submitted with (fake_xr's `capture ... fov` lines), matches the symmetric image (both blurred, every sample) with a best shift within 4 samples of `(0, 0)` (each run is its own moment and wind: the match moves by a few samples between runs of one build); with the offset sign flipped the best match differs at least 1.4 times as much (the flipped eye is ~54 samples off, past the search; measured 1.7-2.9). The asymmetric image covers 35% less angle; TAA history and jitter are disabled to isolate projection geometry (stereo TAA is tested separately) |
| `python tools/vr/test_vr_taa.py --save <scratch save>` | as above | TAA in stereo with each eye's own off-centre frustum, eyes 25 cm apart: with each eye's history the eyes' pictures are lit (TAA's, smooth) and line up as well as SMAA's; one shared history (`SAVAGEWIND_VR_TAA_SHARED=1`) is reported alongside (a still scene looks alike) |
| `python tools/vr/test_vr_shadows.py --save <scratch save>` | as above | the shadows once a pair: 6 shadow passes a pair left out (3 cascades, 3 copies), none with `SAVAGEWIND_VR_SHADOWS_ONCE=0`, the frame rate not lower (+2% at Console); the right-eye captures kept to compare with the left (same shadows) |
| `python tools/vr/test_vr_vignette.py --save <scratch save>` | as above | the comfort vignette on High: a pushed stick closes it in (level 1.0) and its quad (4 m across, 1 m ahead, the head's space) is in each frame; 1.5 s after the sticks are let go it is gone, roomscale walking or not; Off: no frame has it |
| `python tools/vr/test_vr_markers.py --save <scratch save>` | as above | the controller markers: before play a 6 cm quad in each hand's space at its controller (within 1 cm), none in first-person play, in third-person play yes, with the option off never |
| `python tools/vr/test_vr_standing.py --save <scratch save>` | as above | the play position, LOCAL's origin 1.20 m above the floor (`FAKE_XR_LOCAL_HEIGHT`): Seated draws the eye 1.55 m above Link's feet, Standing (set while playing) 1.20 m |
| `python tools/vr/test_vr_overlay.py --save <scratch save>` | as above | Savage Wind's overlay menu in play: keyboard open/close and a VR menu-button hold open/close; the window's picture on the virtual screen and no eyes' pictures while open; closed, the eyes again |
| `python tools/vr/test_vr_perf_overlay.py --save <scratch save>` | as above | Video > Performance overlay in the headset (BetterVR's "Show in both window and VR"): off, none; full and FPS only, a quad in the head's space up and to the right, 1 m ahead, 0.36 m wide, the panel's shape (the full one taller), its picture the window's panel |
| `python tools/vr/test_vr_window.py --save <scratch save>` | as above | the window on one eye: every right frame of a stereo pair gets the left picture (488 of 488 in 5 s); with `SAVAGEWIND_VR_WINDOW=both` none (the pictures are not compared: the scene moves more than the eyes' parallax) |
| `python tools/vr/test_vr_fixed_window.py --save <scratch save>` | as above | a window the system keeps at another size (Steam Frame standalone, gamescope), as `SAVAGEWIND_WINDOW=1280x720`: with the HUD apart and on the picture, the game still draws at the eyes' size (`[vr] game image 1374x1440 in a 1280x720 window`), fake_xr's eye swapchains are that size, and test_vr_game's stereo scenario passes with the right eye's shift at -3 +-2 samples (as with a headset-shaped window; -6 before the fix) |
| `python tools/vr/test_vr_arm_rig.py --save <scratch save>` | as above | the arm rig (B10) with the hand simulator: ten poses solved within 2 cm; the right wrist turned -150..150 deg moves the elbow under 6 cm a 30 deg step and 12 cm overall; the forearm's roll never steps over 60 deg; overhead the clavicles turn and the elbows rise (`--old`: the first rig, `SAVAGEWIND_VR_ARM_RIG=0`) |
| `python tools/vr/test_vr_eye_res.py --save <scratch save>` | as above | Eye resolution live: `set vr_eye_resolution` 1080, 2160, 0 (1440) in play; each time the game draws that size, fake_xr gets eye swapchains of it, the eyes go on (over 20 world frames a second) |
| `python tools/vr/test_vr_ladder.py --save <scratch save>` | as above | ladders' stick turn (C34) on flat ground with `SAVAGEWIND_VR_LADDER_TEST=1`: the head turned, forward walks along Link's facing (`vr` link_facing), not the view; without it, toward the view |
| `python tools/vr/test_vr_roomscale.py --save <scratch save>` | as above | roomscale: the head steps 50 cm right; Link walks about as far (0.52 m), the eye stays where the head put it and ends above him again; with `vr_roomscale` off he stays and the eye moves alone |
| `python tools/vr/test_vr_head_turn.py --save <scratch save>` | as above | head-only turning: a right-stick push leaves the view's yaw unchanged; turning the tracked head 30 degrees turns the view by 30 degrees |
| `python tools/vr/test_vr_options.py --save <scratch save>` | as above | the VR options apply at once with no variables set: `set vr_view` 1 / 0 leaves first person and comes back, `vr_rumble` off stops a rumble reaching the controller, `vr_snap_turn` makes one push of the stick turn one 45 deg step, `vr_height_offset` 20 lifts the eye 20 cm, `vr_hud_distance` / `_size` move and grow the HUD layer, and with `vr_hud_follows_gaze` off it stays ahead of the body when the head turns (fake_xr's `FAKE_XR_LOG_QUADS`) (their persistence: `make test-pc-controls`, test_pc_settings) |
| `python tools/vr/test_vr_events.py [--modes auto first screen]` | your game files, a `VR=1` build, a GPU | events: a new game from an empty save (its opening, Demo169_0): `auto` gives the headset the eyes in 3D on the event's camera; `first` the same while Link is not in the world yet (first person only with him there); `screen` only the virtual screen; `bars` (auto with `SAVAGEWIND_VR_CUTSCENE_BARS=1`) this camera event on the virtual screen |
| `python tools/vr/test_vr_two_hands.py --save <scratch save>` | as above | both hands on the large sword: a first run reads the sword's line (SAVAGEWIND_VR_TRACE_TWO_HANDS), a second puts the left hand on it: a grip far from it does not take hold, one on it does, and after the left hand moves the drawn model's blade points at it (0 deg) |
| `python tools/vr/test_vr_drop.py --save <scratch save> [--item bow]` | as above | the drop gesture: the weapon or bow out, its grip held at the right hip; the quick selector's native drop reports the item dropped and done, and its model lies on the ground |
| `python tools/vr/test_vr_swing_hits.py --save <scratch save>` | as above | hits by swinging: the melee weapon out, a fast cut with `SAVAGEWIND_VR_SWING_ATTACK=0`; two runs: with swing hits the weapon's sensor (its actor + 0xd09) is on in the cut and off before and after it, without them never |
| `python tools/vr/test_vr_weapon_hits.py --save <scratch save>` | as above | the weapon's hits at the hand: the melee weapon out, the right controller held far to the right; two runs: with the hits on, the weapon's actor is moved (`weapons_moved`) and its matrices are at the controller, 0.7 m from Link; off, 0.4 m, where the animation holds it |
| `python tools/vr/test_vr_bow.py --save <scratch save>` | as above | the bow aimed by hand: the bow taken out, the hands held 60 deg right of the head, a draw and a shot; the arrow flies within 8 deg of the hands' line and away from the head's; the left eye's pictures (fake_xr captures) show the arc's dots while drawing, not before and not after the shot |

For the case format and the distinction between replayable fake-runtime evidence and headset evidence, see
[Writing repeatable VR regression cases](vr/TESTING.md).

`VR_TEST_VERBOSE=1` prints each part of a C test as it starts. `SAVAGEWIND_VR_LOG_CAMERA=<n>` logs every n-th camera
update: the eye, the head, the game's camera and the view made of them. `SAVAGEWIND_VR_TRACE=<n>`: the first n of the
game's clock, camera, projection and present calls in play, and the queue thread's frames, in order. fake_xr's captures read each image back and stall the
game for about a second each time: keep `FAKE_XR_CAPTURE_EVERY` high when measuring. `.github/workflows/vr.yml` runs the first three on Linux
and Windows, and builds the whole app with `VR=1` on Windows. The game test never runs in public CI (it needs your own
game files). The first-person and stereo scenarios turn periodic captures off so readbacks do not distort their input
and frame-rate checks; stereo requests one tagged pair after its measurement.

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
