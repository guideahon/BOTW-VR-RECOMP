// VR mode: Link's bones, after BetterVR's skeleton.cpp: in first person his head is hidden (the eye in it sees out, not
// his hair and face) and his arms follow the VR controllers (two-bone IK, runtime/vr/vr_ik.h).
//
// runtime/hle/skel.c rt_skel_calc runs for every gsys model at main+0xf97900, right before main+0x41098 hands its
// nn::g3d SkeletonObj to main+0x45d60, which multiplies each bone's world matrix by its inverse bind pose into the
// skinning matrices the GPU draws with. Layout in 1.9.0 (found in memory, docs/vr/re-notes.md):
//   model + 0xe0       the nn::g3d ModelObj; its first field the ResModel (FMDL; "Link" for Link's)
//   model + 0x118      (ModelObj + 0x38) the SkeletonObj
//   skeleton + 0x00    ResSkeleton (FSKL); FSKL + 0x18: the bones (ResBone, 0x50 each, the name's pointer first; a
//                      bfres string: a u16 length, then the text), in depth-first order
//   skeleton + 0x20    the world matrices, 0x40 a bone: four columns of 4 floats (x, y, z axes, then the position)
//   skeleton + 0x50    the bone count (low 16 bits)
// Changing the world matrices there changes what is drawn this frame (the animation makes them again next frame).
//
// The head: each bone of it (BetterVR's isFaceBone, with the ears) has its axes squeezed to almost nothing and its
// position put in Link's chest, so the face, eyes, ears and hair shrink into him; the head armor ("Armor_..._Head",
// the hat or hood with its hair) and the hair's extra pieces ("Armor_..._Extra_..") the same, every bone.
// The arms: Link's shoulders (Arm_1) stay where the animation has them; the elbow and wrist are solved so the wrist is
// at the controller's grip, the elbow bending down and out; the hand then turns to point where the controller points.
// The upper arm's bones (Arm_1, Arm_1_Assist), the forearm's (Arm_2, Elbow, Wrist_Assist) and the hand's (Wrist, the
// fingers, Weapon) are turned as solid pieces, so what hangs from them (fingers, the weapon in hand) follows; Link's
// armor on the arms (its own models, with the same bone names) gets the same turns.
//
// SAVAGEWIND_VR_HIDE_HEAD: 1 always (to see it in third person), 0 never; default: in first person.
// SAVAGEWIND_VR_ARMS: 0 off; default: in first person with the controllers tracked.
// SAVAGEWIND_VR_LOG_BONES=1: Link's bone names and indices to the log, once; SAVAGEWIND_VR_LOG_ARMS=<n>: every n-th
// frame, each wrist's distance to its target.
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../rt.h"
#include "../control_cells.h"
#include "../game_sites.h"
#include "vr.h"
#include "vr_ik.h"
#include "vr_options.h"

#define GUEST_OK(a) ((a) >= 0x7000000000ull && (a) < 0x7800000000ull)
#define MAX_BONES 512
#define MAX_SKELS 24

static inline uint64_t rd64(uint64_t a) { return GUEST_OK(a) ? *(const uint64_t *)(uintptr_t)a : 0; }

// a bfres string (u16 length, text) at p, or NULL
static const char *res_str(uint64_t p) {
    if (!GUEST_OK(p)) return NULL;
    uint16_t len = *(const uint16_t *)(uintptr_t)p;
    const char *s = (const char *)(uintptr_t)(p + 2);
    if (!len || len > 64 || s[len] != 0) return NULL;
    for (int i = 0; i < len; i++)
        if (s[i] < 0x20 || s[i] > 0x7e) return NULL;
    return s;
}

// the name of a ResModel (FMDL): its first header pointer that reaches a bfres string
static const char *fmdl_name(uint64_t res) {
    if (!GUEST_OK(res) || memcmp((const void *)(uintptr_t)res, "FMDL", 4)) return NULL;
    for (int off = 8; off < 0x20; off += 8) {
        const char *s = res_str(rd64(res + off));
        if (s) return s;
    }
    return NULL;
}

// BetterVR's isFaceBone (skeleton.cpp: the neck too), with the ears: the bones of Link's head
static bool head_bone(const char *n) {
    static const char *const prefix[] = {"Eye", "Cheek", "Lip", "Hair", "Ponytail", "Momi", "Teeth_", "Chin", "Ear_"};
    for (size_t i = 0; i < sizeof prefix / sizeof *prefix; i++)
        if (!strncmp(n, prefix[i], strlen(prefix[i]))) return true;
    return !strcmp(n, "Nose") || !strcmp(n, "Face_Root") || !strcmp(n, "Head") || !strcmp(n, "Neck");
}

// a bone's part in an arm: side (0 left, 1 right) in the high bits, piece in the low
enum { PART_NONE, PART_UPPER, PART_LOWER, PART_HAND, PART_HEAD, PART_CLAV };
#define PART_TWIST 0x08   // (with PART_LOWER: the forearm's twist bone at the wrist, Wrist_Assist)
static uint8_t arm_part(const char *n) {
    size_t len = strlen(n);
    if (len < 3 || n[len - 2] != '_' || (n[len - 1] != 'L' && n[len - 1] != 'R')) return head_bone(n) ? PART_HEAD : 0;
    uint8_t side = n[len - 1] == 'R' ? 0x10 : 0;
    char base[48];
    if (len - 2 >= sizeof base) return 0;
    memcpy(base, n, len - 2), base[len - 2] = 0;
    if (!strcmp(base, "Clavicle") || !strcmp(base, "Clavicle_Assist")) return side | PART_CLAV;
    if (!strcmp(base, "Arm_1") || !strcmp(base, "Arm_1_Assist")) return side | PART_UPPER;
    if (!strcmp(base, "Wrist_Assist")) return side | PART_LOWER | PART_TWIST;
    if (!strcmp(base, "Arm_2") || !strcmp(base, "Elbow")) return side | PART_LOWER;
    if (!strcmp(base, "Wrist") || !strcmp(base, "Weapon") || !strncmp(base, "Finger", 6)) return side | PART_HAND;
    return head_bone(n) ? PART_HEAD : 0;
}

// what each bone of a skeleton is (cached per SkeletonObj)
typedef struct {
    uint64_t skel;
    int n;
    int clav[2], arm1[2], arm2[2], wrist[2], chest;   // Link's: the joints the IK reads
    int head;                                // ... and his head (the eye's height: crouching, swimming, riding)
    int weapon[2];                           // ... and the bones his weapons hang from (Weapon_L, Weapon_R)
    uint8_t part[MAX_BONES];
} skel_info;
static skel_info skels[MAX_SKELS];
static unsigned skels_next;

static int bone_count(uint64_t skel) {
    int n = (int)(rd64(skel + 0x50) & 0xffff);
    return n > 0 && n <= MAX_BONES ? n : 0;
}

static const char *bone_name(uint64_t skel, int i) { return res_str(rd64(rd64(rd64(skel) + 0x18) + 0x50ull * i)); }

static skel_info *info_of(uint64_t skel, bool log) {
    for (int i = 0; i < MAX_SKELS; i++)
        if (skels[i].skel == skel) return &skels[i];
    skel_info *s = &skels[skels_next++ % MAX_SKELS];
    memset(s, 0, sizeof *s);
    s->skel = skel;
    s->n = bone_count(skel);
    for (int h = 0; h < 2; h++) s->clav[h] = s->arm1[h] = s->arm2[h] = s->wrist[h] = s->weapon[h] = -1;
    s->chest = s->head = -1;
    for (int i = 0; i < s->n; i++) {
        const char *b = bone_name(skel, i);
        if (!b) continue;
        s->part[i] = arm_part(b);
        int side = b[strlen(b) - 1] == 'R';
        if (!strcmp(b, "Clavicle_L") || !strcmp(b, "Clavicle_R")) s->clav[side] = i;
        if (!strcmp(b, "Arm_1_L") || !strcmp(b, "Arm_1_R")) s->arm1[side] = i;
        if (!strcmp(b, "Arm_2_L") || !strcmp(b, "Arm_2_R")) s->arm2[side] = i;
        if (!strcmp(b, "Wrist_L") || !strcmp(b, "Wrist_R")) s->wrist[side] = i;
        if (!strcmp(b, "Spine_2")) s->chest = i;
        if (!strcmp(b, "Head")) s->head = i;
        if (!strcmp(b, "Weapon_L") || !strcmp(b, "Weapon_R")) s->weapon[side] = i;
        if (log) LOG("[vr]   bone %3d %s\n", i, b);
    }
    return s;
}

static struct {
    int hide, arms;         // -1 not read: SAVAGEWIND_VR_HIDE_HEAD, SAVAGEWIND_VR_ARMS
    int log_arms;
    uint64_t model;         // Link's, as last seen
    float chest[3];
    bool chest_ok, said_head, said_arms;
    struct {                // this frame's solution per arm (left, right), for Link and his armor
        bool ok;
        vr_vec3 shoulder, elbow, wrist;   // the pivots: shoulder (where the clavicle put it); the solved elbow and wrist
        vr_vec3 clav_pivot;               // the clavicle's root (the turn below is about it)
        vr_quat clav;                     // the clavicle's turn: the shoulder raised and brought forward reaching
        vr_quat upper, lower, hand;
        vr_quat twist, twist_half;        // the forearm's roll about its own axis (at the wrist, at its middle)
        float bend, roll;                 // the wrist's bend and the forearm's roll against the animation's (deg)
        float miss;                       // the wrist's distance to its target after the solve (m)
        float elbow_down, elbow_out, elbow_back;   // the elbow from the shoulder in his body's frame (m: the log)
        float clav_deg;                   // the clavicle's turn (deg: the log)
    } arm[2];
    struct {                // what carries from one frame to the next, per arm (the rig: smoothing, continuity)
        vr_vec3 pole;
        bool pole_ok;
        float twist;                      // the forearm's last roll (radians, unwrapped)
        bool twist_ok;
        uint64_t ns;
    } rig[2];
    // first person: his body's turn to the head's way this frame (body_turn), for the models hanging from him
    bool body_ok;
    vr_quat body_q;
    vr_vec3 body_pivot;
    float body_turned;       // (radians, the last turn: the log)
    uint64_t body_frame;     // (k.frames after the Link calc that made it)
    float body_pre[160][3];  // his bones' places before the turn (what hangs from him is near one of them)
    int body_npre;
    uint64_t frames;
    // the weapons in his hands: each hand's Weapon_ bone before and after the arms' turns, the change between them
    // (carried to the weapon models hanging there), and what each hand holds, from the weapon models found there
    struct {
        bool ok;
        float before[16], after[16], delta[16];   // (after: the Weapon_ bone with the arms' turns)
        int held;            // VR_SKEL_* below
        uint64_t held_frame; // the frame it was last seen
        uint64_t model;
        uint64_t actor;      // its actor, as its bind made its matrix (vr_weapon_bind)
        bool two_handed;     // a large sword or a spear (can be held with both hands)
    } hand[2];
    uint64_t seen[5];        // the frame a weapon model of each VR_SKEL_* kind was last seen on him (hands or back)
    vr_vec3 head_w;          // his head bone, as animated (world), and the frame it was
    uint64_t head_frame;
    uint64_t carried_model;  // the lifted object's model moved to the hands (carried_in_hands)
    // (a check) Link's wrists as drawn this frame, and how far his armor's wrists (sleeves, gloves) end up from them
    vr_vec3 wrist_final[2];
    uint64_t wrist_final_frame;
    float armor_off, armor_off_max;   // (m: the last and the largest since asked)
    uint64_t armor_checks, armor_off_n;   // (armor arm calcs; those more than 3 cm off)
} k = {.hide = -1, .arms = -1};

