// A bound actor right after its matrix was made from its parent's bone (tools/patches.py _weapon_bind, main+0x1525fb8:
// the weapon in Link's hand from his Weapon_ bone), before its model and its physics (the attack sensor among them)
// take it (docs/vr/re-notes.md). The VR mode (make VR=1) moves a weapon in Link's hand with the hand; otherwise
// nothing happens.
#include <stdint.h>

#ifdef SW_VR
void vr_weapon_bind(uint64_t actor);
#endif

void rt_weapon_bind(uint64_t actor) {
#ifdef SW_VR
    vr_weapon_bind(actor);
#else
    (void)actor;
#endif
}

// A weapon's attack step about to read its attack sensor's set-up (tools/patches.py _weapon_attack, main+0x7b1690;
// docs/vr/re-notes.md). The VR mode turns the sensor of the weapon in Link's hand on while it is swung.
#ifdef SW_VR
void vr_weapon_attack(uint64_t weapon);
#endif

void rt_weapon_attack(uint64_t weapon) {
#ifdef SW_VR
    vr_weapon_attack(weapon);
#else
    (void)weapon;
#endif
}
