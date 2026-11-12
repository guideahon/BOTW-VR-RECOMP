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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "../control_cells.h"
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
enum { PART_NONE, PART_UPPER, PART_LOWER, PART_HAND, PART_HEAD };
static uint8_t arm_part(const char *n) {
    size_t len = strlen(n);
    if (len < 3 || n[len - 2] != '_' || (n[len - 1] != 'L' && n[len - 1] != 'R')) return head_bone(n) ? PART_HEAD : 0;
    uint8_t side = n[len - 1] == 'R' ? 0x10 : 0;
    char base[48];
    if (len - 2 >= sizeof base) return 0;
    memcpy(base, n, len - 2), base[len - 2] = 0;
    if (!strcmp(base, "Arm_1") || !strcmp(base, "Arm_1_Assist")) return side | PART_UPPER;
    if (!strcmp(base, "Arm_2") || !strcmp(base, "Elbow") || !strcmp(base, "Wrist_Assist")) return side | PART_LOWER;
    if (!strcmp(base, "Wrist") || !strcmp(base, "Weapon") || !strncmp(base, "Finger", 6)) return side | PART_HAND;
    return head_bone(n) ? PART_HEAD : 0;
}

// what each bone of a skeleton is (cached per SkeletonObj)
typedef struct {
    uint64_t skel;
    int n;
    int arm1[2], arm2[2], wrist[2], chest;   // Link's: the joints the IK reads
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
    for (int h = 0; h < 2; h++) s->arm1[h] = s->arm2[h] = s->wrist[h] = s->weapon[h] = -1;
    s->chest = -1;
    for (int i = 0; i < s->n; i++) {
        const char *b = bone_name(skel, i);
        if (!b) continue;
        s->part[i] = arm_part(b);
        int side = b[strlen(b) - 1] == 'R';
        if (!strcmp(b, "Arm_1_L") || !strcmp(b, "Arm_1_R")) s->arm1[side] = i;
        if (!strcmp(b, "Arm_2_L") || !strcmp(b, "Arm_2_R")) s->arm2[side] = i;
        if (!strcmp(b, "Wrist_L") || !strcmp(b, "Wrist_R")) s->wrist[side] = i;
        if (!strcmp(b, "Spine_2")) s->chest = i;
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
        vr_vec3 shoulder, elbow, wrist;   // the pivots: shoulder; the solved elbow and wrist
        vr_quat upper, lower, hand;
        float miss;                       // the wrist's distance to its target after the solve (m)
    } arm[2];
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
} k = {.hide = -1, .arms = -1};

enum { VR_SKEL_NONE, VR_SKEL_MELEE, VR_SKEL_BOW, VR_SKEL_SHIELD, VR_SKEL_OTHER };

// vr_input.c: what Link holds in a hand (0 left, 1 right), as seen on the last frames: VR_SKEL_*
// (-1: unknown, Link's skeleton not seen in the last half second)
static uint64_t link_seen_ns;
int vr_skel_held(int hand) {
    if (!link_seen_ns || plat_now_ns() - link_seen_ns > 500000000ull) return -1;
    return k.hand[hand].held_frame + 8 >= k.frames ? k.hand[hand].held : VR_SKEL_NONE;
}

static int weapon_kind(const char *n) {
    if (strstr(n, "_Bow_")) return VR_SKEL_BOW;
    if (strstr(n, "_Shield_")) return VR_SKEL_SHIELD;
    if (strstr(n, "_Sword_") || strstr(n, "_Lsword_") || strstr(n, "_Spear_") || strstr(n, "_Weapon_")) return VR_SKEL_MELEE;
    return VR_SKEL_OTHER;
}

bool vr_first_person_now(void);                            // vr_camera.c
bool vr_tracking_to_world(const vr_pose *in, vr_pose *out);   // vr_camera.c
bool vr_event_anim_hands(void);                              // vr_camera.c
bool vr_hand_pose(int hand, vr_pose *out);                  // vr_input.c
bool vr_drawing(void);                                      // vr_input.c
bool vr_two_hand_latched(void);                             // below

static inline float *bone_m(uint64_t world, int i) { return (float *)(uintptr_t)(world + 0x40ull * i); }

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

