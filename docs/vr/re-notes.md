# VR: what the hands and the weapon in hand still need from the game

Motion combat as it is (docs/VR.md, phase 8) reads the hands and presses the game's own buttons: a slash or a stab is
the game's attack, the raised left hand its guard. What BetterVR does on top of that, on the Wii U version, needs game
functions of the 1.9.0 Switch code that are not found yet. These notes are where that stands.

## What BetterVR hooks for it (Wii U v208, its `resources/BreathOfTheWild_BetterVR/patch_CTRL_*.asm`)

| Purpose | Wii U function (BetterVR's names) | What it does there |
|---|---|---|
| Link's arms follow the controllers | `gsys::ModelUnit::getBoneLocalMatrix`, via `ksys::phys::ModelBoneAccessor` (`getBoneName`) | For the model "GameROMPlayer", each arm bone's local matrix (Clavicle, Arm_1, Arm_2, Elbow, Wrist, Wrist_Assist, Weapon _L / _R) is replaced: two-bone IK from the shoulder to the controller's grip pose |
| The weapon in the hand | `ActorWeapons::resetBaseProc`, `PlayerInfo::getActor`, `hook_SetPlayerWeaponScale` | The weapon follows the Weapon_R bone (above); its scale kept |
| The weapon's hit where it moves | `hook_EnableWeaponAttackSensor`, `ksys::phys::contactLayerFromText`, `ksys::act::hasTag` | The weapon's attack sensor (its hitbox) is turned on while the swing analysis says an attack is active, so what the blade touches is hit, scaled by the swing's power |
| Dropping, grabbing, throwing | `ActorWeapons::dropWeapon`, `PlayerOrEnemy::dropWeapon`, `requestCreateWeapon`, `CreatePlayerEquipActorMgr` | Gestures to take weapons from the back, drop and throw them |
| Roomscale and collisions | `RigidBody_getPosition / getTransform`, `SetRigidBodyPosition / Transform`, ray and shape casts | Link's body follows the player's real steps; the camera kept out of walls |

BetterVR also carries Link's skeleton (bone names, rest positions, hierarchy) as data taken from the game's model. That
can't come into this repository (AGENTS.md: no game data): it would have to be read from the player's own files at
run time, or from the model in memory.

## Link's skeleton in memory (2026-11-11)

Tools: `mem find` (text, `hex:`, `ptr:<lo>-<hi>`, `mtx:<x>,<y>,<z>,<r>` for 3x4 matrices with orthogonal rows translated
near a point) and `mem watch <addr> <len>` (the page trap of SAVAGEWIND_WATCH, armed at run time; with
SAVAGEWIND_WATCH_WRITES=<n> each write logs its host pc and the guest call chain), driven by `tools/vr/re_run.py`,
`re_bones.py`, `re_tables.py`, `re_bonewatch.py`, `re_skel.py`, `re_model.py` (each boots the game into play against
fake_xr from a scratch save).

- **Bone names in the code.** main's data has tables of sead::SafeString (vtable 0x7001bc32c8, then the text) naming
  Link's bones: 0x7001e14xxx lists the hand and arm (Finger_e2_L ... Weapon_Root_L, Weapon_L, Clavicle_R, Arm_1_R,
  Arm_2_R, Wrist_R, Finger_a1_R ... Finger_d3_R); 0x7001e45560 the clavicles, arms, legs and face controls
  (Clavicle_L / _R, Arm_1_L / _R, Leg_1_L / _R, Lash_Trans_U/V, Nose_Scale, Mouth_Scale ...); 0x7001db1xxx the horse's
  and other animals' bones.
- **Link's world matrices.** In play, 111 contiguous 3x4 matrices (stride 0x30) translated within 1.6 m of Link:
  the run `mem find mtx:` finds (one run, at the same address in runs from the same save). Next to it a second run of
  the same size 0x1500 later: the pair is a double-buffered copy (a descriptor list nearby holds both, size 0x14d0 =
  111 x 0x30).