// the `vr` command: how far the armor's wrists were from Link's (the largest since the last call), and how often
void vr_skel_armor_off(float *last, float *max, uint64_t *checks, uint64_t *off) {
    *last = k.armor_off, *max = k.armor_off_max, *checks = k.armor_checks, *off = k.armor_off_n;
    k.armor_off_max = 0;
}

enum { VR_SKEL_NONE, VR_SKEL_MELEE, VR_SKEL_BOW, VR_SKEL_SHIELD, VR_SKEL_OTHER };

// vr_input.c: what Link holds in a hand (0 left, 1 right), as seen on the last frames: VR_SKEL_*
// (-1: unknown, Link's skeleton not seen in the last half second)
static uint64_t link_seen_ns;
static uint64_t hand_log_started_ns, hand_log_at_ns[2];

RT_ON_RESET(vr_skel_hand_log_reset) {
    hand_log_started_ns = 0;
    memset(hand_log_at_ns, 0, sizeof hand_log_at_ns);
}

// his head bone's height above where he stands (the game's player position), as last animated: standing, crouching,
// swimming, on a horse; false when not seen in the last half second
static _Atomic uint32_t head_rel_bits;
static _Atomic uint64_t head_seen_ns;
static _Atomic uint32_t face_bits;
static _Atomic uint64_t face_ns;
// the way Link's body faces (a heading: atan2 of his root's +Z x over z), as last animated; false when not seen lately
bool vr_skel_facing(float *yaw) {
    uint64_t t = atomic_load_explicit(&face_ns, memory_order_relaxed);
    if (!t || plat_now_ns() - t > 500000000ull) return false;
    uint32_t b = atomic_load_explicit(&face_bits, memory_order_relaxed);
    memcpy(yaw, &b, 4);
    return true;
}

// Link's head bone in the world as last animated (the `vr` command: how far the eye is from it); false when not seen
bool vr_skel_head_world(float out[3]) {
    if (!k.head_frame || k.head_frame + 30 < k.frames) return false;
    out[0] = k.head_w.x, out[1] = k.head_w.y, out[2] = k.head_w.z;
    return true;
}

bool vr_skel_head_height(float *y) {
    uint64_t t = atomic_load_explicit(&head_seen_ns, memory_order_relaxed);
    if (!t || plat_now_ns() - t > 500000000ull) return false;
    uint32_t b = atomic_load_explicit(&head_rel_bits, memory_order_relaxed);
    memcpy(y, &b, 4);
    return true;
}

int vr_skel_held(int hand) {
    if (!link_seen_ns || plat_now_ns() - link_seen_ns > 500000000ull) return -1;
    return k.hand[hand].held_frame + 8 >= k.frames ? k.hand[hand].held : VR_SKEL_NONE;
}

// vr_input.c: whether Link carries a weapon of this kind (VR_SKEL_MELEE, _BOW...), in a hand or on his back, as seen
// on the last half second of frames: 1 yes, 0 no, -1 unknown (his skeleton not seen)
int vr_skel_owns(int kind) {
    if (kind < 0 || kind > 4 || !link_seen_ns || plat_now_ns() - link_seen_ns > 500000000ull || k.frames < 30) return -1;
    return k.seen[kind] && k.seen[kind] + 30 >= k.frames;
}

// ---- what Link carries over his head, in the hands (BetterVR's weapon.cpp, by LiftType) ----
// While he carries something (vr_player_carrying: the grab action's flag) in first person, the model above his head
// (its root within 0.6 m of a point 0.35 m over his head bone; not his weapons or armor) is drawn in the hands instead:
// a small one (BetterVR's one-hand list: bombs, small pots, Octorok rocks, letter bottles, Cuccos, skull heads, ice
// balls) at the right hand at half its size, anything else between the hands at 0.7 of it. Only its picture moves:
// its physics stays where the game holds it until the throw, which then leaves from there with the hand's velocity
// (vr_input.c). SAVAGEWIND_VR_CARRY_IN_HANDS=0: over his head as in the game
bool vr_player_carrying(void);
bool vr_hand_pose(int hand, vr_pose *out);                     // vr_input.c
bool vr_tracking_to_world(const vr_pose *in, vr_pose *out);   // vr_camera.c
static uint64_t carried_frames;
uint64_t vr_carried_frames(void) { return carried_frames; }

static inline float *bone_m(uint64_t world, int i) { return (float *)(uintptr_t)(world + 0x40ull * i); }

