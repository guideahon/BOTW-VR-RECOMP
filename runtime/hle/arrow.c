// An arrow leaving the bow (tools/patches.py _arrow_aim, main+0x931590): the shoot step of its AI has just made the
// way it flies (a unit vector, from where the arrow is to the shooter's aim point) and is about to scale it by the
// first speed; the aim point (target) goes to the arrow as its TargetPos (docs/vr/re-notes.md). The VR mode (make
// VR=1) turns both along the player's hands; otherwise nothing happens.
#include <stdint.h>

#ifdef SW_VR
void vr_arrow_aim(float *dir, const float *origin, float *target);
#endif

// A lifted object thrown (tools/patches.py _grab_throw, main+0xc76e20): vel is the throw velocity the game just made
// (world, m/s), about to go to the object. The VR mode puts the hand's own throw there.
#ifdef SW_VR
void vr_grab_throw(float *vel);
#endif
void rt_grab_throw(float *vel) {
#ifdef SW_VR
    vr_grab_throw(vel);
#else
    (void)vel;
#endif
}

// The UI's world-to-screen (tools/patches.py _ui_project, main+0xe1de34): xy is a spot's place on the screen, -1..1,
// about to become a label's or marker's layout position (times half: the layout's half size); caller, who asked. The
// VR mode moves it onto the floating HUD panel.
#ifdef SW_VR
void vr_ui_project(float *xy, uint64_t caller, const float *half);
#endif
void rt_ui_project(float *xy, uint64_t caller, const float *half) {
#ifdef SW_VR
    vr_ui_project(xy, caller, half);
#else
    (void)xy, (void)caller, (void)half;
#endif
}

void rt_arrow_aim(float *dir, const float *origin, float *target) {
#ifdef SW_VR
    vr_arrow_aim(dir, origin, target);
#else
    (void)dir, (void)origin, (void)target;
#endif
}

// A multi-shot bow's other arrows (tools/patches.py _arrow_aim_side, main+0x92cfbc): their shoot step has just made
// the way each flies (from its start to its target, the bow's spread in it); actor is the arrow, target its TargetPos.
#ifdef SW_VR
void vr_arrow_aim_side(uint64_t actor, float *dir, float *target);
#endif
void rt_arrow_aim_side(uint64_t actor, float *dir, float *target) {
#ifdef SW_VR
    vr_arrow_aim_side(actor, dir, target);
#else
    (void)actor, (void)dir, (void)target;
#endif
}