- **Who writes them.** `mem watch` on the first matrix: one writer, a copy (guest lr 0x7000045da4) called from
  main+0xf9790c in **main+0xf978e8**, itself called through a vtable (+0x2b0) from main+0xf976cc. main+0xf978e8 takes a
  gsys model (x0), passes `x0 + 0xe0` (an embedded nn::g3d ModelObj: its first field is the ResModel, "FMDL") and its
  flags (`[x0 + 0xd8] & 3`) to main+0x41098, then tail-calls main+0x41250 with the same: the skeleton's world
  calculation and its copy out. **The hook point for the arms and the head:** before main+0xf97908 (`bl 0x41098`) the
  local matrices are animated and the world ones not yet made.
- **Found (same day):** Link's model is the one whose ResModel is named "Link" (tools/vr/re_model.py,
  re_link.py). model + 0x118 (ModelObj + 0x38) is its SkeletonObj: + 0x00 the ResSkeleton (FSKL; FSKL + 0x18 the
  ResBone array, 0x50 a bone, the name's pointer first; 121 bones, depth-first: Root, Skl_Root, Spine_1, Spine_2,
  Clavicle_L, Arm_1_L ... Neck 52, Head 53 ... Weapon_Root 120), + 0x18 the local matrices (0x60 a bone: flags, scale,
  rotation rows, translation; not read again before the skinning), + 0x20 **the world matrices** (0x40 a bone, four
  columns), + 0x50 the bone count. main+0x41098 loads ModelObj + 0x38 and jumps to main+0x45d60, which multiplies the
  world matrices by the inverse bind poses into the GPU's skinning matrices (the double-buffered run found first). So
  the world matrices at the hook (main+0xf97900) are what is drawn: runtime/vr/vr_skel.c hides the head and solves the
  arms there. The head armor and the hair's extras are models of their own ("Armor_..._Head", "Armor_Default_Extra_00 /
  01"), the arms' armor too ("Armor_..._Upper"), with the same bone names.

## The bow aimed by the hands (2026-11-11): not yet

BetterVR (Wii U) overrides the arrow's launch transform where AI_Arrow::handleStateShoot sets the arrow's rigid body
(patch_CTRL_EntityController.asm 0x02706270 -> hook_OverrideArrowShotTransform, src/hooking/bow.cpp), and its
"TargetPos" parameter loads. On 1.9.0 so far:

- The bow and the arrow are in the hands: the weapon models at Link's hands are read (`Weapon_Bow_001` at the left,
  `Weapon_Arrow_001` at the right while drawing; runtime/vr/vr_skel.c) and follow the IK hands.
- Tried: while drawing, the LookAtCamera's eye / at set along the bow (from the string hand to the bow hand) and the
  view matrix written back with the head's at the end of doUpdateMatrix (a site at 0x7000eb2e4c, its `ret`). The arrow
  still flew where the head looked (it landed at the view's heading, 33.8 deg, with the hands aiming 60 deg to the
  right): the aim follows the camera's matrix, which is also what is drawn. Removed again.
- The matrix reaches the GPU through a generic uniform-block setter (main+0xef0f78, from main+0xf86e80; `mem find
  view:<eye>,<r>` lists the 93 copies of the view matrix and `mem watch` on them names it), at render time, from the
  same camera. Decoupling the aim from the picture there would mean telling the view's uniform apart from the rest.
- Second round: the function profiler (SAVAGEWIND_FNPROF) over a whole run shooting and one not shooting: 87
  functions run only when shooting. A block at main+0x9cd7d4 .. 0x9d7434 runs once a frame while an arrow flies (176
  frames for one shot): the arrow's AI. Of the ones run once a shot, main+0x9d50e4 reads the arrow actor's matrix
  (actor + 0x39c, 3x4 row-major; actor = [AI + 0x188]) and sets a random 5-35 s timer: it is **the arrow sticking**
  (its state when it lands), not the launch (it runs before the trigger is pulled for arrows of earlier shots, and
  long after letting go). Hooking it changed nothing; removed. The "IsShootByPlayer" users all run equally with and
  without a shot: parameter set-ups, not the shot.