static bool one_hand_item(const char *n) {
    static const char *const names[] = {"RemoteBomb", "Pot", "Obj_KorokPot", "FldObj_Bottle_Letter", "Animal_Kokko",
                                        "Lizalfos_IceBall", "Wizzrobe_WeatherIceBall", "OctObj_", "Bone_Junior_Head",
                                        "Pot_A_S"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (strstr(n, names[i])) return true;
    return false;
}

static bool carried_in_hands(uint64_t world, uint64_t skel, uint64_t model, const char *n) {
    static int on = -1;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_CARRY_IN_HANDS") && *getenv("SAVAGEWIND_VR_CARRY_IN_HANDS") == '0');
    if (!on || k.head_frame + 4 < k.frames || !vr_player_carrying()) {
        k.carried_model = 0;
        return false;
    }
    float *r = bone_m(world, 0);
    vr_vec3 p = vr_bm_pos(r), over = vr_v3_add(k.head_w, vr_v3(0, 0.35f, 0));
    if (k.carried_model != model && vr_v3_len(vr_v3_sub(p, over)) > 0.6f) return false;
    vr_pose h[2], w[2];
    for (int i = 0; i < 2; i++)
        if (!vr_hand_pose(i, &h[i]) || !vr_tracking_to_world(&h[i], &w[i])) return false;
    bool one = one_hand_item(n);
    vr_vec3 to = one ? vr_v3_add(w[1].p, vr_quat_rotate(w[1].q, vr_v3(0, 0.02f, -0.08f)))
                     : vr_v3_scale(vr_v3_add(w[0].p, w[1].p), 0.5f);
    float sc = one ? 0.5f : 0.7f;
    if (k.carried_model != model) {
        k.carried_model = model;
        LOG("[vr] carried: %s in the %s\n", n, one ? "right hand" : "hands");
    }
    // each bone: scaled about the model's root, then moved so the root is at the hand(s)
    int nb = bone_count(skel);
    for (int i = 0; i < nb; i++) {
        float *m = bone_m(world, i);
        for (int c = 0; c < 3; c++)
            for (int rr = 0; rr < 3; rr++) m[c * 4 + rr] *= sc;
        m[12] = to.x + (m[12] - p.x) * sc, m[13] = to.y + (m[13] - p.y) * sc, m[14] = to.z + (m[14] - p.z) * sc;
    }
    carried_frames++;
    return true;
}

static uint64_t string_frames;   // frames the bow's string was drawn to the hand
uint64_t vr_bow_string_frames(void) { return string_frames; }

static int weapon_kind(const char *n) {
    if (strstr(n, "_Bow_")) return VR_SKEL_BOW;
    if (strstr(n, "_Shield_")) return VR_SKEL_SHIELD;
    if (strstr(n, "_Sword_") || strstr(n, "_Lsword_") || strstr(n, "_Spear_") || strstr(n, "_Weapon_")) return VR_SKEL_MELEE;
    return VR_SKEL_OTHER;
}

bool vr_first_person_now(void);                            // vr_camera.c
bool vr_tracking_to_world(const vr_pose *in, vr_pose *out);   // vr_camera.c
bool vr_view_heading(float *yaw);                              // vr_camera.c
bool vr_event_anim_hands(void);                              // vr_camera.c
bool vr_rune_aiming_now(void);                               // vr_camera.c
bool vr_hand_pose(int hand, vr_pose *out);                  // vr_input.c
bool vr_drawing(void);                                      // vr_input.c
bool vr_two_hand_latched(void);                             // below


// squeeze a bone's axes so what is skinned to it shrinks to a point, and put that point in Link's chest: a vertex
// weighted partly to a body bone (hair over the shoulders) then ends inside him
static void squeeze(float *m) {
    for (int i = 0; i < 12; i++) m[i] *= 1e-4f;
    if (k.chest_ok) m[12] = k.chest[0], m[13] = k.chest[1], m[14] = k.chest[2];
}

// the turn from a controller's grip axes (OpenXR: -z ahead through the fist, +y up from it) to Link's wrist bone's, per
// hand (his skeleton mirrors them): BetterVR's (src/hooking/skeleton.cpp, the same model on the Wii U), its glm
// products written as ours (a after b)
static vr_quat wrist_correction(int side) {
    static vr_quat c[2];
    static bool made;
    if (!made) {
        const float d = 3.14159265f / 180;
        vr_vec3 X = vr_v3(1, 0, 0), Y = vr_v3(0, 1, 0), Z = vr_v3(0, 0, 1);
        c[0] = vr_quat_mul(vr_quat_mul(vr_quat_axis_angle(Y, 90 * d), vr_quat_axis_angle(Z, -90 * d)),
                           vr_quat_axis_angle(Z, 30 * d));
        c[1] = vr_quat_mul(vr_quat_mul(vr_quat_mul(vr_quat_axis_angle(Z, -90 * d), vr_quat_axis_angle(Y, -180 * d)),
                                       vr_quat_axis_angle(X, 270 * d)), vr_quat_axis_angle(Z, 30 * d));
        made = true;
    }
    return c[side];
}

// The arm rig (SAVAGEWIND_VR_ARM_RIG=0: the first one, kept for comparison). B10, 2026-11-23: in a Quest 3 the wrists
// followed the hands but the arms took unnatural poses: the elbow's hint followed the controller's whole turn, so
// turning a hand swung the elbow round the arm; the elbow could never rise above the shoulder (hands overhead folded the
// arm down); the shoulder never moved (a reach stretched the arm instead); the forearm's roll could flip at 180 deg.
static int arm_rig(void) {
    static int on = -1;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_ARM_RIG") && *getenv("SAVAGEWIND_VR_ARM_RIG") == '0');
    return on;
}

// Link's body's frame: out (from the other shoulder to this one, level) and back (level: against his root's +Z, the way
// he faces)
static void body_frame(uint64_t world, vr_vec3 sh, vr_vec3 other, vr_vec3 *out, vr_vec3 *back) {
    vr_vec3 o = vr_v3_sub(sh, other);
    o.y = 0;
    float ol = vr_v3_len(o);
    *out = ol > 1e-4f ? vr_v3_scale(o, 1 / ol) : vr_v3(0, 0, 0);
    const float *r0 = bone_m(world, 0);
    vr_vec3 f = vr_v3(r0[8], 0, r0[10]);
    float fl = vr_v3_len(f);
    *back = fl > 1e-4f ? vr_v3_scale(f, -1 / fl) : vr_v3(0, 0, 0);
}

// one arm's solution from Link's skeleton and the controller
static void solve_arm(uint64_t world, const skel_info *s, int side) {
    k.arm[side].ok = false;
    if (s->arm1[side] < 0 || s->arm2[side] < 0 || s->wrist[side] < 0) return;
    vr_pose hand, hw;
    if (!vr_hand_pose(side, &hand) || !vr_tracking_to_world(&hand, &hw)) return;
    vr_vec3 sh = vr_bm_pos(bone_m(world, s->arm1[side])), el = vr_bm_pos(bone_m(world, s->arm2[side])),
            wr = vr_bm_pos(bone_m(world, s->wrist[side]));
    vr_quat q0 = vr_bm_rot(bone_m(world, s->wrist[side]));   // (the wrist bone's turn as animated)
    // the wrist sits a little behind the grip's centre, along the controller's back
    vr_vec3 target = vr_v3_add(hw.p, vr_quat_rotate(hw.q, vr_v3(0, 0, 0.05f)));
    vr_vec3 out, back;
    body_frame(world, sh, vr_bm_pos(bone_m(world, s->arm1[1 - side])), &out, &back);
    int rig = arm_rig();
    uint64_t now = plat_now_ns();
    float dt = k.rig[side].ns ? (float)(now - k.rig[side].ns) / 1e9f : 1.0f;
    if (dt > 0.5f) k.rig[side].pole_ok = false;   // (a pause, a load: the elbow's hint starts over; the roll keeps going)
    k.rig[side].ns = now;
    // the clavicle: reaching far, high or across, the shoulder goes with the hand (up to 20 deg about the clavicle's
    // root, more the further the reach is past 80% of the arm and the higher the hand is above the shoulder), so the
    // arm needs less stretch (VR_IK_MAX_STRETCH) and a hand overhead lifts the shoulder as a body does
    k.arm[side].clav = vr_quat_identity(), k.arm[side].clav_deg = 0;
    k.arm[side].clav_pivot = sh;
    if (rig && s->clav[side] >= 0) {
        vr_vec3 cp = vr_bm_pos(bone_m(world, s->clav[side]));
        float l = vr_v3_len(vr_v3_sub(el, sh)) + vr_v3_len(vr_v3_sub(wr, el));
        vr_vec3 d = vr_v3_sub(target, sh);
        float reach = l > 0.05f ? vr_v3_len(d) / l : 0;
        // (a hand hanging at the side is at full reach too: the far reach counts only from chest height up)
        float far = (reach - 0.85f) / 0.3f, high = (target.y - sh.y) / 0.4f, level = (target.y - (sh.y - 0.15f)) / 0.15f;
        far = far < 0 ? 0 : far > 1 ? 1 : far, high = high < 0 ? 0 : high > 1 ? 1 : high;
        level = level < 0 ? 0 : level > 1 ? 1 : level;
        float w = fmaxf(far * level, high);
        vr_vec3 a0 = vr_v3_sub(sh, cp), a1 = vr_v3_sub(target, cp);
        if (w > 0 && vr_v3_len(a0) > 0.02f && vr_v3_len(a1) > 0.02f) {
            vr_quat full = vr_quat_arc(vr_v3_normalize(a0), vr_v3_normalize(a1));
            float ang = 2 * acosf(fminf(1, fabsf(full.w)));
            float want = fminf(ang, 20 * 3.14159265f / 180) * w;
            if (ang > 1e-4f) {
                vr_vec3 ax = vr_v3_normalize(vr_v3(full.x, full.y, full.z));
                if (full.w < 0) ax = vr_v3_scale(ax, -1);
                vr_quat c = vr_quat_axis_angle(ax, want);
                k.arm[side].clav = c, k.arm[side].clav_pivot = cp, k.arm[side].clav_deg = want * 57.29578f;
                sh = vr_v3_add(cp, vr_quat_rotate(c, a0));
                el = vr_v3_add(cp, vr_quat_rotate(c, vr_v3_sub(el, cp)));
                wr = vr_v3_add(cp, vr_quat_rotate(c, vr_v3_sub(wr, cp)));
                q0 = vr_quat_mul(c, q0);
            }
        }
    }
    // the elbow bends down and out (away from the body: from the other shoulder towards this one)
    vr_vec3 pole = vr_v3_add(vr_v3(0, -1, 0), vr_v3_scale(out, 0.6f));
    static float elbow_drop = -1;   // SAVAGEWIND_VR_ELBOW_DROP: how far under the hand (and shoulder: first rig) the elbow stays at least (m)
    if (elbow_drop < 0) elbow_drop = getenv("SAVAGEWIND_VR_ELBOW_DROP") ? (float)atof(getenv("SAVAGEWIND_VR_ELBOW_DROP")) : 0.10f;
    if (rig) {
        // as BetterVR's (skeleton.cpp: down, out, a little back, turned with the body), and where the controller points
        // from: the elbow lies behind the fist along the controller's pointing line - its direction only, never its
        // roll, so turning a hand palm up or down turns the forearm and leaves the elbow where it is
        // (SAVAGEWIND_VR_ELBOW_FOLLOW: how much the pointing pulls, default 1)
        static float follow = -1;
        if (follow < 0) follow = getenv("SAVAGEWIND_VR_ELBOW_FOLLOW") ? (float)atof(getenv("SAVAGEWIND_VR_ELBOW_FOLLOW")) : 1;
        pole = vr_v3_add(vr_v3_add(vr_v3(0, -1, 0), vr_v3_scale(out, 1.0f)), vr_v3_scale(back, 0.5f));
        float fl = vr_v3_len(vr_v3_sub(wr, el));
        vr_vec3 point = vr_quat_rotate(hw.q, vr_v3(0, 0, -1));
        if (follow > 0 && fl > 1e-4f) {
            vr_vec3 want = vr_v3_sub(target, vr_v3_scale(point, fl));
            // (under the hand by at least a little: a controller pointed down would put the elbow over it, a
            // chicken wing; over the shoulder is fine - hands overhead raise the elbows)
            want.y = fminf(want.y, target.y - elbow_drop * 0.5f);
            vr_vec3 wv = vr_v3_sub(want, sh);
            float wl = vr_v3_len(wv);
            if (wl > 1e-4f) pole = vr_v3_add(vr_v3_scale(vr_v3_normalize(pole), 1), vr_v3_scale(wv, follow / wl));
        }
        // smoothed (a quarter of the way in 30 ms): a pole that jumps with the hand's jitter makes the elbow flutter
        float pl = vr_v3_len(pole);
        if (pl > 1e-4f) {
            pole = vr_v3_scale(pole, 1 / pl);
            if (k.rig[side].pole_ok) {
                float kf = 1 - expf(-dt / 0.10f);
                vr_vec3 p = vr_v3_add(k.rig[side].pole, vr_v3_scale(vr_v3_sub(pole, k.rig[side].pole), kf));
                if (vr_v3_len(p) > 1e-4f) pole = vr_v3_normalize(p);
            }
            k.rig[side].pole = pole, k.rig[side].pole_ok = true;
        }
    } else {
        // (the first rig) ... and towards where the hand puts it: the forearm as the animation holds it from the hand (in
        // the wrist bone's frame), carried by the controller's turn, says where the elbow wants to be. SAVAGEWIND_VR_
        // ELBOW_FOLLOW=0: the fixed pole alone
        static float follow = -1;
        if (follow < 0) follow = getenv("SAVAGEWIND_VR_ELBOW_FOLLOW") ? (float)atof(getenv("SAVAGEWIND_VR_ELBOW_FOLLOW")) : 3;
        if (follow > 0 && vr_v3_len(vr_v3_sub(wr, el)) > 1e-4f) {
            vr_vec3 bk = vr_quat_rotate(vr_quat_conj(q0), vr_v3_normalize(vr_v3_sub(el, wr)));   // (wrist -> elbow, local)
            vr_vec3 want = vr_v3_add(target, vr_v3_scale(vr_quat_rotate(vr_quat_mul(hw.q, wrist_correction(side)), bk),
                                                         vr_v3_len(vr_v3_sub(wr, el))));
            want.y = fminf(want.y, fminf(target.y, sh.y) - elbow_drop);
            vr_vec3 w = vr_v3_sub(want, sh);
            float wl = vr_v3_len(w);
            if (wl > 1e-4f) pole = vr_v3_add(vr_v3_scale(pole, 0.5f), vr_v3_scale(w, follow / wl));
        }
    }
    vr_ik_result r = vr_ik_two_bone(sh, el, wr, target, pole, 0.75f);
    // the hand: turned as the controller is (BetterVR's skeleton.cpp: the grip's orientation times a fixed turn per hand
    // that takes the controller's axes to the wrist bone's), from where the arm's turns leave it; SAVAGEWIND_VR_HAND_ROLL
    // =0: only pointed where the controller points, its roll the animation's (as before)
    static int roll = -1;
    if (roll < 0) roll = !(getenv("SAVAGEWIND_VR_HAND_ROLL") && *getenv("SAVAGEWIND_VR_HAND_ROLL") == '0');
    vr_quat hand_turn, twist = vr_quat_identity(), twist_half = vr_quat_identity();
    if (roll) {
        vr_quat after = vr_quat_mul(r.lower, vr_quat_mul(r.upper, q0));
        vr_quat target_q = vr_quat_mul(hw.q, wrist_correction(side));
        hand_turn = vr_quat_normalize(vr_quat_mul(target_q, vr_quat_conj(after)));
        // the roll about the forearm turns the forearm, not the wrist alone (a wrist can't twist): the turn's twist about
        // the forearm's axis goes to the twist bone at the wrist in full and the forearm's middle by half, so a hand
        // turned palm up rolls the forearm with it (BetterVR's arm has the same split) rather than folding at the wrist
        static int fore_twist = -1;
        if (fore_twist < 0) fore_twist = !(getenv("SAVAGEWIND_VR_FOREARM_TWIST") && *getenv("SAVAGEWIND_VR_FOREARM_TWIST") == '0');
        vr_vec3 ax = vr_v3_normalize(vr_v3_sub(r.wrist, r.elbow));
        float along = hand_turn.x * ax.x + hand_turn.y * ax.y + hand_turn.z * ax.z;
        if (hand_turn.w < 0) along = -along;   // (the same turn with w >= 0)
        float ang = 2 * atan2f(along, fabsf(hand_turn.w));   // (swing-twist: the twist's angle about ax, signed)
        if (rig) {
            // continuous from the last frame (the angle wraps at 180 deg: the forearm flipped round), then eased into
            // at most 110 deg either way on the forearm (a soft limit: no step where a hard one lets go) - the hand bone
            // still takes the whole turn
            if (k.rig[side].twist_ok) {
                while (ang - k.rig[side].twist > 3.14159265f) ang -= 2 * 3.14159265f;
                while (ang - k.rig[side].twist < -3.14159265f) ang += 2 * 3.14159265f;
            }
            // (past 210 deg either way no forearm goes: back to the principal angle - never wound up the wrong way, as
            // after a pause taken at an extreme)
            if (fabsf(ang) > 3.14159265f + 0.5f) ang = remainderf(ang, 2 * 3.14159265f);
            {   // (debugging) SAVAGEWIND_VR_LOG_TWIST=1: the forearm's roll as solved, signed, raw and kept continuous
                static int lt = -1;
                if (lt < 0) lt = getenv("SAVAGEWIND_VR_LOG_TWIST") != NULL;
                if (lt && fabsf(ang - k.rig[side].twist) > 0.05f)
                    LOG("[vr-twist] %s raw %.0f continuous %.0f (was %.0f)\n", side ? "right" : "left",
                        2 * atan2f(along, fabsf(hand_turn.w)) * 57.29578f, ang * 57.29578f, k.rig[side].twist * 57.29578f);
            }
            k.rig[side].twist = ang, k.rig[side].twist_ok = true;
            const float lim = 110 * 3.14159265f / 180;
            ang = lim * tanhf(ang / lim);
        }
        if (fore_twist && (fabsf(along) > 1e-5f || fabsf(hand_turn.w) > 1e-5f)) {
            twist = vr_quat_axis_angle(ax, ang);
            twist_half = vr_quat_axis_angle(ax, ang * 0.5f);
        }
    } else {
        vr_vec3 fore = vr_v3_normalize(vr_v3_sub(r.wrist, r.elbow));
        vr_vec3 point = vr_quat_rotate(hw.q, vr_v3(0, 0, -1));
        hand_turn = vr_quat_arc(fore, vr_v3_normalize(point));
    }
    {   // (for a headset's first runs) the first minute with the arms active, every ~5 s: each controller as the runtime
        // gives it, so a hand turned the wrong way can be told from the log. Use wall time: guest frames include pauses,
        // loading and reboots, and are not a reliable measure of how long the player has had to collect a pose.
        uint64_t now = plat_now_ns();
        if (!hand_log_started_ns) hand_log_started_ns = now;
        if (now >= hand_log_started_ns && now - hand_log_started_ns < 60000000000ull &&
            (!hand_log_at_ns[side] || now - hand_log_at_ns[side] >= 5000000000ull)) {
            hand_log_at_ns[side] = now;
            vr_vec3 ahead = vr_quat_rotate(hand.q, vr_v3(0, 0, -1)), up = vr_quat_rotate(hand.q, vr_v3(0, 1, 0));
            LOG("[vr-hands] %s grip q %.3f %.3f %.3f %.3f (room: ahead %.2f %.2f %.2f, up %.2f %.2f %.2f) at %.2f %.2f %.2f\n",
                side ? "right" : "left", hand.q.x, hand.q.y, hand.q.z, hand.q.w, ahead.x, ahead.y, ahead.z, up.x, up.y, up.z,
                hand.p.x, hand.p.y, hand.p.z);
        }
    }
    k.arm[side].ok = true;
    k.arm[side].shoulder = sh, k.arm[side].elbow = r.elbow, k.arm[side].wrist = r.wrist;
    k.arm[side].upper = r.upper, k.arm[side].lower = r.lower, k.arm[side].hand = hand_turn;
    k.arm[side].twist = twist, k.arm[side].twist_half = twist_half;
    vr_quat swing = vr_quat_mul(hand_turn, vr_quat_conj(twist));   // (what the wrist itself bends)
    k.arm[side].bend = 2 * acosf(fminf(1, fabsf(swing.w))) * 57.29578f;
    k.arm[side].roll = 2 * acosf(fminf(1, fabsf(twist.w))) * 57.29578f;
    k.arm[side].miss = vr_v3_len(vr_v3_sub(r.wrist, target));
    vr_vec3 e = vr_v3_sub(r.elbow, sh);
    k.arm[side].elbow_down = -e.y, k.arm[side].elbow_out = vr_v3_dot(e, out), k.arm[side].elbow_back = vr_v3_dot(e, back);
}

// this frame's arm turns on a skeleton's arm bones (Link's or his armor's), by their parts
static void apply_arms(uint64_t world, const skel_info *s) {
    for (int i = 0; i < s->n; i++) {
        uint8_t p = s->part[i] & 0x07;
        int side = s->part[i] >> 4;
        if (!((p >= PART_UPPER && p <= PART_HAND) || p == PART_CLAV) || !k.arm[side].ok) continue;
        float *m = bone_m(world, i);
        vr_bm_turn(m, k.arm[side].clav, k.arm[side].clav_pivot, k.arm[side].clav_pivot);
        if (p == PART_CLAV) continue;
        vr_bm_turn(m, k.arm[side].upper, k.arm[side].shoulder, k.arm[side].shoulder);
        if (p >= PART_LOWER) vr_bm_turn(m, k.arm[side].lower, k.arm[side].elbow, k.arm[side].elbow);
        if (p == PART_LOWER)   // (about the forearm's own line: the wrist stays where it was solved)
            vr_bm_turn(m, (s->part[i] & PART_TWIST) ? k.arm[side].twist : k.arm[side].twist_half, k.arm[side].elbow,
                       k.arm[side].elbow);
        if (p == PART_HAND) vr_bm_turn(m, k.arm[side].hand, k.arm[side].wrist, k.arm[side].wrist);
    }
}

// The player's movement flags (the player object, PlayerInfo + 0x60, at + 0xcec on 1.9.0: found with the hand simulator
// by diffing the object standing, crouched, jumping and climbing; the bits are BetterVR's PlayerMoveBitFlags from the
// Wii U: 1 << 2 crouched, 1 << 1 in the air, 1 << 4 | 1 << 7 climbing a wall, 1 << 10 swimming, 1 << 15 the paraglider,
// 1 << 19 ragdoll, 1 << 20 on a horse). false when the player is not there
bool vr_player_flags(uint32_t *f) {
    if (!g_site.player_info) return false;
    uint64_t pinfo = rd64(g_site.player_info);
    uint64_t player = rd64(pinfo + 0x60);
    if (!GUEST_OK(player)) return false;
    *f = *(const uint32_t *)(uintptr_t)(player + 0xcec);
    return true;
}
// Link carries something lifted over his head (a pot, a rock, a bomb): the grab action's enter (main+0xc7721c, its
// animation GrabUp) sets bit 17 of the player's + 0xc40 and the throw (main+0xc76d60) clears it
bool vr_player_carrying(void) {
    if (!g_site.player_info) return false;
    uint64_t pinfo = rd64(g_site.player_info);
    uint64_t player = rd64(pinfo + 0x60);
    if (!GUEST_OK(player)) return false;
    if (*(const uint32_t *)(uintptr_t)(player + 0xc40) & 0x20000u) return true;
    // a rune's bomb in his hands (round or square) does not go through the grab action: the game shows "Drop A, Throw
    // R" and the trigger's A dropped it at his feet (2026-11-24). While one is held the player's + 0xd44 names it "Bomb"
    // ("None" otherwise; bit 13 of + 0xc48 is set with it), back to "None" once thrown
    return !memcmp((const void *)(uintptr_t)(player + 0xd44), "Bomb", 5);
}
#define VR_MOVE_SPECIAL ((1u << 4) | (1u << 7) | (1u << 10) | (1u << 15) | (1u << 19) | (1u << 20))

// First person: Link's body turned to where the head looks (BetterVR's Skl_Root override), about where he stands, so
// his shoulders are under the eye whichever way he runs - running sideways his body faced the run and the arms could
// not reach the controllers (up to 20 cm short) - and what hangs from him (armor, the weapons on his back) with it.
// SAVAGEWIND_VR_BODY_FOLLOW=0: his body as the game turns it
static void body_turn(uint64_t world, const skel_info *s, bool arms, const double *lp) {
    static int on = -1;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_BODY_FOLLOW") && *getenv("SAVAGEWIND_VR_BODY_FOLLOW") == '0');
    k.body_ok = false;
    float view;
    if (!on || !arms || !vr_view_heading(&view)) return;
    // (not climbing, swimming, gliding, riding or tumbling: his body faces the wall, the water, the horse - BetterVR
    // leaves those to the game too)
    uint32_t mf;
    if (vr_player_flags(&mf) && (mf & VR_MOVE_SPECIAL)) return;
    {   // (SAVAGEWIND_VR_LADDER_TEST=1, vr_input.c: his body left as the game turns it, as on a ladder)
        static int test = -1;
        if (test < 0) test = getenv("SAVAGEWIND_VR_LADDER_TEST") && *getenv("SAVAGEWIND_VR_LADDER_TEST") == '1';
        if (test) return;
    }
    const float *r = bone_m(world, 0);
    float body = atan2f(r[8], r[10]);   // (his root's +Z: the way he faces)
    float d = remainderf(view - body, 2 * 3.14159265f);
    k.body_q = vr_quat_yaw(d);
    k.body_pivot = vr_v3((float)lp[0], r[13], (float)lp[2]);
    k.body_ok = true;
    k.body_turned = d;
    k.body_frame = k.frames + 1;
    k.body_npre = s->n < 160 ? s->n : 160;
    for (int i = 0; i < k.body_npre; i++) memcpy(k.body_pre[i], bone_m(world, i) + 12, sizeof k.body_pre[i]);
    for (int i = 0; i < s->n; i++) vr_bm_turn(bone_m(world, i), k.body_q, k.body_pivot, k.body_pivot);
}

// the same turn on a model that hangs from him (an armor piece, a weapon on his back): its root on one of his bones
// as the game placed them (within 25 cm: a frame's run), not another's weapon passing near him
static void body_turn_other(uint64_t world, uint64_t skel) {
    if (!k.body_ok || k.body_frame + 2 < k.frames) return;
    vr_vec3 p = vr_bm_pos(bone_m(world, 0));
    bool on = false;
    for (int i = 0; i < k.body_npre && !on; i++) {
        float dx = p.x - k.body_pre[i][0], dy = p.y - k.body_pre[i][1], dz = p.z - k.body_pre[i][2];
        on = dx * dx + dy * dy + dz * dz < 0.25f * 0.25f;
    }
    if (!on) return;
    int nb = bone_count(skel);
    for (int i = 0; i < nb; i++) vr_bm_turn(bone_m(world, i), k.body_q, k.body_pivot, k.body_pivot);
}

static void hide_head(uint64_t world, const skel_info *s, bool every) {
    for (int i = 0; i < s->n; i++)
        if (every || (s->part[i] & 0x07) == PART_HEAD) squeeze(bone_m(world, i));
}

static void skel_calc(uint64_t model);
static void pair_pose(uint64_t model);
void vr_skel_calc(uint64_t model) {
    skel_calc(model);
    pair_pose(model);
}

// Stereo: Link the same in both eyes. A pair's two eyes are two game frames half a step apart, both drawn from the
// left frame's place (vr_camera.c, the pair's anchor), and the right frame animated him again: walking, his body, belt
// and legs were somewhere else in each eye, while the arms (the controllers, kept once a pair) matched - "extreme
// jitter when the character walks, the whole body flickers" (2026-11-27; tools/vr/test_vr_body_walk.py: 18-28% of the
// lower picture differing between the eyes, 2% standing). The left frame keeps the bones of Link and of the models on
// him (armor, weapons) as they are drawn; the right frame of the same pair draws those. SAVAGEWIND_VR_PAIR_POSE=0: off
// (also off with SAVAGEWIND_VR_PAIR_ANCHOR=0, each eye its own place)
#define PAIR_MODELS 32
static struct { uint64_t model, pair; int n; float (*m)[16]; } pair_models[PAIR_MODELS];
static uint64_t pair_poses_kept;
uint64_t vr_pair_poses_kept(void) { return pair_poses_kept; }

static void pair_pose(uint64_t model) {
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("SAVAGEWIND_VR_PAIR_POSE"), *a = getenv("SAVAGEWIND_VR_PAIR_ANCHOR");
        on = !(e && *e == '0') && !(a && *a == '0');
        if (!on) LOG("[vr] stereo: Link animated in each eye's own frame (SAVAGEWIND_VR_PAIR_POSE=0)\n");
    }
    extern int vr_frame_eye_peek(uint64_t *pair);   // (vr_camera.c)
    uint64_t pair;
    int eye;
    if (!on || !vr_world_view() || (eye = vr_frame_eye_peek(&pair)) < 0) return;
    uint64_t skel = rd64(model + 0x118);
    if (!GUEST_OK(skel)) return;
    uint64_t world = rd64(skel + 0x20);
    if (!GUEST_OK(world)) return;
    int n = bone_count(skel);
    if (!n) return;
    if (model != k.model) {   // (one on Link: an armor piece or a weapon, within reach of him)
        const char *name = fmdl_name(rd64(model + 0xe0));
        if (!name || (strncmp(name, "Armor_", 6) && strncmp(name, "Weapon_", 7))) return;
        double lp[3];
        if (!control_cell_get(&control_cell_player_pos, lp)) return;
        const float *r = bone_m(world, 0);
        float dx = r[12] - (float)lp[0], dy = r[13] - (float)lp[1], dz = r[14] - (float)lp[2];
        if (dx * dx + dy * dy + dz * dz > 9.0f) return;
    }
    int at = -1, free_at = -1, old_at = 0;
    for (int i = 0; i < PAIR_MODELS; i++) {
        if (pair_models[i].model == model) { at = i; break; }
        if (!pair_models[i].model && free_at < 0) free_at = i;
        if (pair_models[i].pair < pair_models[old_at].pair) old_at = i;
    }
    if (eye == 0) {
        if (at < 0) at = free_at >= 0 ? free_at : old_at;
        if (pair_models[at].n < n) free(pair_models[at].m), pair_models[at].m = malloc(sizeof(float[16]) * (size_t)n);
        if (!pair_models[at].m) { pair_models[at].model = 0, pair_models[at].n = 0; return; }
        pair_models[at].model = model, pair_models[at].pair = pair;
        if (pair_models[at].n < n) pair_models[at].n = n;
        memcpy(pair_models[at].m, (const void *)(uintptr_t)world, sizeof(float[16]) * (size_t)n);
    } else if (at >= 0 && pair_models[at].pair == pair && pair_models[at].n >= n) {
        memcpy((void *)(uintptr_t)world, pair_models[at].m, sizeof(float[16]) * (size_t)n);
        if (model == k.model) pair_poses_kept++;
    }
}