// one arm's solution from Link's skeleton and the controller
static void solve_arm(uint64_t world, const skel_info *s, int side) {
    k.arm[side].ok = false;
    if (s->arm1[side] < 0 || s->arm2[side] < 0 || s->wrist[side] < 0) return;
    vr_pose hand, hw;
    if (!vr_hand_pose(side, &hand) || !vr_tracking_to_world(&hand, &hw)) return;
    vr_vec3 sh = vr_bm_pos(bone_m(world, s->arm1[side])), el = vr_bm_pos(bone_m(world, s->arm2[side])),
            wr = vr_bm_pos(bone_m(world, s->wrist[side]));
    // the wrist sits a little behind the grip's centre, along the controller's back
    vr_vec3 target = vr_v3_add(hw.p, vr_quat_rotate(hw.q, vr_v3(0, 0, 0.05f)));
    // the elbow bends down and out (away from the body: from the other shoulder towards this one)
    vr_vec3 out = vr_v3_sub(sh, vr_bm_pos(bone_m(world, s->arm1[1 - side])));
    out.y = 0;
    float ol = vr_v3_len(out);
    out = ol > 1e-4f ? vr_v3_scale(out, 1 / ol) : vr_v3(0, 0, 0);
    vr_vec3 pole = vr_v3_add(vr_v3(0, -1, 0), vr_v3_scale(out, 0.6f));
    vr_ik_result r = vr_ik_two_bone(sh, el, wr, target, pole, 0.75f);
    // the hand: turned as the controller is (BetterVR's skeleton.cpp: the grip's orientation times a fixed turn per hand
    // that takes the controller's axes to the wrist bone's), from where the arm's turns leave it; SAVAGEWIND_VR_HAND_ROLL
    // =0: only pointed where the controller points, its roll the animation's (as before)
    static int roll = -1;
    if (roll < 0) roll = !(getenv("SAVAGEWIND_VR_HAND_ROLL") && *getenv("SAVAGEWIND_VR_HAND_ROLL") == '0');
    vr_quat hand_turn;
    if (roll) {
        vr_quat q0 = vr_bm_rot(bone_m(world, s->wrist[side]));
        vr_quat after = vr_quat_mul(r.lower, vr_quat_mul(r.upper, q0));
        vr_quat target_q = vr_quat_mul(hw.q, wrist_correction(side));
        hand_turn = vr_quat_normalize(vr_quat_mul(target_q, vr_quat_conj(after)));
    } else {
        vr_vec3 fore = vr_v3_normalize(vr_v3_sub(r.wrist, r.elbow));
        vr_vec3 point = vr_quat_rotate(hw.q, vr_v3(0, 0, -1));
        hand_turn = vr_quat_arc(fore, vr_v3_normalize(point));
    }
    k.arm[side].ok = true;
    k.arm[side].shoulder = sh, k.arm[side].elbow = r.elbow, k.arm[side].wrist = r.wrist;
    k.arm[side].upper = r.upper, k.arm[side].lower = r.lower, k.arm[side].hand = hand_turn;
    k.arm[side].miss = vr_v3_len(vr_v3_sub(r.wrist, target));
}

// this frame's arm turns on a skeleton's arm bones (Link's or his armor's), by their parts
static void apply_arms(uint64_t world, const skel_info *s) {
    for (int i = 0; i < s->n; i++) {
        uint8_t p = s->part[i] & 0x0f;
        int side = s->part[i] >> 4;
        if (p < PART_UPPER || p > PART_HAND || !k.arm[side].ok) continue;
        float *m = bone_m(world, i);
        vr_bm_turn(m, k.arm[side].upper, k.arm[side].shoulder, k.arm[side].shoulder);
        if (p >= PART_LOWER) vr_bm_turn(m, k.arm[side].lower, k.arm[side].elbow, k.arm[side].elbow);
        if (p == PART_HAND) vr_bm_turn(m, k.arm[side].hand, k.arm[side].wrist, k.arm[side].wrist);
    }
}

static void hide_head(uint64_t world, const skel_info *s, bool every) {
    for (int i = 0; i < s->n; i++)
        if (every || (s->part[i] & 0x0f) == PART_HEAD) squeeze(bone_m(world, i));
}