- Still to find: the arrow's launch (the AI state entered when the bow is let go, among the arrow AI block's other
  once-a-shot functions: main+0x9cd7d4, 0x9ce3dc, 0x9d6780, 0x9d6be0, or the player's side: main+0x812080 .. 0x812ddc,
  0x684e5c, 0x68717c, 0x687bb0, 0xbcb250, 0xbde780, 0x138c1dc, 0x138fdbc, 0x1390898), then where it sets the arrow's
  matrix or velocity. A frame-exact window (SAVAGEWIND_FNPROF=<from>:<to> around the release, the arrow seen leaving in
  SAVAGEWIND_VR_LOG_MODELS=Weapon_Arrow) separates the launch from the flight.
- Next: the shot itself. "IsShootByPlayer" (main .rodata 0x161b425) is read by 16 functions (tools: an adrp/add xref
  scan); the one of the player's arrow AI, then where it sets the arrow's transform or velocity (a `mem watch` on a
  flying arrow's rigid body, the arrow found with SAVAGEWIND_VR_LOG_MODELS=Weapon_Arrow), is the place to override
  the direction with the bow's, as BetterVR does.

## What is found in 1.9.0 so far

A scan of the main module (`.text`) for the shape of a 64-byte bone array accessor (index << 6, a halfword or byte flag
or'ed and stored back, a 3-float scale) gave one cluster of model bone accessors, the runtime addresses of 1.9.0
(build cd57b23fa4bbad65):

| Address | Shape | Guess |
|---|---|---|
| `0x7000f961c4` | `x0 + 0x118 -> + 0x20` bones, `idx << 6`; flag at `+8` (halfword) `| 8`; 64 bytes copied out, transposed | a bone's matrix, read for someone else (a 4x4 / 3x4 layout change) |
| `0x7000f9621c` | flag at `x0 + 0x170` (byte) `| 4`; returns `x0 + 0x198 -> + idx << 6` | a bone's local matrix, handed out to be written (marked dirty first): the closest match to BetterVR's hook point |
| `0x7000f96254` | `x0 + 0x128`, rows of 128 bytes, a 32-byte table at `+0x58`; writes up to five floats to x3 / x4 / x5 | an animation or shape parameter, not a bone matrix |

None is confirmed. Counted in play with the function profiler, no patch needed (`SAVAGEWIND_FNPROF=2500:3500`
`SAVAGEWIND_FNPROF_BT=7000f9621c,7000f961c4,7000f96254`, flat, on the Great Plateau; runtime/fnprof.c):

| Address | Calls in 1000 frames | First caller (guest lr) |
|---|---|---|
| `0x7000f9621c` | 324 | `0x700159498c` |
| `0x7000f961c4` | 18 | `0x70012f828c` |
| `0x7000f96254` | 2 | `0x70010e57d4` |

So none of them runs for every bone every frame: the arms' animation writes its local matrices another way, and the
accessor BetterVR uses on the Wii U is something else on the Switch (or is reached only by some actors). The way on:
`SAVAGEWIND_FNPROF` over a window where Link only stands, then one where he swings a weapon, and the functions whose
count goes up by about (arm bones x frames) are the candidates; `SAVAGEWIND_FNPROF_BT` then names their callers.

The next steps:

1. Log the callers of `0x7000f9621c` in play (a patch in tools/patches.py that calls the runtime with x0 and w1, the
   way `_lookat_update` calls `rt_lookat`), and find the calls whose model is Link's (the same model unit every frame,
   near Link's position from `PlayerInfo` +0x88).
2. Get the bone names from the model (nn::g3d's skeleton keeps them: a dictionary of names next to the bone array),
   and so the indices of Clavicle_R ... Weapon_R.
3. Port BetterVR's arm IK (src/hooking/skeleton.cpp) to C over runtime/vr/vr_math.h, with unit tests, and write the
   arm bones' local matrices there each frame, after the game's animation and before the matrices are used.
4. Then the weapon's attack sensor: find what turns it on in an attack animation and call it from vr_melee's
   attack state.

Each needs the game running and, for the arms, a headset to judge the result.