static void skel_calc(uint64_t model) {
    if (k.hide < 0) {
        const char *e = getenv("SAVAGEWIND_VR_HIDE_HEAD"), *a = getenv("SAVAGEWIND_VR_ARMS"),
                   *l = getenv("SAVAGEWIND_VR_LOG_ARMS");
        k.hide = e ? atoi(e) : 2;   // (2: in first person)
        k.arms = a ? atoi(a) : 2;
        k.log_arms = l ? atoi(l) : 0;
    }
    // first person in play only: not in menus (the inventory shows Link as he is), not in third person or an event on
    // its own camera
    bool fp = vr_enabled() && vr_world_view() && vr_first_person_now();
    bool hide = k.hide == 1 || (k.hide == 2 && fp), arms = k.arms && fp && !vr_event_anim_hands() &&   // (an event may leave his hands to its animation)
                                                       !vr_rune_aiming_now();   // (Magnesis takes its target with his hand as the game holds it)
    if (!hide && !arms && !vr_enabled()) return;   // (in VR the weapons are watched even when nothing is changed)
    uint64_t skel = rd64(model + 0x118);
    if (!GUEST_OK(skel)) return;
    uint64_t world = rd64(skel + 0x20);
    if (!GUEST_OK(world)) return;
    const char *n = model == k.model ? "Link" : fmdl_name(rd64(model + 0xe0));
    if (!n) return;
    static int log_models = -1;   // (debugging) SAVAGEWIND_VR_LOG_MODELS=<prefix>: those models' root position, each frame
    static const char *log_prefix;
    if (log_models < 0) log_prefix = getenv("SAVAGEWIND_VR_LOG_MODELS"), log_models = log_prefix != NULL;
    if (log_models && !strncmp(n, log_prefix, strlen(log_prefix))) {
        const float *m = bone_m(world, 0);
        LOG("[vr-model] %llu %s %llx %.3f %.3f %.3f\n", (unsigned long long)k.frames, n, (unsigned long long)model, m[12],
            m[13], m[14]);
    }
    if (!strcmp(n, "Link")) {
        // the player's Link only: another (a picture's, a menu's) is far from where the player is
        double lp[3];
        if (control_cell_get(&control_cell_player_pos, lp)) {
            const float *r = bone_m(world, 0);
            float dx = r[12] - (float)lp[0], dy = r[13] - (float)lp[1], dz = r[14] - (float)lp[2];
            if (dx * dx + dy * dy + dz * dz > 9.0f) return;
        }
        bool first = model != k.model;
        k.model = model;
        link_seen_ns = plat_now_ns();
        const skel_info *s = info_of(skel, first && getenv("SAVAGEWIND_VR_LOG_BONES"));
        if (first)
            LOG("[vr] Link's skeleton: model %llx, %d bones, arms %d %d %d / %d %d %d\n", (unsigned long long)model, s->n,
                s->arm1[0], s->arm2[0], s->wrist[0], s->arm1[1], s->arm2[1], s->wrist[1]);
        if (s->chest >= 0) {
            const float *m = bone_m(world, s->chest);
            k.chest[0] = m[12], k.chest[1] = m[13], k.chest[2] = m[14], k.chest_ok = true;
        }
        double fp_lp[3];
        bool have_lp = control_cell_get(&control_cell_player_pos, fp_lp);
        if (s->head >= 0) k.head_w = vr_bm_pos(bone_m(world, s->head)), k.head_frame = k.frames;
        {   // the way he faces (his root's +Z), for the eye while he climbs (vr_camera.c)
            const float *r0 = bone_m(world, 0);
            float fy = atan2f(r0[8], r0[10]);
            uint32_t fb;
            memcpy(&fb, &fy, 4);
            atomic_store_explicit(&face_bits, fb, memory_order_relaxed);
            atomic_store_explicit(&face_ns, plat_now_ns(), memory_order_relaxed);
        }
        if (s->head >= 0 && have_lp) {   // (before it is hidden)
            float y = bone_m(world, s->head)[13] - (float)fp_lp[1];
            uint32_t yb;
            memcpy(&yb, &y, 4);
            atomic_store_explicit(&head_rel_bits, yb, memory_order_relaxed);
            atomic_store_explicit(&head_seen_ns, plat_now_ns(), memory_order_relaxed);
            if (k.log_arms > 0 && k.frames % (uint64_t)(k.log_arms * 4) == 0) LOG("[vr-head] %.3f m above his feet\n", y);
        }
        float before[2][16];
        for (int side = 0; side < 2; side++)
            if (s->weapon[side] >= 0) memcpy(before[side], bone_m(world, s->weapon[side]), sizeof before[side]);
        body_turn(world, s, arms && have_lp, fp_lp);
        if (arms) {
            for (int side = 0; side < 2; side++) solve_arm(world, s, side);
            apply_arms(world, s);
            for (int side = 0; side < 2; side++)
                if (s->wrist[side] >= 0) k.wrist_final[side] = vr_bm_pos(bone_m(world, s->wrist[side]));
            k.wrist_final_frame = k.frames;
            if ((k.arm[0].ok || k.arm[1].ok) && !k.said_arms) LOG("[vr] Link's arms follow the controllers\n"), k.said_arms = true;
            if (k.log_arms > 0 && k.frames % (uint64_t)k.log_arms == 0)
                LOG("[vr-arms] left %s %.3f m, right %s %.3f m from the target (wrist bend %.0f / %.0f deg, forearm "
                    "roll %.0f / %.0f; body turned %.0f deg; elbow down/out/back %.2f %.2f %.2f / %.2f %.2f %.2f m; "
                    "clavicle %.0f / %.0f deg)\n", k.arm[0].ok ? "ok" : "--", k.arm[0].miss,
                    k.arm[1].ok ? "ok" : "--", k.arm[1].miss, k.arm[0].bend, k.arm[1].bend, k.arm[0].roll, k.arm[1].roll,
                    k.body_ok ? k.body_turned * 57.29578f : 0.0f, k.arm[0].elbow_down, k.arm[0].elbow_out,
                    k.arm[0].elbow_back, k.arm[1].elbow_down, k.arm[1].elbow_out, k.arm[1].elbow_back, k.arm[0].clav_deg,
                    k.arm[1].clav_deg);
        } else {
            k.arm[0].ok = k.arm[1].ok = false;
        }
        for (int side = 0; side < 2; side++) {   // (the hands' change, for the weapons there)
            k.hand[side].ok = false;
            if (s->weapon[side] < 0) continue;
            memcpy(k.hand[side].before, before[side], sizeof before[side]);
            float inv[16];
            if (vr_bm_inverse(before[side], inv)) {
                vr_bm_mul(bone_m(world, s->weapon[side]), inv, k.hand[side].delta);
                memcpy(k.hand[side].after, bone_m(world, s->weapon[side]), sizeof k.hand[side].after);
                k.hand[side].ok = true;
            }
        }
        if (hide) {
            hide_head(world, s, false);
            if (!k.said_head) LOG("[vr] Link's head hidden (first person)\n"), k.said_head = true;
        }
        k.frames++;
        return;
    }
    if (arms && strncmp(n, "Weapon_", 7) && strncmp(n, "Armor_", 6) && carried_in_hands(world, skel, model, n)) return;
    // a weapon (sword, bow, shield...): in a hand when it sits at that hand's Weapon_ bone as the animation left it;
    // it is then carried by the hand's change (the arms' turns), and tells what that hand holds
    if (!strncmp(n, "Weapon_", 7) && !strstr(n, "Quiver")) {   // (the quiver stays on his back)
        float *root = bone_m(world, 0);
        vr_vec3 p = vr_bm_pos(root);
        bool in_hand = false;
        k.seen[weapon_kind(n)] = k.frames;
        for (int side = 0; side < 2; side++) {
            if (!k.hand[side].ok) continue;
            vr_vec3 h = vr_bm_pos(k.hand[side].before);
            float dist = vr_v3_len(vr_v3_sub(p, h));
            if (getenv("SAVAGEWIND_VR_LOG_WEAPONS") && k.frames % 120 == 0)
                LOG("[vr] %s %.3f m from the %s hand\n", n, dist, side ? "right" : "left");
            // (one in the hand sits on the bone; one on the back only passes near it.) Already at the turned hand: its
            // actor was moved there (vr_weapon_bind), so the model is in that hand and needs no carrying
            bool moved = false;
            if (dist > 0.03f) {
                vr_vec3 ha = vr_bm_pos(k.hand[side].after);
                if (!arms || vr_v3_len(vr_v3_sub(p, ha)) > 0.06f) continue;
                moved = true;
            }
            int kind = weapon_kind(n);
            // two at one hand this frame (a bow and the arrow's...): the bow first, then a melee weapon, then a shield
            static const int prio[] = {0, 2, 3, 1, 0};
            if (k.hand[side].held_frame == k.frames && k.hand[side].model != model && prio[kind] < prio[k.hand[side].held])
                kind = -1;
            if (kind >= 0 && (k.hand[side].held_frame != k.frames || k.hand[side].model != model)) {
                if (kind != k.hand[side].held && getenv("SAVAGEWIND_VR_TRACE_GESTURES"))
                    LOG("[vr] %s hand holds %s (%d)\n", side ? "right" : "left", n, kind);
                k.hand[side].held = kind, k.hand[side].model = model;
                k.hand[side].two_handed = kind == VR_SKEL_MELEE && (strstr(n, "_Lsword_") || strstr(n, "_Spear_"));
            }
            k.hand[side].held_frame = k.frames;
            if (moved && vr_two_hand_latched() && getenv("SAVAGEWIND_VR_TRACE_TWO_HANDS") && k.frames % 30 == 0) {
                vr_pose ho, hw;   // (tests) how far the model's blade is from the line to the other hand
                if (vr_hand_pose(1 - side, &ho) && vr_tracking_to_world(&ho, &hw)) {
                    vr_vec3 z = vr_v3_normalize(vr_v3(root[8], root[9], root[10]));
                    vr_vec3 to = vr_v3_normalize(vr_v3_sub(hw.p, p));
                    float c = fabsf(vr_v3_dot(z, to));
                    LOG("[vr-two-hands] model blade %.1f deg from the other hand\n", acosf(c > 1 ? 1 : c) * 57.29578f);
                }
            }
            if (arms && !moved) {
                int nb = bone_count(skel);
                for (int i = 0; i < nb; i++) vr_bm_mul(k.hand[side].delta, bone_m(world, i), bone_m(world, i));
            }
            // the bow's string drawn to the string hand (BetterVR's hook_PoseBowString): while the bow is drawn in first
            // person, its two nock bones (String_L_2, String_R_2: where the string's halves meet) are put at the other
            // hand, so the string follows the hand however far it pulls (at most 0.9 m from the bow's grip), not the
            // draw animation's own pull. SAVAGEWIND_VR_BOW_STRING=0: the animation's string
            static int string_on = -1;
            if (string_on < 0) string_on = !(getenv("SAVAGEWIND_VR_BOW_STRING") && *getenv("SAVAGEWIND_VR_BOW_STRING") == '0');
            if (string_on && arms && strstr(n, "_Bow_") && vr_drawing()) {
                static uint64_t nock_skel;
                static int nock[2] = {-1, -1};
                if (nock_skel != skel) {
                    nock_skel = skel, nock[0] = nock[1] = -1;
                    int nb = bone_count(skel);
                    for (int i = 0; i < nb; i++) {
                        const char *b = bone_name(skel, i);
                        if (b && !strcmp(b, "String_L_2")) nock[0] = i;
                        if (b && !strcmp(b, "String_R_2")) nock[1] = i;
                    }
                    LOG("[vr] bow %s: its string's nock bones %d %d\n", n, nock[0], nock[1]);
                }
                vr_pose hs, ws;
                if (nock[0] >= 0 && nock[1] >= 0 && vr_hand_pose(1 - side, &hs) && vr_tracking_to_world(&hs, &ws)) {
                    vr_vec3 grip = vr_bm_pos(bone_m(world, 0)), to = vr_v3_sub(ws.p, grip);
                    float l = vr_v3_len(to);
                    if (l > 0.9f) to = vr_v3_scale(to, 0.9f / l);
                    vr_vec3 at = vr_v3_add(grip, to);
                    for (int s2 = 0; s2 < 2; s2++) {
                        float *m = bone_m(world, nock[s2]);
                        m[12] = at.x, m[13] = at.y, m[14] = at.z;
                    }
                    string_frames++;
                    if (getenv("SAVAGEWIND_VR_LOG_ARROWS") && k.frames % 30 == 0)
                        LOG("[vr-bow] string at the hand, %.2f m from the grip\n", vr_v3_len(to));
                }
            }
            // the arrow nocked while the bow is drawn: laid on the line from the string hand to the bow hand, the way
            // it flies (vr_arrow_aim), not turned with the string hand's wrist - it pointed anywhere (2026-11-20). Its
            // model runs along its +Z (BetterVR's bow.cpp), turned about its own root at the string hand.
            // SAVAGEWIND_VR_ARROW_ON_BOW=0: as the hand holds it
            static int on_bow = -1;
            if (on_bow < 0) on_bow = !(getenv("SAVAGEWIND_VR_ARROW_ON_BOW") && *getenv("SAVAGEWIND_VR_ARROW_ON_BOW") == '0');
            if ((on_bow || getenv("SAVAGEWIND_VR_LOG_ARROWS")) && arms && strstr(n, "_Arrow") && vr_drawing() &&
                k.hand[1 - side].held == VR_SKEL_BOW &&
                k.hand[1 - side].held_frame + 8 >= k.frames) {
                vr_pose hb, hs, wb, ws;
                if (vr_hand_pose(1 - side, &hb) && vr_hand_pose(side, &hs) && vr_tracking_to_world(&hb, &wb) &&
                    vr_tracking_to_world(&hs, &ws) && vr_v3_len(vr_v3_sub(wb.p, ws.p)) > 0.08f) {
                    vr_vec3 want = vr_v3_normalize(vr_v3_sub(wb.p, ws.p));
                    float *r0 = bone_m(world, 0);
                    vr_vec3 z = vr_v3(r0[8], r0[9], r0[10]);
                    if (getenv("SAVAGEWIND_VR_LOG_ARROWS") && k.frames % 30 == 0) {
                        float c = vr_v3_dot(vr_v3_normalize(z), want);
                        LOG("[vr-arrow] nocked %s: its +Z %.1f deg from the line to the bow\n", n,
                            acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f);
                    }
                    if (on_bow == 1 && vr_v3_len(z) > 1e-4f) {
                        vr_quat q = vr_quat_arc(vr_v3_normalize(z), want);
                        vr_vec3 pivot = vr_bm_pos(r0);
                        int nb = bone_count(skel);
                        for (int i = 0; i < nb; i++) vr_bm_turn(bone_m(world, i), q, pivot, pivot);
                        if (getenv("SAVAGEWIND_VR_LOG_ARROWS") && k.frames % 30 == 0) {
                            float c = vr_v3_dot(vr_v3_normalize(vr_v3(r0[8], r0[9], r0[10])), want);
                            LOG("[vr-arrow] laid on the bow: %.1f deg off\n", acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f);
                        }
                    }
                }
            }
            in_hand = true;
            break;
        }
        if (!in_hand && arms) body_turn_other(world, skel);   // (on his back, his hip: turned with his body)
        return;
    }
    if (strncmp(n, "Armor_", 6)) return;
    size_t len = strlen(n);
    bool head_armor = (len > 5 && !strcmp(n + len - 5, "_Head")) || strstr(n, "_Extra_");
    const skel_info *s = info_of(skel, false);
    if (hide && head_armor) hide_head(world, s, true);
    else if (arms) {
        body_turn_other(world, skel);   // (his body's turn first, then the arms' as on him)
        apply_arms(world, s);           // (sleeves and gloves follow the arms)
        for (int side = 0; side < 2; side++) {
            if (s->wrist[side] < 0 || !k.arm[side].ok || k.wrist_final_frame + 2 < k.frames) continue;
            float d = vr_v3_len(vr_v3_sub(vr_bm_pos(bone_m(world, s->wrist[side])), k.wrist_final[side]));
            k.armor_off = d, k.armor_checks++;
            if (d > k.armor_off_max) k.armor_off_max = d;
            if (d > 0.03f) k.armor_off_n++;
        }
    }
}


