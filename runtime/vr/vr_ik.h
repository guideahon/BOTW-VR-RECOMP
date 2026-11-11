// Two-bone IK for Link's arms (runtime/vr/vr_skel.c), in world space on the game's bone matrices. Header only and free
// of the rest of the runtime: tests/vr/test_vr_ik.c checks it. After BetterVR's SolveTwoBoneIK (skeleton.cpp, MIT):
// the shoulder stays, the elbow goes where the law of cosines puts it on the side of a pole direction, the wrist reaches
// the target (the upper arm stretching more than the forearm when the target is out of reach). Unlike BetterVR, which
// builds each bone's frame from its forward axis, each bone here is turned by the shortest rotation from its animated
// direction to the solved one, so the animation's twist and the game's bone axes are kept whatever they are.
//
// A bone's world matrix as the game keeps it: 16 floats, four columns (the x, y and z axes, then the position), the
// fourth row unused.
#pragma once
#include <math.h>
#include <stdbool.h>

#include "vr_math.h"

typedef float vr_bone_mtx[16];

static inline vr_vec3 vr_bm_pos(const float *m) { return vr_v3(m[12], m[13], m[14]); }

// q, the shortest rotation taking unit a to unit b
static inline vr_quat vr_quat_arc(vr_vec3 a, vr_vec3 b) {
    float d = vr_v3_dot(a, b);
    if (d > 0.999999f) return vr_quat_identity();
    if (d < -0.999999f) {   // (opposite: any axis perpendicular to a)
        vr_vec3 ax = fabsf(a.x) < 0.9f ? vr_v3_cross(a, vr_v3(1, 0, 0)) : vr_v3_cross(a, vr_v3(0, 1, 0));
        ax = vr_v3_normalize(ax);
        vr_quat q = {ax.x, ax.y, ax.z, 0};
        return q;
    }
    vr_vec3 c = vr_v3_cross(a, b);
    vr_quat q = {c.x, c.y, c.z, 1 + d};
    return vr_quat_normalize(q);
}

// m = T(to) * R(q) * T(-from) * m: the bone turned by q about `from`, then moved so `from` lands on `to`
static inline void vr_bm_turn(float *m, vr_quat q, vr_vec3 from, vr_vec3 to) {
    for (int c = 0; c < 3; c++) {
        vr_vec3 ax = vr_quat_rotate(q, vr_v3(m[4 * c], m[4 * c + 1], m[4 * c + 2]));
        m[4 * c] = ax.x, m[4 * c + 1] = ax.y, m[4 * c + 2] = ax.z;
    }
    vr_vec3 p = vr_v3_add(vr_quat_rotate(q, vr_v3_sub(vr_bm_pos(m), from)), to);
    m[12] = p.x, m[13] = p.y, m[14] = p.z;
}

typedef struct {
    vr_vec3 elbow, wrist;   // where they end up
    vr_quat upper, lower;   // the turns: the upper arm's about the shoulder, then the forearm's about the new elbow
    float stretch;          // how far the arm was lengthened to reach (m)
} vr_ik_result;

// shoulder, elbow, wrist: the animated joint positions; target: where the wrist should go; pole: the side the elbow
// bends to (world direction, need not be unit); bias: the upper arm's share of a stretch (BetterVR 0.75)
static inline vr_ik_result vr_ik_two_bone(vr_vec3 shoulder, vr_vec3 elbow, vr_vec3 wrist, vr_vec3 target, vr_vec3 pole,
                                          float bias) {
    vr_ik_result r;
    float l1o = vr_v3_len(vr_v3_sub(elbow, shoulder)), l2o = vr_v3_len(vr_v3_sub(wrist, elbow));
    float l1 = l1o, l2 = l2o;
    vr_vec3 dir = vr_v3_sub(target, shoulder);
    float dist = vr_v3_len(dir);
    const float eps = 0.001f;
    r.stretch = 0;
    if (dist > l1 + l2 - eps) {
        float extra = dist - (l1 + l2) + eps;
        l1 += extra * bias, l2 += extra * (1 - bias);
        r.stretch = extra;
    }
    if (dist < eps) dist = eps;
    if (dist > l1 + l2 - eps) dist = l1 + l2 - eps;
    float ca = (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist);
    float alpha = acosf(ca < -1 ? -1 : ca > 1 ? 1 : ca);
    vr_vec3 dn = vr_v3_normalize(dir);
    vr_vec3 pn = vr_v3_cross(dn, pole);
    if (vr_v3_len(pn) < 1e-5f) pn = vr_v3_cross(dn, vr_v3(0, 0, 1));   // (the pole along the reach: any plane)
    pn = vr_v3_normalize(pn);
    vr_vec3 ortho = vr_v3_normalize(vr_v3_cross(pn, dn));
    vr_vec3 a1 = vr_v3_normalize(vr_v3_add(vr_v3_scale(dn, cosf(alpha)), vr_v3_scale(ortho, sinf(alpha))));
    r.elbow = vr_v3_add(shoulder, vr_v3_scale(a1, l1));
    vr_vec3 a2 = vr_v3_normalize(vr_v3_sub(vr_v3_add(shoulder, vr_v3_scale(dn, dist)), r.elbow));
    r.wrist = vr_v3_add(r.elbow, vr_v3_scale(a2, l2));
    r.upper = vr_quat_arc(vr_v3_normalize(vr_v3_sub(elbow, shoulder)), a1);
    vr_vec3 fore = vr_quat_rotate(r.upper, vr_v3_normalize(vr_v3_sub(wrist, elbow)));   // (the forearm carried by the upper turn)
    r.lower = vr_quat_arc(vr_v3_normalize(fore), a2);
    return r;
}