void vr_skel_calc(uint64_t model) {
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
    bool hide = k.hide == 1 || (k.hide == 2 && fp), arms = k.arms && fp && !vr_event_anim_hands();   // (an event may leave his hands to its animation)
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
        float before[2][16];
        for (int side = 0; side < 2; side++)
            if (s->weapon[side] >= 0) memcpy(before[side], bone_m(world, s->weapon[side]), sizeof before[side]);
        if (arms) {
            for (int side = 0; side < 2; side++) solve_arm(world, s, side);
            apply_arms(world, s);
            if ((k.arm[0].ok || k.arm[1].ok) && !k.said_arms) LOG("[vr] Link's arms follow the controllers\n"), k.said_arms = true;
            if (k.log_arms > 0 && k.frames % (uint64_t)k.log_arms == 0)
                LOG("[vr-arms] left %s %.3f m, right %s %.3f m from the target\n", k.arm[0].ok ? "ok" : "--",
                    k.arm[0].miss, k.arm[1].ok ? "ok" : "--", k.arm[1].miss);
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
    // a weapon (sword, bow, shield...): in a hand when it sits at that hand's Weapon_ bone as the animation left it;
    // it is then carried by the hand's change (the arms' turns), and tells what that hand holds
    if (!strncmp(n, "Weapon_", 7) && !strstr(n, "Quiver")) {   // (the quiver stays on his back)
        float *root = bone_m(world, 0);
        vr_vec3 p = vr_bm_pos(root);
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
            break;
        }
        return;
    }
    if (strncmp(n, "Armor_", 6)) return;
    size_t len = strlen(n);
    bool head_armor = (len > 5 && !strcmp(n + len - 5, "_Head")) || strstr(n, "_Extra_");
    const skel_info *s = info_of(skel, false);
    if (hide && head_armor) hide_head(world, s, true);
    else if (arms) apply_arms(world, s);   // (sleeves and gloves follow the arms)
}


// ---- the bow aimed by hand (BetterVR's arrow launch override) ----
// An arrow leaving the bow (rt_arrow_aim, main+0x931590): its AI has just made the way it flies, from where the arrow
// is to the shooter's aim point (the camera's line for the player). In first person, for an arrow at Link (his shots),
// that way becomes the line from the string hand through the bow hand, and the aim point moves onto it. SAVAGEWIND_VR_BOW_AIM=head keeps the game's.
static uint64_t arrows_aimed;
uint64_t vr_arrows_aimed(void) { return arrows_aimed; }

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
    dir[0] = d.x, dir[1] = d.y, dir[2] = d.z;
    // the aim point the arrow then flies at: as far away as the game's, on the new line
    float far = vr_v3_len(vr_v3_sub(vr_v3(target[0], target[1], target[2]), o));
    if (!(far > 5.0f && far < 2000.0f)) far = 100.0f;
    vr_vec3 t = vr_v3_add(o, vr_v3_scale(d, far));
    target[0] = t.x, target[1] = t.y, target[2] = t.z;
    arrows_aimed++;
    if (log) fprintf(stderr, "[vr-arrow] aimed by hand: %.3f %.3f %.3f\n", d.x, d.y, d.z);
}

// The arc the arrow about to go would fly (BetterVR draws it while the bow is drawn): LOCAL space points (metres), from
// the string hand along the line through the bow hand; none unless the bow is in one hand, an arrow nocked in the
// other and the main hand's trigger held, in first person, aiming by hand. The shape is the one measured from the
// game's arrows (a straight run of about 25 m, then a fall growing with the square of the distance: 4.6 m down at
// 50 m), not the bow's own parameters.
// SAVAGEWIND_VR_BOW_ARC=0: no arc.
int vr_bow_arc(float (*pts)[3], int max) {
    int on = vr_opt_on(VR_OPT_BOW_ARC) && vr_opt(VR_OPT_BOW_AIM) == 0;
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
        vr_vec3 p = vr_v3_add(hb.p, vr_v3_scale(d, s));
        pts[n][0] = p.x, pts[n][1] = p.y - fall, pts[n][2] = p.z;
        n++;
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