// ---- the bow aimed by hand (BetterVR's arrow launch override) ----
// An arrow leaving the bow (rt_arrow_aim, main+0x931590): its AI has just made the way it flies, from where the arrow
// is to the shooter's aim point (the camera's line for the player). In first person, for an arrow at Link (his shots),
// that way becomes the line from the string hand through the bow hand, and the aim point moves onto it. SAVAGEWIND_VR_BOW_AIM=head keeps the game's.
static uint64_t arrows_aimed, side_arrows_aimed;
uint64_t vr_arrows_aimed(void) { return arrows_aimed; }
uint64_t vr_side_arrows_aimed(void) { return side_arrows_aimed; }
// the last shot's turn, from the way the game aimed it to the hands' (a multi-shot bow's other arrows get the same)
static struct { vr_quat turn; uint64_t ns; } last_shot;

void vr_arrow_aim(float *dir, const float *origin, float *target) {
    static int log = -1;
    if (log < 0) log = getenv("SAVAGEWIND_VR_LOG_ARROWS") != NULL;
    int mode = vr_opt(VR_OPT_BOW_AIM) == 0;   // (live: the Options screen)
    double lp[3];
    if (!mode || !vr_first_person_now() || !control_cell_get(&control_cell_player_pos, lp)) return;
    vr_vec3 o = vr_v3(origin[0], origin[1], origin[2]);
    float near = vr_v3_len(vr_v3_sub(o, vr_v3((float)lp[0], (float)lp[1] + 1.2f, (float)lp[2])));
    if (log)
        fprintf(stderr, "[vr-arrow] shot from %.2f %.2f %.2f (%.2f m from Link) dir %.3f %.3f %.3f\n", o.x, o.y, o.z, near,
                dir[0], dir[1], dir[2]);
    if (!(near < 3.0f)) return;   // someone else's arrow
    int bow = vr_skel_held(1) == VR_SKEL_BOW ? 1 : 0;   // the bow hand (left unless the bow is in the right)
    vr_pose hb, hs, wb, ws;
    if (!vr_hand_pose(bow, &hb) || !vr_hand_pose(1 - bow, &hs) || !vr_tracking_to_world(&hb, &wb) ||
        !vr_tracking_to_world(&hs, &ws))
        return;
    vr_vec3 d = vr_v3_sub(wb.p, ws.p);
    if (vr_v3_len(d) < 0.08f) d = vr_quat_rotate(wb.q, vr_v3(0, 0, -1));   // hands together: where the bow points
    d = vr_v3_normalize(d);
    last_shot.turn = vr_quat_between(vr_v3_normalize(vr_v3(dir[0], dir[1], dir[2])), d);
    last_shot.ns = plat_now_ns();
    dir[0] = d.x, dir[1] = d.y, dir[2] = d.z;
    // the aim point the arrow then flies at: as far away as the game's, on the new line
    float far = vr_v3_len(vr_v3_sub(vr_v3(target[0], target[1], target[2]), o));
    if (!(far > 5.0f && far < 2000.0f)) far = 100.0f;
    vr_vec3 t = vr_v3_add(o, vr_v3_scale(d, far));
    target[0] = t.x, target[1] = t.y, target[2] = t.z;
    arrows_aimed++;
    if (log) fprintf(stderr, "[vr-arrow] aimed by hand: %.3f %.3f %.3f\n", d.x, d.y, d.z);
}

