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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "vr.h"
#include "vr_ik.h"

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
    for (int h = 0; h < 2; h++) s->arm1[h] = s->arm2[h] = s->wrist[h] = -1;
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
} k = {.hide = -1, .arms = -1};

bool vr_first_person_now(void);                            // vr_camera.c
bool vr_tracking_to_world(const vr_pose *in, vr_pose *out);   // vr_camera.c
bool vr_hand_pose(int hand, vr_pose *out);                  // vr_input.c

static inline float *bone_m(uint64_t world, int i) { return (float *)(uintptr_t)(world + 0x40ull * i); }

// squeeze a bone's axes so what is skinned to it shrinks to a point, and put that point in Link's chest: a vertex
// weighted partly to a body bone (hair over the shoulders) then ends inside him
static void squeeze(float *m) {
    for (int i = 0; i < 12; i++) m[i] *= 1e-4f;
    if (k.chest_ok) m[12] = k.chest[0], m[13] = k.chest[1], m[14] = k.chest[2];
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
    // the hand: from the forearm's direction (after the solve) to where the controller points
    vr_vec3 fore = vr_v3_normalize(vr_v3_sub(r.wrist, r.elbow));
    vr_vec3 point = vr_quat_rotate(hw.q, vr_v3(0, 0, -1));
    vr_quat hand_turn = vr_quat_arc(fore, vr_v3_normalize(point));
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
    bool fp = vr_enabled() && vr_first_person_now();
    bool hide = k.hide == 1 || (k.hide == 2 && fp), arms = k.arms && fp;
    if (!hide && !arms) return;
    uint64_t skel = rd64(model + 0x118);
    if (!GUEST_OK(skel)) return;
    uint64_t world = rd64(skel + 0x20);
    if (!GUEST_OK(world)) return;
    const char *n = model == k.model ? "Link" : fmdl_name(rd64(model + 0xe0));
    if (!n) return;
    if (!strcmp(n, "Link")) {
        bool first = model != k.model;
        k.model = model;
        const skel_info *s = info_of(skel, first && getenv("SAVAGEWIND_VR_LOG_BONES"));
        if (first)
            LOG("[vr] Link's skeleton: model %llx, %d bones, arms %d %d %d / %d %d %d\n", (unsigned long long)model, s->n,
                s->arm1[0], s->arm2[0], s->wrist[0], s->arm1[1], s->arm2[1], s->wrist[1]);
        if (s->chest >= 0) {
            const float *m = bone_m(world, s->chest);
            k.chest[0] = m[12], k.chest[1] = m[13], k.chest[2] = m[14], k.chest_ok = true;
        }
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
        if (hide) {
            hide_head(world, s, false);
            if (!k.said_head) LOG("[vr] Link's head hidden (first person)\n"), k.said_head = true;
        }
        k.frames++;
        return;
    }
    if (strncmp(n, "Armor_", 6)) return;
    size_t len = strlen(n);
    bool head_armor = (len > 5 && !strcmp(n + len - 5, "_Head")) || strstr(n, "_Extra_");
    const skel_info *s = info_of(skel, false);
    if (hide && head_armor) hide_head(world, s, true);
    else if (arms) apply_arms(world, s);   // (sleeves and gloves follow the arms)
}
