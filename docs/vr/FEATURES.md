# VR feature table

Every feature of this mod, of BetterVR (the reference, for the Wii U game on Cemu) and of Savage Wind (the PC
recomp it builds on) that matters in VR, with three checks each. This table is the project's control list: it is
generated from [features.py](features.py) (edit the rows there and run `python3 docs/vr/features.py`).
En español: [FEATURES.es.md](FEATURES.es.md).

- **Implemented**: ✅ done, 🟡 partly, ❌ not yet, ➖ not needed here.
- **AI validated**: ✅ checked by an automated test (the fake_xr runtime, the hand simulator, a unit test or a
  profile; never a headset), 🟡 partly, ⬜ not checked yet.
- **Human validated**: what the player saw in the headset. ⬜ not tested yet, ✅ works, ❌ fails, 🟡 partly. Only
  the player fills it: test a row and report it by its ID (for example "C07 fails: B still runs"), with a capture of
  the game window and `savagewind.log` if you can.
- **Origin**: BVR from BetterVR, Mod this mod's own, SW Savage Wind's.

Summary: 193 features; implemented 173, AI validated 145, human validated 4, failing in the headset 1.

## V. Headset and picture

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| V01 | OpenXR session: the game starts in the headset (Virtual Desktop / SteamVR / Link) | BVR | ✅ | ✅ | ✅ | Start the VR shortcut: the game shows in the headset |
| V02 | No headset or runtime: it goes on flat and says why | Mod | ✅ | ✅ | ⬜ | Start with Virtual Desktop not connected: it plays on the monitor and the log says why |
| V03 | Virtual screen for menus, loading and the title | BVR | ✅ | ✅ | ⬜ | Title screen: a floating screen ahead |
| V04 | Real stereo (a picture per eye, no alternating) | BVR | ✅ | ✅ | ⬜ | In play, close one eye then the other: right depth, eyes not swapped |
| V05 | 6DOF: turning, tilting, moving the head | BVR | ✅ | ✅ | ⬜ | Look around, crouch, lean: the view follows with no lag |
| V06 | Each eye's own field of view (asymmetric) | Mod | ✅ | ✅ | ⬜ | The picture sharp to its edges, not stretched looking sideways |
| V07 | The world at normal speed in stereo | Mod | ✅ | ✅ | ⬜ | Grass, water and people move at normal speed |
| V08 | Eye resolution (1080 to 2160; 1440 by default), applied live | Mod | ✅ | ✅ | ⬜ | Options > VR > Eye resolution in play: sharper or faster at once, no restart |
| V09 | Render scale (Video > Advanced) sharpens the picture | SW | ✅ | ⬜ | ⬜ | Set it to 150%: sharper, fewer fps |
| V10 | TAA in stereo (a history per eye) | Mod | ✅ | ✅ | ⬜ | Anti-aliasing TAA: no ghost of one eye in the other, smooth edges |
| V11 | No depth of field or lens flare in VR (the flare's glare, ghost discs and streak skipped: test_vr_eye_match.py at Kakariko at noon) | BVR | ✅ | ✅ | ⬜ | Look at the sun and far away: no flares (no coloured discs), no blur, both eyes alike |
| V12 | The monitor window shows the left eye only | Mod | ✅ | ✅ | ⬜ | Look at the monitor: it does not flicker between eyes |
| V13 | The sun's shadows steady in both eyes | Mod | ✅ | ✅ | ⬜ | Tree shadows do not flicker |
| V14 | Video rows that do nothing in VR hidden | Mod | ✅ | ✅ | ⬜ | Options > Video: no fullscreen, monitor, field of view, frame rate, VSync |
| V15 | Depth for reprojection (SAVAGEWIND_VR_DEPTH=1) | BVR | 🟡 | ✅ | ⬜ | Off by default: with Virtual Desktop the GPU device was lost |
| V16 | The session lost (headset off, Virtual Desktop closed): goes on flat | Mod | ✅ | ✅ | ⬜ | Close Virtual Desktop while playing: the game goes on on the monitor |
| V17 | Snapdragon GSR upscaler (single pass, no history) | Mod | ✅ | ⬜ | ⬜ | Video > Advanced > Upscaler: Snapdragon GSR, a smaller Render scale: sharp edges, no ghosting |
| V18 | Performance rows on the VR page (FPS overlay, render scale, upscaler, its mode or Dynamic, anti-aliasing) | Mod | ✅ | 🟡 | ⬜ | Options > VR: change them in play with the FPS overlay on and watch the frame rate move |

## P. View and camera

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| P01 | First person: the eye in Link's head | BVR | ✅ | ✅ | ⬜ | Options > VR > View: First person |
| P02 | Link's head hidden (face, hair, hat) | BVR | ✅ | ✅ | ⬜ | Look down and around: no inside of his head |
| P03 | Third person (the game's camera) | BVR | ✅ | ✅ | ⬜ | View: Third person; the head moves the look |
| P04 | Third-person camera distance | BVR | ✅ | ✅ | ⬜ | Options > VR > Third-person distance |
| P05 | Recentring the view | BVR | ✅ | ✅ | ⬜ | Command vr recenter, or back from a menu: the view faces Link's way |
| P06 | Crouched, swimming or riding, the eye goes down with his head | Mod | ✅ | ✅ | ⬜ | Crouch (L3): the view goes down; swim; ride |
| P07 | Link's body turns to where you look | BVR | ✅ | ✅ | ⬜ | Look to a side standing: the body follows, the arms still reach your hands |
| P08 | The game camera's fast turns do not turn the view (lock on) | Mod | ✅ | ✅ | ⬜ | Lock on with the left trigger: the view does not jerk |
| P09 | No tumbling view on strong hits (BetterVR: prevent ragdoll) | BVR | ➖ | ➖ | ⬜ | A strong hit: the view moves but never tumbles (here the eye is always upright) |
| P10 | Climbing: the eye off the wall | BVR | ✅ | ✅ | ⬜ | Climb a wall: you never see inside the rock |
| P11 | Swimming: the eye above the water | BVR | ✅ | ✅ | ⬜ | Swim: the eye just above the surface, following Link's head |
| P12 | The scope (Sheikah Slate) on the virtual screen, with its zoom | BVR | ✅ | ✅ | ⬜ | Right stick click held: the scope on the flat screen; let go: the world |
| P13 | Shrine doors and LinkTag actors in stereo | BVR | ✅ | ✅ | ⬜ | Enter a shrine: the door opens normally |
| P14 | Third-person camera without collision where it gets in the way | BVR | 🟡 | ⬜ | ⬜ | Only the distance is done; walls still push the camera |
| P15 | The camera riding a sand seal | BVR | ❌ | ⬜ | ⬜ | Not done |

## B. Body and hands

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| B01 | Link's arms follow the controllers (two-bone IK) | BVR | ✅ | ✅ | ⬜ | Move your hands: Link's are where yours are (the arm poses: B10) |
| B02 | Clothes and armor follow the arms | BVR | ✅ | ✅ | ⬜ | Wear other armor: sleeves and gloves follow |
| B03 | Natural wrist and forearm turn | Mod | ✅ | ✅ | ⬜ | Palm up and down: the forearm turns, the wrist does not fold |
| B04 | Elbows down, no "chicken wings" | Mod | ✅ | ✅ | ⬜ | Hands ahead at chest height: elbows down |
| B05 | Controller markers in menus and third person | Mod | ✅ | ✅ | ⬜ | In a menu: a pale blue disc at each hand. Options > VR > Show the controllers |
| B06 | What Link holds is read from the game | Mod | ✅ | ✅ | ⬜ | Equip a sword, bow, shield: the gestures know what you hold |
| B07 | Torches, Bokoblin arms and branches as weapons | BVR | ✅ | ⬜ | ⬜ | Pick up a torch or a Bokoblin arm: it is used as a sword |
| B08 | Link's head keeps its shadow in first person | BVR | ❌ | ⬜ | ⬜ | Not done: the hidden head has no shadow |
| B09 | Link and his weapon do not fade with the camera near | BVR | ➖ | ✅ | ⬜ | Not needed: in first-person captures the hands and weapons are solid |
| B10 | Natural arm poses while moving and turning the hands (elbow, shoulder, forearm roll) | Mod | ✅ | ✅ | ⬜ | Move the hands around, raise them overhead, turn the palms up and down: the elbows and shoulders look natural, nothing snaps (SAVAGEWIND_VR_ARM_RIG=0: the first rig, to compare) |
| B11 | Link's hands and arms at the right depth in both eyes while turning (the right frame's skeleton placed with the pair's anchor) | Mod | ✅ | ✅ | ⬜ | Hold the hands ahead and turn with the stick: the hands stay single, not crossed or doubled |

## C. Controls (Quest layout)

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| C01 | Left stick: walk where you look | BVR | ✅ | ✅ | ⬜ | Walk looking to a side: he goes where you look |
| C02 | Walk where the left controller points | BVR | ✅ | ✅ | ⬜ | Options > VR > Walking direction: the left hand |
| C03 | Left stick click: crouch; held: run | Mod | ✅ | ✅ | ⬜ | Hold the click while walking: he runs |
| C04 | Right stick: turn (smooth, snap or head only) | BVR | ✅ | ✅ | ⬜ | Options > VR > Turning; try the three |
| C05 | Right stick up: jump | Mod | ✅ | ✅ | ⬜ | Push the right stick up: he jumps (not during Magnesis) |
| C06 | A: accept, interact | Mod | ✅ | ✅ | ⬜ | Talk, open a chest with A |
| C07 | B: cancel, put away (a tap; held does not run) | Mod | ✅ | ✅ | ⬜ | Hold B: he does not run |
| C08 | Right trigger: attack with the right hand | Mod | ✅ | ✅ | ⬜ | Sword out: the right trigger attacks |
| C09 | Right trigger held: a charged attack (the weapons' menu: the right shoulder's grip; SAVAGEWIND_VR_TRIGGER_MENUS=1: also the trigger held, hand empty) | Mod | ✅ | ✅ | ⬜ | Sword out, hold the trigger: Link charges, let go: the spin |
| C10 | Left trigger: guard, lock on, and a parry at the press | Mod | ✅ | ✅ | ⬜ | Shield: press just before a hit: parry |
| C11 | Left trigger held: the guard as long as held (the shields' menu: the left shoulder's grip) | Mod | ✅ | ✅ | ⬜ | Shield out, hold the trigger: he keeps guarding, no menu |
| C12 | A while guarding does not parry | Mod | ✅ | ✅ | ⬜ | Guarding, press A: nothing |
| C13 | Right grip: the bow out or away | Mod | ✅ | ✅ | ⬜ | Right grip away from the body: bow out; again: away |
| C14 | Right grip held: the bows' menu | Mod | ✅ | ✅ | ⬜ | Hold it: the bows menu; no arrow on letting go |
| C15 | Left grip held with the bow out: the arrows' menu | Mod | ✅ | ✅ | ⬜ | Bow out, hold the left grip: the arrows menu |
| C16 | Left grip: grab, interact | BVR | ✅ | ✅ | ⬜ | Left grip at an object: he picks it up |
| C17 | Y: the rune; held: the runes' menu | Mod | ✅ | ✅ | ⬜ | Y: the rune out; hold: the menu |
| C18 | X: the map | Mod | ✅ | ✅ | ⬜ | X: the map opens |
| C19 | Menu button: pause | Mod | ✅ | ✅ | ⬜ | Left menu button: the pause menu |
| C20 | Right stick click: inventory; held: the scope | BVR | ✅ | ✅ | ⬜ | Click: the inventory |
| C21 | In menus the buttons are the game's letters (triggers L / R) | BVR | ✅ | ✅ | ⬜ | Inventory: the triggers turn the pages |
| C22 | A button held when a menu opens or closes is not repeated | Mod | ✅ | ✅ | ⬜ | Open the map with X held: it does not close by itself |
| C23 | Quest button icons instead of Switch ones | Mod | ✅ | ✅ | ⬜ | Video > Button icons: Automatic; prompts show A, B, the triggers |
| C24 | The game's rumble on the controllers | BVR | ✅ | ✅ | ⬜ | Hit or get hit: it vibrates |
| C25 | Vibration strength | BVR | ✅ | ✅ | ⬜ | Options > VR > Vibration and Vibration strength |
| C26 | Left-handed mode (hands swapped) | BVR | ✅ | ✅ | ⬜ | Options > VR > Left-handed: everything to the other hand |
| C27 | BetterVR's buttons (SAVAGEWIND_VR_CONTROLS=bettervr) | BVR | ✅ | ✅ | ⬜ | B jumps, A held runs |
| C28 | Classic layout (SAVAGEWIND_VR_CONTROLS=classic) | Mod | ✅ | ✅ | ⬜ | Buttons as a gamepad, no gestures |
| C29 | Index, WMR, Vive, Pico, Vive Cosmos and Steam Frame controllers | BVR | ✅ | ⬜ | ⬜ | Only with those controllers |
| C30 | A gamepad still works next to the VR controllers | Mod | ✅ | ✅ | ⬜ | Connect an Xbox controller: it plays too |
| C31 | Stick dead zone | BVR | ✅ | ✅ | ⬜ | Options > VR > Stick dead zone: raise it if Link walks on his own |
| C32 | Fix controller rotation (Meta's Oculus runtime) | BVR | ✅ | 🟡 | ⬜ | Options > VR > Fix controller rotation: Automatic turns it on with the Oculus runtime |
| C33 | No "nothing to draw" head shake | BVR | ✅ | ✅ | ⬜ | Without a bow, the right grip or trigger does not make Link shake his head |
| C34 | Ladders handled specially (up and down whatever the view) | BVR | ✅ | 🟡 | ⬜ | On a ladder, look to a side and push forward: Link climbs (the turn checked on flat ground, not yet on a ladder) |
| C35 | A quick menu does not turn the view (the right stick picks in it) | Mod | ✅ | 🟡 | ✅ | Hold the runes or weapons menu, push the right stick to the sides, let go: the view looks where it did |

## G. Gestures and body slots

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| G01 | Right shoulder + grip: the weapon out or away | BVR | ✅ | ✅ | ⬜ | Right hand at the shoulder, grip: sword out; again: away |
| G02 | Right shoulder grip held: the weapons' menu | BVR | ✅ | ✅ | ⬜ | Hold 0.5 s: the menu |
| G03 | Left shoulder + grip: the bow | BVR | ✅ | ✅ | ⬜ | Hand at the left shoulder, grip: the bow |
| G04 | Left shoulder held: shields (with a weapon out) or bows | BVR | ✅ | ✅ | ⬜ | Hold 0.5 s: the menu |
| G05 | Left hip + grip: the rune; held: the runes | BVR | ✅ | ✅ | ⬜ | Hand at the left hip |
| G06 | Right hip grip held: drop what that hand holds | BVR | ✅ | ✅ | ⬜ | Options > VR > Drop gesture; hold 0.5 s: the weapon falls (never the Slate) |
| G07 | Throw the weapon: trigger held at the shoulder, let go | BVR | ✅ | ✅ | ⬜ | Weapon out, trigger at the right shoulder, let go: thrown |
| G08 | Whistle: both hands at the mouth + both triggers | Mod | ✅ | ✅ | ⬜ | The horse comes; at the mouth the triggers do not guard or attack |
| G09 | BetterVR's whistle: both grips at the mouth | BVR | ✅ | ✅ | ⬜ | bettervr layout: grips at the mouth |
| G10 | Physical parry: a quick push of the left hand while guarding | BVR | ✅ | ✅ | ⬜ | Shield up, push the left hand ahead: parry |
| G11 | Guard by raising the shield hand | BVR | ✅ | ✅ | ⬜ | Options > VR > Raise the left hand to guard; hand in front of the chest: guards |
| G12 | A vibration at each body slot | BVR | ✅ | ✅ | ⬜ | Pass the hand over the shoulder: it vibrates |
| G13 | The body slots' guide | BVR | ✅ | ✅ | ⬜ | Options > VR > Body slot guide: pale discs at the shoulders, hips and mouth when a hand is near |
| G14 | The shield aimed by the hand (the guard faces the view) | BVR | ➖ | ⬜ | ⬜ | Not needed if the guard follows the view (BetterVR sets it from Link's facing; here Link turns to the view to guard): guarding, does it block hits from the side you look at? |

## A. Combat

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| A01 | Slashes and stabs by motion (Acudofy's analysis) | BVR | ✅ | ✅ | ⬜ | Options > VR > Motion combat; slash the air: Link attacks; thrust: a stab |
| A02 | Slow moves and wrist flicks do not count | BVR | ✅ | ✅ | ⬜ | Move the hand slowly: no attack |
| A03 | The weapon hits where it is (its hitbox follows the hand) | BVR | ✅ | ✅ | ⬜ | Hit an enemy at your side: it is hit where the hand is |
| A04 | It hits when swung, harder the faster | BVR | ✅ | ✅ | ⬜ | A soft swing and a hard one: different damage |
| A05 | One hit per swing | BVR | ✅ | ✅ | ⬜ | Options > VR > One hit per swing: the same enemy is not hit twice |
| A06 | Cutting trees | BVR | ✅ | ✅ | ⬜ | A chop at a tree: it falls |
| A07 | A swing also plays Link's attack | Mod | ✅ | ✅ | ⬜ | Options > VR > Swings play Link's attack (off by default since 2026-11-27: its animation moved Link's body under the hands - "four arms"): off, it hits without the animation |
| A08 | Easy swings profile | BVR | ✅ | ✅ | ⬜ | Options > VR > Easy swings |
| A09 | Two-handed weapons held with both hands | BVR | ✅ | ✅ | ⬜ | Options > VR > Two-handed grip; a claymore out, left grip on the hilt: the blade points at the other hand |
| A10 | Lighting fires, cutting ropes by motion | BVR | ✅ | ⬜ | ⬜ | A torch to the grass; cut a rope |

## R. Bow

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| R01 | Aiming with the hands (string hand to bow hand) | BVR | ✅ | ✅ | ✅ | The arrow flies along the hands' line, not where you look |
| R02 | Trajectory arc (yellow dots) and its modes | BVR | ✅ | ✅ | ⬜ | Draw: the dots show; Options > VR > Bow trajectory: Full, Faint, Aiming dot only, Off |
| R03 | The arrow stays on the bow while drawing | Mod | ✅ | ✅ | ⬜ | Draw: the arrow points at the bow, not anywhere |
| R04 | No arrow when letting go of a bow menu | Mod | ✅ | ✅ | ⬜ | Bows menu, let go: no shot |
| R05 | Slow motion jumping with the bow | SW | ✅ | ⬜ | ⬜ | Jump from high and draw: slow motion |
| R06 | Aiming with the head | Mod | ✅ | ✅ | ⬜ | Options > VR > Aim the bow with your hands: off, arrows fly where you look |
| R07 | The string follows the drawing hand | BVR | ✅ | ✅ | ⬜ | Draw: the string goes to your hand |

## U. Runes

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| U01 | Runes aimed with the head (the reticle) | BVR | ✅ | ✅ | ⬜ | Bombs, Stasis, Cryonis aim at the view's centre |
| U02 | The right trigger uses the rune | Mod | ✅ | ✅ | ⬜ | Rune out, right trigger: it is used |
| U03 | Magnesis by hand: left and right | BVR+Mod | ✅ | ✅ | ⬜ | Grab something with Magnesis, move the hand sideways |
| U04 | Magnesis: up and down by hand | Mod | ✅ | ✅ | ⬜ | Raise and lower the hand: the object follows |
| U05 | Magnesis: the stick brings it near or far | Mod | ✅ | ✅ | ⬜ | Right stick up and down |
| U06 | Magnesis: the view does not turn by itself | Mod | ✅ | ✅ | ⬜ | Move the object: the view stays |
| U07 | Stasis, Cryonis, bombs | SW | ✅ | 🟡 | ⬜ | Use them as usual |
| U08 | Magnesis: twist the wrist to spin the object | BVR | ❌ | ⬜ | ⬜ | Not done |

## I. Objects

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| I01 | Lifting objects | SW | ✅ | ⬜ | ⬜ | A at a pot or a small rock: he lifts it |
| I02 | Throwing with the hand's velocity | BVR | ✅ | ⬜ | ⬜ | Carrying it: right trigger pressed, swing the hand and let go: it flies where you threw |
| I03 | What you carry drawn in your hand(s) | BVR | ✅ | ⬜ | ⬜ | Carrying: small things in the right hand, big ones between the hands, scaled down |
| I04 | The item windows: a chest's item, a new item, the inventory's description | Mod | ✅ | ✅ | ✅ | Open a chest; open the inventory: the boxes at the game's size and place |
| I05 | Riding a horse | SW | ✅ | ⬜ | ⬜ | Mount and ride: the view follows |
| I06 | Gliding, climbing, swimming | SW | ✅ | 🟡 | ⬜ | Try each |
| I07 | Cooking, talking, shopping | SW | ✅ | ⬜ | ⬜ | Cook at a pot, buy at a shop |
| I08 | Soothing the horse: on horseback with nothing in hand, the right trigger (BetterVR 1.0.2) | BVR | ✅ | ✅ | ⬜ | Ride with no weapon out and pull the right trigger: the horse calms down, no weapon is drawn; with a weapon out it attacks |
| I09 | Petting the horse: an empty hand patting low ahead (its neck) | Mod | ✅ | ✅ | ⬜ | Riding, pat the air low in front of you: Link pets the horse |

## K. Comfort

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| K01 | Smooth turning and its speed | BVR | ✅ | ✅ | ⬜ | Options > VR > Turning speed |
| K02 | Snap turning and its angle | BVR | ✅ | ✅ | ⬜ | Options > VR > Snap angle |
| K03 | Turning with the head only | Mod | ✅ | ✅ | ⬜ | Turning: Head only; the stick does not turn |
| K04 | Comfort vignette while moving | Mod | ✅ | ✅ | ⬜ | Options > VR > Comfort vignette: High; walking darkens the edges |
| K05 | Seated or standing | BVR | ✅ | ✅ | ⬜ | Options > VR > Play position: Standing uses your real height |
| K06 | Height offset (-30 to +30 cm) | BVR | ✅ | ✅ | ⬜ | Options > VR > Height |
| K07 | Roomscale: walking in the room walks Link (the simple way) | BVR+Mod | 🟡 | ✅ | ⬜ | Options > VR > Roomscale; two steps in the room: Link walks; at a wall the view comes back |
| K08 | Comfort presets (Gentle, Balanced, Immersive) | BVR | ✅ | ✅ | ⬜ | Options > VR > Comfort preset; change a row after it: Custom |
| K09 | Full roomscale (Link's body moved with its own world casts) | BVR | ❌ | ⬜ | ⬜ | Not done: the simple roomscale above |

## H. HUD and menus

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| H01 | The HUD apart, floating ahead | BVR | ✅ | ✅ | ⬜ | Hearts and minimap on a panel ahead, not on the eyes |
| H02 | The HUD follows the gaze or stays | BVR | ✅ | ✅ | ⬜ | Options > VR > HUD follows your gaze: off, it stays low ahead |
| H03 | HUD size and distance | BVR | ✅ | ✅ | ⬜ | Options > VR > HUD size / HUD distance |
| H04 | Pause, map, inventory on the virtual screen | BVR | ✅ | ✅ | ⬜ | Open them: a floating screen |
| H05 | Savage Wind's overlay menu on the virtual screen | Mod | ✅ | ✅ | ⬜ | Open with F1 or hold the VR menu button for 0.6 s during play |
| H06 | Performance overlay in the headset | BVR | ✅ | ✅ | ⬜ | Video > Performance overlay: up and to the right |
| H07 | VR options in the game | BVR | ✅ | ✅ | ⬜ | Pause > System > Options > VR |
| H08 | Controls guide page | BVR | ✅ | ✅ | ⬜ | Options > VR > Controls guide: each button and gesture |
| H09 | Dialogues and texts readable | SW | ✅ | ⬜ | ⬜ | Talk to someone: it reads well on the screen |
| H10 | Screen markers placed on the floating HUD (names, targets) | BVR | ✅ | 🟡 | ⬜ | Lock on to an enemy, pin a map marker: the marker sits over the thing, not nearer the centre (HUD following the gaze) |
| H11 | The stamina wheel placed for the floating HUD (first person: under the hearts) | BVR | ✅ | 🟡 | ⬜ | Sprint or climb in first person: the wheel under the hearts |
| H12 | UI blending fixes | BVR | ➖ | ✅ | ⬜ | Not needed here: the HUD is split another way; captures look right |

## E. Events and cutscenes

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| E01 | Automatic cutscene mode (BetterVR's table of 1497 events) | BVR | ✅ | ✅ | ⬜ | Options > VR > Cutscenes: Automatic |
| E02 | Cutscenes always first person, third person or flat | BVR | ✅ | ✅ | ⬜ | Try each mode with an event (opening a chest) |
| E03 | Black bars for cutscenes on the flat screen | BVR | ✅ | ✅ | ⬜ | Options > VR > Cutscene black bars |
| E04 | Events without the frame rate drop (chests appearing, opening) | Mod | ✅ | ✅ | ⬜ | Clear a camp: the chest appears at a good frame rate |
| E05 | Events that must run at 30 fps (Kohga) on the screen | SW+Mod | ✅ | ⬜ | ⬜ | Only if you get there |

## O. Options, install and platform

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| O01 | Installer Install BOTW VR.bat | Mod | ✅ | ✅ | ⬜ | Install from scratch |
| O02 | The VR launcher (Savage Wind VR.exe) | Mod | ✅ | ✅ | ⬜ | Start it from Virtual Desktop |
| O03 | The options are kept between runs | Mod | ✅ | ✅ | ⬜ | Change one, restart: it stays |
| O04 | The VR options in Spanish | Mod | ✅ | ✅ | ⬜ | Language Spanish: all VR texts in Spanish |
| O05 | The vr console command (status) | Mod | ✅ | ✅ | ⬜ | In-game console: vr |
| O06 | Linux (AppImage with VR) | Mod | ✅ | 🟡 | ⬜ | Only with Linux and SteamVR / Monado / WiVRn |
| O07 | Foveated rendering | BVR | ❌ | ⬜ | ⬜ | Not done (Virtual Desktop did not offer the extension) |
| O08 | Spectator / recording view (wider, no HUD) | BVR | ❌ | ⬜ | ⬜ | Not done: the monitor shows the left eye |
| O09 | Boot straight into the game | BVR | ➖ | ➖ | ⬜ | Not needed: the VR launcher starts the game directly |
| O10 | A window the system keeps at another size (Steam Frame standalone, gamescope): the eyes at their own resolution, the eyes fused | Mod | ✅ | ✅ | ⬜ | Steam Frame: Options > VR > Eye resolution changes the sharpness; no eyestrain |
| O11 | No freezes from the monitor window: in play it is not updated (SAVAGEWIND_VR_MIRROR=1: shown) | Mod | ✅ | ✅ | ⬜ | Play with the game window on the monitor, move it: the headset never freezes (before: up to 2 s) |
| O12 | Lighter barriers between render passes in VR (SAVAGEWIND_VK_BARRIER_FRAG=0: as before) | Mod | ✅ | ✅ | ⬜ | Compare the fps with and without; the picture the same (terrain, grass, water, effects) |
| O13 | The sun's shadows drawn once a stereo pair: off by default, the right eye's shadows came out wrong (SAVAGEWIND_VR_SHADOWS_ONCE=1: on) | Mod | ✅ | ⬜ | ⬜ | Stand still near shadows (trees, rocks): the same shadows in both eyes |
| O14 | The HUD layer made once a stereo pair | Mod | ✅ | ✅ | ⬜ | The HUD (hearts, minimap, stamina) shows and updates as before |
| O15 | Linux 6.7+: the game geometry in video memory (SAVAGEWIND_NO_WRITE_WATCH=1: as before; Windows already had it) | Mod | ✅ | ✅ | ⬜ | Linux: the log says "write watch: userfaultfd" and "heap copy in video memory: on" |
| O16 | The game's HUD drawn only in the right eye's frame, which makes the HUD layer (SAVAGEWIND_VR_HUD_ONCE=0: in both) | Mod | ✅ | ✅ | ⬜ | The HUD (hearts, minimap, stamina, prompts) shows and updates as before; compare the fps with and without |
| O17 | Synchronous stereo, in progress (SAVAGEWIND_VR_SYNC=1, off by default): one game frame a pair, its lists translated again for the right eye with its camera - both eyes the same moment, the game simulated once a pair. The geometry and the light are right, eye-independent passes drawn once, the sun shadows right in both eyes; one far tree black and now and then a darker right eye (docs/vr/SYNC_STEREO.md) | Mod | 🟡 | ⬜ | ⬜ | Not for play yet |
| O18 | Multiview (Options > VR > Rendering: Multiview; experimental, not the default): both eyes drawn in one pass from one game frame, the HUD apart, TAA per eye, smooth turning - 147.5 headset fps against 98.5 in stereo with TAA (fake_xr, RTX 3090, Ryzen 9 9950X3D, 5 interleaved runs). Not yet in it: object motion vectors for TAA, Video's Upscaler (Automatic picks Stereo when it is on; Sharpening and Colour per eye since 2026-11-26), a few 64x64 reflection passes left-eye only. 2026-11-25 (after that report): the HUD in its 16:9 layout as in Stereo, the right eye's outer edge complete (both eyes the union field), the right eye no longer the left one's picture in a third of the frames (Quest 3 dumps), eye-match at Kakariko 0.00-0.18% differing | Mod | 🟡 | ✅ | ❌ in a headset (2026-11-25) smoke, part of the ground and an enemy marker showed in one eye only; item windows looked stretched up and down (fake_xr: the HUD layer is stretched in multiview only); things lower than they should be (missing terrain: fixed after); snow in a different place in each eye | Play a while in the default mode: the hands and the world at the right depth in both eyes, no eye darker or doubled, turning with the stick smooth; compare the fps with Stereo |
| O19 | Options > VR > Rendering (also in Savage Wind Configuration, the launcher): Automatic (recommended: Stereo for now), Stereo (most compatible), Multiview (faster, experimental), Mono; applies after a restart (SAVAGEWIND_VR_RENDER=auto|stereo|multiview|mono) | Mod | ✅ | ✅ | ⬜ | Pick each, restart: the log says "[vr] rendering: ..." and the picture and fps change; Automatic says stereo |
| O20 | Mono: one centred picture shown to both eyes, no stereoscopic depth (for people who see with one eye, or for speed): only when chosen, never automatic. Both eyes the same bytes, the views from the head's centre, one game update a frame (test_vr_mono.py); 178.6 headset fps (fake_xr) | Mod | ✅ | ✅ | ⬜ | Options > VR > Rendering: Mono, restart: comfortable, menus and HUD single and readable; the 3D depth is gone (as it should) |
| O21 | Multiview asked for (or Automatic) on a driver without it: Stereo, not the synchronous stereo prototype (SAVAGEWIND_VK_NO_MULTIVIEW=1 tries it) | Mod | ✅ | ✅ | ⬜ | Only on a GPU without multiview: the log says the driver has none and plays in Stereo |
| O22 | Options > VR > Experimental features (off by default; SAVAGEWIND_VR_EXPERIMENTAL=1): experimental paths run only with it on - today Rendering: Multiview; off, a chosen Multiview runs Stereo and the row says it needs it | Mod | ✅ | ✅ | ⬜ | Rendering: Multiview with it off: the row says "(needs Experimental features)", the game plays in Stereo; turn it on, restart: Multiview |
| O23 | The game window on the monitor hidden while the headset session runs (Options > VR > Hide the window on the monitor, on; shown again when the session ends; SAVAGEWIND_VR_HIDE_WINDOW=0 keeps it) | Mod | ✅ | ✅ | ⬜ | Play in the headset: no game window on the desktop; Savage Wind Configuration stays open beside it; disconnect the headset: the window comes back |
| O24 | Performance in VR (Options > VR > Performance, on by default): Draw distance Potato and SMAA in place of TAA in the headset only, Video's choices kept for flat play; +25% headset fps in Stereo, +24% in Multiview, +31% in Mono (fake_xr, RTX 3090; SAVAGEWIND_VR_PERFORMANCE=0 turns it off) | Mod | ✅ | ✅ | ⬜ | Play a while with it on and off (restart not needed): compare the fps and how far off trees and buildings show; the log says `[vr] performance: on` |

## S. Savage Wind (the PC recomp) in VR

| ID | Feature | Origin | Implemented | AI validated | Human validated | How to test |
|---|---|---|---|---|---|---|
| S01 | The whole game and both DLC (1.9.0) | SW | ✅ | ⬜ | ⬜ | Use something of the DLC (Majora's Mask, the Trial of the Sword) |
| S02 | Graphics presets (Potato to Savage) | SW | ✅ | ⬜ | ⬜ | Video > Preset: the picture changes in the headset |
| S03 | Draw distance, shadows, quality | SW | ✅ | ✅ | ⬜ | Video > Advanced: it shows in the headset |
| S04 | DLSS / FSR 3 / FSR 4 / XeSS | SW | ✅ | ⬜ | ⬜ | Video > Upscaler: does it look right in both eyes? (off recommended) |
| S05 | FSR 3 frame generation | SW | ✅ | ⬜ | ⬜ | Probably no use in VR |
| S06 | HDR | SW | ➖ | ➖ | ➖ | In VR the window stays SDR |
| S07 | Colour grading, colour vision filters | SW | ✅ | ⬜ | ⬜ | Options > Colour: applied in the headset? |
| S08 | Keyboard and mouse | SW | ➖ | ➖ | ➖ | Not for VR |
| S09 | Gyro, Steam Input | SW | ➖ | ➖ | ➖ | Not for VR |
| S10 | Save profiles, backups, Wii U / Cemu import | SW | ✅ | ⬜ | ⬜ | Load an imported save |
| S11 | Photo mode | SW | ✅ | ⬜ | ⬜ | Does it work in VR? |
| S12 | Cheats, assists and the teleport | SW | ✅ | 🟡 | ⬜ | Teleport: the view arrives right |
| S13 | amiibo | SW | ✅ | ⬜ | ⬜ | Use the amiibo rune |
| S14 | Quick inventory drop and sort | SW | ✅ | ✅ | ⬜ | The drop gesture uses it |
| S15 | 30 to 360 fps | SW | ➖ | ➖ | ➖ | In VR the headset sets the frame rate (twice its Hz) |
| S16 | Discord Rich Presence | SW | ✅ | ⬜ | ⬜ | Optional |

## Where this mod goes beyond BetterVR

- Native, no emulator: a much higher frame rate (about 110 stereo pairs a second at 1080 lines an eye on an RTX 3090,
  measured against fake_xr).
- Savage Wind's graphics: DLSS, FSR, render scale, draw distance, shadows, TAA per eye.
- The 1.9.0 game with both DLC.
- Its own Quest layout, Magnesis up and down by hand, the arrow on the bow, the Quest's button icons.
- Tests with no headset (fake_xr, the hand simulator).