// A multi-shot bow's other arrows (a Lynel bow's three: rt_arrow_aim_side, main+0x92cfbc): their own shoot step aims
// each at the game's aim point turned by the bow's spread (3 deg), so they flew where the head looked while the main
// arrow went along the bow (2026-11-27). Each gets the main arrow's turn (the game's aim to the hands'), the spread kept
// about the bow's line; the same arrows only (near Link, his shot, the main one aimed in the last second).
void vr_arrow_aim_side(uint64_t actor, float *dir, float *target) {
    static int log = -1;
    if (log < 0) log = getenv("SAVAGEWIND_VR_LOG_ARROWS") != NULL;
    double lp[3];
    if (!GUEST_OK(actor) || vr_opt(VR_OPT_BOW_AIM) != 0 || !vr_first_person_now() ||
        !control_cell_get(&control_cell_player_pos, lp))
        return;
    bool own = *(const uint8_t *)(uintptr_t)(actor + 0xc79);   // (its start its own, not the actor's place)
    const float *op = (const float *)(uintptr_t)(actor + (own ? 0xc30 : 0x3a4));
    vr_vec3 o = own ? vr_v3(op[0], op[1], op[2]) : vr_v3(op[0], op[4], op[8]);   // (a 3x4 row-major matrix's column)
    float near = vr_v3_len(vr_v3_sub(o, vr_v3((float)lp[0], (float)lp[1] + 1.2f, (float)lp[2])));
    uint64_t now = plat_now_ns();
    if (log)
        fprintf(stderr, "[vr-arrow] other arrow from %.2f %.2f %.2f (%.2f m from Link) dir %.3f %.3f %.3f, the main one "
                "%.2f s before\n", o.x, o.y, o.z, near, dir[0], dir[1], dir[2],
                last_shot.ns ? (now - last_shot.ns) / 1e9 : -1.0);
    if (!(near < 3.0f) || !last_shot.ns || now - last_shot.ns > 1000000000ull) return;
    vr_vec3 d = vr_v3_normalize(vr_quat_rotate(last_shot.turn, vr_v3(dir[0], dir[1], dir[2])));
    dir[0] = d.x, dir[1] = d.y, dir[2] = d.z;
    float far = vr_v3_len(vr_v3_sub(vr_v3(target[0], target[1], target[2]), o));
    if (!(far > 5.0f && far < 2000.0f)) far = 100.0f;
    vr_vec3 t = vr_v3_add(o, vr_v3_scale(d, far));
    target[0] = t.x, target[1] = t.y, target[2] = t.z;
    side_arrows_aimed++;
    if (log) fprintf(stderr, "[vr-arrow] other arrow turned with the main one: %.3f %.3f %.3f\n", d.x, d.y, d.z);
}

