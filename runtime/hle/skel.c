// A gsys model's skeleton, right before its world matrices are made from the animated local ones (tools/patches.py
// _skel_calc, main+0xf978e8; docs/vr/re-notes.md). The VR mode (make VR=1) changes Link's local bones here
// (runtime/vr/vr_skel.c); otherwise nothing happens.
#include <stdint.h>

#ifdef SW_VR
void vr_skel_calc(uint64_t model);
#endif

void rt_skel_calc(uint64_t model) {
#ifdef SW_VR
    vr_skel_calc(model);
#else
    (void)model;
#endif
}
