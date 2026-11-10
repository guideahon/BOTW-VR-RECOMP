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

## What is found in 1.9.0 so far

A scan of the main module (`.text`) for the shape of a 64-byte bone array accessor (index << 6, a halfword or byte flag
or'ed and stored back, a 3-float scale) gave one cluster of model bone accessors, the runtime addresses of 1.9.0
(build cd57b23fa4bbad65):

| Address | Shape | Guess |
|---|---|---|
| `0x7000f961c4` | `x0 + 0x118 -> + 0x20` bones, `idx << 6`; flag at `+8` (halfword) `| 8`; 64 bytes copied out, transposed | a bone's matrix, read for someone else (a 4x4 / 3x4 layout change) |
| `0x7000f9621c` | flag at `x0 + 0x170` (byte) `| 4`; returns `x0 + 0x198 -> + idx << 6` | a bone's local matrix, handed out to be written (marked dirty first): the closest match to BetterVR's hook point |
| `0x7000f96254` | `x0 + 0x128`, rows of 128 bytes, a 32-byte table at `+0x58`; writes up to five floats to x3 / x4 / x5 | an animation or shape parameter, not a bone matrix |

None is confirmed. The next steps:

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