// The arc the arrow about to go would fly (BetterVR draws it while the bow is drawn): LOCAL space points (metres), from
// the string hand along the line through the bow hand; none unless the bow is in one hand, an arrow nocked in the
// other and the main hand's trigger held, in first person, aiming by hand. The shape is the one measured from the
// game's arrows (a straight run of about 25 m, then a fall growing with the square of the distance: 4.6 m down at
// 50 m), not the bow's own parameters.
// SAVAGEWIND_VR_BOW_ARC=0: no arc.
int vr_bow_arc(float (*pts)[3], int max) {
    int mode = vr_opt(VR_OPT_BOW_ARC);   // (0 the arc, 1 off, 2 the aiming dot only, 3 faint: every third dot)
    int on = mode != 1 && vr_opt(VR_OPT_BOW_AIM) == 0;
    if (!on || !vr_first_person_now()) return 0;
    int bow = vr_skel_held(1) == VR_SKEL_BOW ? 1 : 0;
    if (!vr_drawing()) return 0;   // the main hand's trigger let go: the shot is gone
    if (vr_skel_held(bow) != VR_SKEL_BOW || vr_skel_held(1 - bow) != VR_SKEL_OTHER) return 0;
    vr_pose hb, hs;
    if (!vr_hand_pose(bow, &hb) || !vr_hand_pose(1 - bow, &hs)) return 0;
    vr_vec3 d = vr_v3_sub(hb.p, hs.p);
    if (vr_v3_len(d) < 0.08f) d = vr_quat_rotate(hb.q, vr_v3(0, 0, -1));
    d = vr_v3_normalize(d);
    int n = 0;
    for (int i = 0; n < max; i++) {
        float s = 0.4f + 0.06f * (float)(i * i);   // denser near the bow
        float fall = s > 25.0f ? 0.0074f * (s - 25.0f) * (s - 25.0f) : 0.0f;
        if (s > 80.0f || fall > 20.0f) break;
        if (mode == 2) {   // BetterVR's "only the aiming dot": one dot where the arrow is 10 m out
            if (s < 10.0f) continue;
            s = 10.0f, fall = 0;
        } else if (mode == 3 && i % 3) continue;
        vr_vec3 p = vr_v3_add(hb.p, vr_v3_scale(d, s));
        pts[n][0] = p.x, pts[n][1] = p.y - fall, pts[n][2] = p.z;
        n++;
        if (mode == 2) break;
    }
    return n;
}

