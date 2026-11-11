// runtime/vr/vr_ik.h: two-bone IK on world bone matrices. A right arm hanging down from (0.2, 1.4, 0): upper arm
// 0.28 m, forearm 0.27 m. Targets in reach are met exactly, bone lengths stay, the elbow bends toward the pole; a
// target out of reach stretches the arm along the reach; the bone matrices turned keep their axes orthonormal and
// their twist about the bone.
#include "vr/vr_ik.h"
#include "vr_test.h"

static float dist3(vr_vec3 a, vr_vec3 b) { return vr_v3_len(vr_v3_sub(a, b)); }

int main(void) {
    vr_vec3 sh = vr_v3(0.2f, 1.4f, 0), el = vr_v3(0.2f, 1.12f, 0), wr = vr_v3(0.2f, 0.85f, 0);
    vr_vec3 down = vr_v3(0, -1, 0);

    VR_TEST("in reach: the wrist on the target, the lengths kept, the elbow toward the pole");
    vr_vec3 tg = vr_v3(0.3f, 1.3f, -0.35f);
    vr_ik_result r = vr_ik_two_bone(sh, el, wr, tg, down, 0.75f);
    CHECK_NEAR(dist3(r.wrist, tg), 0, 1e-4);
    CHECK_NEAR(dist3(r.elbow, sh), 0.28f, 1e-4);
    CHECK_NEAR(dist3(r.wrist, r.elbow), 0.27f, 1e-4);
    CHECK_NEAR(r.stretch, 0, 1e-6);
    // the elbow is below the line shoulder-target (the pole is down)
    vr_vec3 mid = vr_v3_scale(vr_v3_add(sh, tg), 0.5f);
    CHECK(r.elbow.y < mid.y);
    // the turns move the animated joints onto the solved ones
    vr_vec3 e2 = vr_v3_add(sh, vr_quat_rotate(r.upper, vr_v3_sub(el, sh)));
    CHECK_NEAR(dist3(e2, r.elbow), 0, 1e-4);
    vr_vec3 w1 = vr_v3_add(sh, vr_quat_rotate(r.upper, vr_v3_sub(wr, sh)));   // (the wrist carried by the upper turn)
    vr_vec3 w2 = vr_v3_add(r.elbow, vr_quat_rotate(r.lower, vr_v3_sub(w1, e2)));
    CHECK_NEAR(dist3(w2, tg), 0, 1e-3);

    VR_TEST("a pole to the side bends the elbow that way");
    r = vr_ik_two_bone(sh, el, wr, vr_v3(0.2f, 1.4f, -0.4f), vr_v3(1, 0, 0), 0.75f);
    CHECK(r.elbow.x > 0.2f + 0.05f);

    VR_TEST("out of reach: stretched along the reach, mostly the upper arm");
    tg = vr_v3(0.2f, 1.4f, -0.8f);
    r = vr_ik_two_bone(sh, el, wr, tg, down, 0.75f);
    CHECK_NEAR(dist3(r.wrist, tg), 0, 2e-3);
    CHECK(r.stretch > 0.2f);
    CHECK_NEAR(dist3(r.elbow, sh) - 0.28f, r.stretch * 0.75f, 2e-3);
    CHECK_NEAR(r.elbow.y, 1.4f, 2e-2);   // (almost straight)

    VR_TEST("degenerate: the target at the shoulder, the pole along the reach");
    r = vr_ik_two_bone(sh, el, wr, sh, down, 0.75f);
    CHECK(isfinite(r.elbow.x) && isfinite(r.wrist.y));
    r = vr_ik_two_bone(sh, el, wr, vr_v3(0.2f, 0.9f, 0), down, 0.75f);
    CHECK(isfinite(r.elbow.z));

    VR_TEST("a bone matrix turned: axes orthonormal, the twist about the bone kept, the position moved");
    vr_bone_mtx m = {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0.2f, 1.4f, 0, 1};   // x axis down the arm
    vr_quat q = vr_quat_arc(vr_v3(0, -1, 0), vr_v3(0, 0, -1));   // the arm raised forward
    vr_bm_turn(m, q, sh, vr_v3(0.2f, 1.5f, 0));
    vr_vec3 x = vr_v3(m[0], m[1], m[2]), y = vr_v3(m[4], m[5], m[6]), z = vr_v3(m[8], m[9], m[10]);
    CHECK_NEAR(vr_v3_len(x), 1, 1e-5);
    CHECK_NEAR(vr_v3_dot(x, y), 0, 1e-5);
    CHECK_NEAR(vr_v3_dot(y, z), 0, 1e-5);
    CHECK_NEAR(x.z, -1, 1e-5);   // (the bone's axis now points forward)
    CHECK_NEAR(z.z, 0, 1e-5);    // (z, across the bone, untouched by a turn in its plane)
    CHECK_NEAR(m[13], 1.5f, 1e-5);
    CHECK_NEAR(vr_v3_len(vr_v3(vr_quat_arc(vr_v3(1, 0, 0), vr_v3(-1, 0, 0)).x, 0, 0)) <= 1, 1, 0);
    VR_TEST_END("test_vr_ik");
}