// ---- the held weapon's hits where the hand is (BetterVR's hook_ChangeWeaponMtx) ----
// A bound actor's matrix right after its bind made it from the parent's bone (rt_weapon_bind, main+0x1525fb8): its
// model and its physics (the weapon's attack sensor) take it next. A weapon in one of Link's hands (its model the one
// that hand carries, or its matrix on that hand's Weapon_ bone as the animation left it) gets the hand's change from
// the arms' turns, as its model does: it then cuts where the controller swings it. 3x4 row-major (actor + 0x398).
// SAVAGEWIND_VR_WEAPON_HITS=0: the hits stay where the animation holds the weapon.
static uint64_t weapons_moved;
uint64_t vr_weapons_moved(void) { return weapons_moved; }

// ---- both hands on a large sword or a spear (BetterVR's two-hand grip) ----
// With one in the main hand and the other hand empty, the other hand's grip pressed near the weapon's line (within
// 0.25 m of it: the idle sways it, and BetterVR uses its bounding box; 0.08 to 1.2 m from the main hand, either way) takes hold of it until let go: the weapon then turns
// about the main hand's grip so its blade (its actor's z axis, as BetterVR's) points at the other hand, on the side it
// was taken. That grip does not grab (vr_input.c asks vr_two_hand_zone). Its hits are x1.2; held in one hand, x0.85.
// SAVAGEWIND_VR_TWO_HANDS=0: off.
bool vr_grip_held(int hand);   // vr_input.c
static struct {
    bool zone, latched, grip_was;
    int off;                    // the other hand (0 left, 1 right: the hand itself)
    float sign;
    uint64_t frames;            // weapon updates turned by it
} th;
bool vr_two_hand_zone(int hand) { return th.zone && th.off == hand; }
bool vr_two_hand_latched(void) { return th.latched; }
uint64_t vr_two_hand_frames(void) { return th.frames; }

// m: the weapon's matrix (4x4 column-major, already carried by the main hand), turned in place when held in both
static void two_hand(int side, float *m) {
    int en = vr_opt_on(VR_OPT_TWO_HANDS);
    int off = 1 - side;
    th.off = off;
    bool can = en && k.hand[side].two_handed && k.hand[side].held == VR_SKEL_MELEE &&
               (k.hand[off].held_frame + 8 < k.frames || k.hand[off].held == VR_SKEL_NONE);
    vr_pose ho, hw;
    bool grip = vr_grip_held(off);
    if (!can || !vr_hand_pose(off, &ho) || !vr_tracking_to_world(&ho, &hw)) {
        th.zone = th.latched = false, th.grip_was = grip;
        return;
    }
    vr_vec3 o = vr_v3(m[12], m[13], m[14]), blade = vr_v3_normalize(vr_v3(m[8], m[9], m[10]));
    vr_vec3 to = vr_v3_sub(hw.p, o);
    float t = vr_v3_dot(to, blade);
    float across = vr_v3_len(vr_v3_sub(to, vr_v3_scale(blade, t)));
    th.zone = fabsf(t) > 0.08f && fabsf(t) < 1.2f && across < 0.25f;
    if (getenv("SAVAGEWIND_VR_TRACE_TWO_HANDS") && k.frames % 45 == 0) {
        // (tests) the blade's line in the room's (tracking) space too: the other hand's two poses give the turn between
        vr_quat back = vr_quat_mul(ho.q, vr_quat_conj(hw.q));   // world -> tracking
        vr_vec3 bt = vr_quat_rotate(back, blade), ot = vr_v3_add(ho.p, vr_quat_rotate(back, vr_v3_sub(o, hw.p)));
        LOG("[vr-two-hands] other hand %.2f m along the blade, %.2f m off it; blade %.3f %.3f %.3f from %.3f %.3f %.3f"
            " (tracking)\n", t, across, bt.x, bt.y, bt.z, ot.x, ot.y, ot.z);
    }
    if (grip && !th.grip_was && th.zone) {
        th.latched = true, th.sign = t >= 0 ? 1.0f : -1.0f;
        LOG("[vr] both hands on the weapon (the other hand %.2f m along it)\n", t);
    }
    if (!grip) th.latched = false;
    th.grip_was = grip;
    if (!th.latched || vr_v3_len(to) < 0.05f) return;
    // turn the weapon about its grip: its blade onto the line to the other hand
    vr_quat q = vr_quat_arc(blade, vr_v3_scale(vr_v3_normalize(to), th.sign));
    for (int c = 0; c < 3; c++) {
        vr_vec3 v = vr_quat_rotate(q, vr_v3(m[4 * c], m[4 * c + 1], m[4 * c + 2]));
        m[4 * c] = v.x, m[4 * c + 1] = v.y, m[4 * c + 2] = v.z;
    }
    th.frames++;
}

void vr_weapon_bind(uint64_t actor) {
    int on = vr_opt_on(VR_OPT_WEAPON_HITS);
    if (!on || !GUEST_OK(actor) || !k.arms || !vr_enabled() || !vr_first_person_now()) return;
    float *a = (float *)(uintptr_t)(actor + 0x398);
    uint64_t model = rd64(actor + 0x4e0);
    for (int side = 0; side < 2; side++) {
        if (!k.hand[side].ok || !k.arm[side].ok || k.hand[side].held_frame + 8 < k.frames) continue;
        const float *b = k.hand[side].before;
        float dx = a[3] - b[12], dy = a[7] - b[13], dz = a[11] - b[14];
        bool same = model && model == k.hand[side].model;
        if (!same && dx * dx + dy * dy + dz * dz > 0.03f * 0.03f) continue;
        float m[16] = {a[0], a[4], a[8], 0, a[1], a[5], a[9], 0, a[2], a[6], a[10], 0, a[3], a[7], a[11], 1}, r[16];
        vr_bm_mul(k.hand[side].delta, m, r);
        k.hand[side].actor = actor;
        two_hand(side, r);
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 4; col++) a[row * 4 + col] = r[col * 4 + row];
        if (!weapons_moved++)
            LOG("[vr] the weapon in Link's %s hand follows it (actor %llx, its model %s the hand's)\n",
                side ? "right" : "left", (unsigned long long)actor, same ? "is" : "is not");
        return;
    }
}

// ---- the held weapon hits whenever it is swung (BetterVR's hook_EnableWeaponAttackSensor) ----
// A weapon's attack step (rt_weapon_attack, main+0x7b1690) is about to read its attack sensor's set-up (actor + 0xc20:
// mode, flags, power multiplier, scale, shield break power; + 0xc48 contact layer made, + 0xc4c re-arm) and, re-armed
// with mode 0 or 2, copy it to the sensor in use (+ 0xcf4; on with mode 2), or with another mode turn it off. For the
// melee weapon in Link's hand, in first person: while the motion analysis says a swing is going (vr_input.c
// vr_swing), mode 2, armed once a swing (one hit each), the power multiplied as BetterVR's (0.2 for a brush, 1.35 for
// a full swing) and the shield break power 2 (trees are cut); after it, off. SAVAGEWIND_VR_SWING_HITS=0: the game's
// attacks only.
bool vr_swing(float *power, uint32_t *id);   // vr_input.c
static struct { uint64_t armed, actor; uint32_t id; bool on; } sw;
uint64_t vr_swing_hits(void) { return sw.armed; }

void vr_weapon_attack(uint64_t weapon) {
    int en = vr_opt_on(VR_OPT_SWING_HITS);
    if (!en || !GUEST_OK(weapon) || !k.arms || !vr_enabled() || !vr_first_person_now()) return;
    int side = -1;
    for (int h = 0; h < 2; h++)
        if (k.hand[h].actor == weapon && k.hand[h].held == VR_SKEL_MELEE && k.hand[h].held_frame + 8 >= k.frames) side = h;
    if (side < 0) {
        if (sw.on && sw.actor == weapon) sw.on = false;   // (put away while on: the game's own state takes over)
        return;
    }
    uint8_t *w = (uint8_t *)(uintptr_t)weapon;
    float power = 0;
    uint32_t id = 0;
    bool swinging = vr_swing(&power, &id);
    if (swinging) {
        float n = (power - 0.15f) / 0.85f;
        n = n < 0 ? 0 : n > 1 ? 1 : n;
        float mult = 0.20f + 1.15f * n * n * (3 - 2 * n);
        if (k.hand[side].two_handed) mult *= th.latched ? 1.2f : 0.85f;   // (BetterVR's grip multipliers)
        *(int32_t *)(w + 0xc20) = 2;          // mode: on
        *(uint32_t *)(w + 0xc30) = 2;         // shield break power (2 cuts trees)
        *(float *)(w + 0xc28) = mult;         // the power multiplier, set up...
        *(float *)(w + 0xd04) = mult;         // ... and in use
        bool multi = !vr_opt_on(VR_OPT_ONE_HIT);   // (BetterVR's OneHitPerSwing off: armed every update, half power)
        if (multi) {
            *(float *)(w + 0xc28) = mult * 0.5f;
            *(float *)(w + 0xd04) = mult * 0.5f;
        }
        if (!sw.on || sw.actor != weapon || sw.id != id || multi) {   // a new swing: armed again (a new hit list)
            w[0xc4c] = 1, w[0xc48] = 0;
            sw.armed++, sw.id = id;
            static bool said;
            if (!said) LOG("[vr] the weapon's attack sensor turned on by a swing (power %.2f, x%.2f)\n", power, mult), said = true;
        }
        sw.on = true, sw.actor = weapon;
    } else if (sw.on && sw.actor == weapon) {
        *(int32_t *)(w + 0xc20) = 1;          // mode: off
        w[0xc4c] = 1, w[0xc48] = 0;
        sw.on = false;
    }
}

// (tests) the sensor in use of the melee weapon in Link's hand: 1 on (armed with mode 2: + 0xd09), 0 off, -1 none
int vr_swing_sensor(void) {
    for (int h = 0; h < 2; h++)
        if (k.hand[h].held == VR_SKEL_MELEE && GUEST_OK(k.hand[h].actor) && k.hand[h].held_frame + 8 >= k.frames)
            return *(const uint8_t *)(uintptr_t)(k.hand[h].actor + 0xd09) ? 1 : 0;
    return -1;
}
