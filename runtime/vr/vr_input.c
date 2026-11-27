// VR mode, phase 5: the VR controllers as the game's controller. An OpenXR action set with an action per control the
// game reads, bindings suggested for the common controllers, and each poll of the game's input (runtime/hle/input.c,
// where the test script's input is merged too) reads them, turns them into the game's buttons and sticks
// (vr_input_map.h) and adds them to the gamepad's and keyboard's. A gamepad keeps working beside them.
//
// Phase 8, motion combat (SAVAGEWIND_VR_MOTION, on): the hands' poses too. The right hand's slashes and stabs
// (vr_melee.h, BetterVR's analysis) press attack (Y) as they begin; the left hand raised in front of the face holds
// the guard (ZL). The buttons keep working.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "../platform.h"

#include <openxr/openxr.h>

#include "vr.h"
#include "vr_input_map.h"
#include "vr_internal.h"
#include "vr_melee.h"
#include "vr_options.h"
#include "vr_gestures.h"

#define VR_THROW_SAMPLES 16   // the right hand's last velocities kept for a throw by hand

enum {
    A_MOVE, A_CAMERA, A_A, A_B, A_X, A_Y, A_LCLICK, A_RCLICK, A_MENU, A_LTRIGGER, A_RTRIGGER, A_LGRIP, A_RGRIP, A_HAND,
    A_HAPTIC, A_COUNT
};
static const struct {
    const char *name, *label;
    XrActionType type;
} defs[A_COUNT] = {
    {"move", "Move", XR_ACTION_TYPE_VECTOR2F_INPUT},
    {"camera", "Camera", XR_ACTION_TYPE_VECTOR2F_INPUT},
    {"a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"crouch", "Crouch (left stick)", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"look", "Look (right stick)", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"menu", "Menu (+; held: -)", XR_ACTION_TYPE_BOOLEAN_INPUT},
    {"zl", "ZL", XR_ACTION_TYPE_FLOAT_INPUT},
    {"zr", "ZR", XR_ACTION_TYPE_FLOAT_INPUT},
    {"l", "L (with the right stick: d-pad)", XR_ACTION_TYPE_FLOAT_INPUT},
    {"r", "R", XR_ACTION_TYPE_FLOAT_INPUT},
    {"hand", "Hand pose (motion combat)", XR_ACTION_TYPE_POSE_INPUT},
    {"haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT},
};

// bindings per controller: action, path under /user/hand/
typedef struct {
    int action;
    const char *path;
} bind;
static const bind touch[] = {
    {A_MOVE, "left/input/thumbstick"}, {A_CAMERA, "right/input/thumbstick"}, {A_A, "right/input/a/click"},
    {A_B, "right/input/b/click"}, {A_X, "left/input/x/click"}, {A_Y, "left/input/y/click"},
    {A_LCLICK, "left/input/thumbstick/click"}, {A_RCLICK, "right/input/thumbstick/click"},
    {A_MENU, "left/input/menu/click"}, {A_LTRIGGER, "left/input/trigger/value"},
    {A_RTRIGGER, "right/input/trigger/value"}, {A_LGRIP, "left/input/squeeze/value"},
    {A_RGRIP, "right/input/squeeze/value"}, {A_HAND, "left/input/grip/pose"}, {A_HAND, "right/input/grip/pose"},
    {-1, NULL},
};
static const bind index_[] = {  // (Valve Index: the left A / B are X / Y; the left trackpad pressed is the menu)
    {A_MOVE, "left/input/thumbstick"}, {A_CAMERA, "right/input/thumbstick"}, {A_A, "right/input/a/click"},
    {A_B, "right/input/b/click"}, {A_X, "left/input/a/click"}, {A_Y, "left/input/b/click"},
    {A_LCLICK, "left/input/thumbstick/click"}, {A_RCLICK, "right/input/thumbstick/click"},
    {A_MENU, "left/input/trackpad/force"}, {A_LTRIGGER, "left/input/trigger/value"},
    {A_RTRIGGER, "right/input/trigger/value"}, {A_LGRIP, "left/input/squeeze/value"},
    {A_RGRIP, "right/input/squeeze/value"}, {A_HAND, "left/input/grip/pose"}, {A_HAND, "right/input/grip/pose"},
    {-1, NULL},
};
static const bind wmr[] = {  // (Windows Mixed Reality: the touchpads pressed are A / X, the right menu B)
    {A_MOVE, "left/input/thumbstick"}, {A_CAMERA, "right/input/thumbstick"}, {A_A, "right/input/trackpad/click"},
    {A_B, "right/input/menu/click"}, {A_X, "left/input/trackpad/click"},
    {A_LCLICK, "left/input/thumbstick/click"}, {A_RCLICK, "right/input/thumbstick/click"},
    {A_MENU, "left/input/menu/click"}, {A_LTRIGGER, "left/input/trigger/value"},
    {A_RTRIGGER, "right/input/trigger/value"}, {A_LGRIP, "left/input/squeeze/click"},
    {A_RGRIP, "right/input/squeeze/click"}, {A_HAND, "left/input/grip/pose"}, {A_HAND, "right/input/grip/pose"},
    {-1, NULL},
};
static const bind vive[] = {  // (HTC Vive wands: the touchpads are the sticks, pressed A / X; the right menu B)
    {A_MOVE, "left/input/trackpad"}, {A_CAMERA, "right/input/trackpad"}, {A_A, "right/input/trackpad/click"},
    {A_B, "right/input/menu/click"}, {A_X, "left/input/trackpad/click"}, {A_MENU, "left/input/menu/click"},
    {A_LTRIGGER, "left/input/trigger/value"}, {A_RTRIGGER, "right/input/trigger/value"},
    {A_LGRIP, "left/input/squeeze/click"}, {A_RGRIP, "right/input/squeeze/click"}, {A_HAND, "left/input/grip/pose"},
    {A_HAND, "right/input/grip/pose"}, {-1, NULL},
};
static const bind simple[] = {  // (the minimal one every runtime has: select is A, the left menu the menu)
    {A_A, "right/input/select/click"}, {A_MENU, "left/input/menu/click"}, {A_HAND, "left/input/grip/pose"},
    {A_HAND, "right/input/grip/pose"}, {-1, NULL},
};
static const bind pico[] = {  // (Pico 4 / Neo 3: as the Touch)
    {A_MOVE, "left/input/thumbstick"}, {A_CAMERA, "right/input/thumbstick"}, {A_A, "right/input/a/click"},
    {A_B, "right/input/b/click"}, {A_X, "left/input/x/click"}, {A_Y, "left/input/y/click"},
    {A_LCLICK, "left/input/thumbstick/click"}, {A_RCLICK, "right/input/thumbstick/click"},
    {A_MENU, "left/input/menu/click"}, {A_LTRIGGER, "left/input/trigger/value"},
    {A_RTRIGGER, "right/input/trigger/value"}, {A_LGRIP, "left/input/squeeze/value"},
    {A_RGRIP, "right/input/squeeze/value"}, {A_HAND, "left/input/grip/pose"}, {A_HAND, "right/input/grip/pose"},
    {-1, NULL},
};
static const bind cosmos[] = {  // (HTC Vive Cosmos: as the Touch, the grips a click)
    {A_MOVE, "left/input/thumbstick"}, {A_CAMERA, "right/input/thumbstick"}, {A_A, "right/input/a/click"},
    {A_B, "right/input/b/click"}, {A_X, "left/input/x/click"}, {A_Y, "left/input/y/click"},
    {A_LCLICK, "left/input/thumbstick/click"}, {A_RCLICK, "right/input/thumbstick/click"},
    {A_MENU, "left/input/menu/click"}, {A_LTRIGGER, "left/input/trigger/value"},
    {A_RTRIGGER, "right/input/trigger/value"}, {A_LGRIP, "left/input/squeeze/click"},
    {A_RGRIP, "right/input/squeeze/click"}, {A_HAND, "left/input/grip/pose"}, {A_HAND, "right/input/grip/pose"},
    {-1, NULL},
};
static const bind frame[] = {  // (Steam Frame: A / B on the right; the left bumper the rune (Y), its view button the
                               // map (X), the right menu the menu, as BetterVR's)
    {A_MOVE, "left/input/thumbstick"}, {A_CAMERA, "right/input/thumbstick"}, {A_A, "right/input/a/click"},
    {A_B, "right/input/b/click"}, {A_X, "left/input/view/click"}, {A_Y, "left/input/bumper/click"},
    {A_LCLICK, "left/input/thumbstick/click"}, {A_RCLICK, "right/input/thumbstick/click"},
    {A_MENU, "right/input/menu/click"}, {A_LTRIGGER, "left/input/trigger/value"},
    {A_RTRIGGER, "right/input/trigger/value"}, {A_LGRIP, "left/input/squeeze/value"},
    {A_RGRIP, "right/input/squeeze/value"}, {A_HAND, "left/input/grip/pose"}, {A_HAND, "right/input/grip/pose"},
    {-1, NULL},
};
static const struct {
    const char *profile;
    const bind *b;
} profiles[] = {
    {"/interaction_profiles/bytedance/pico4_controller", pico},
    {"/interaction_profiles/bytedance/pico_neo3_controller", pico},
    {"/interaction_profiles/htc/vive_cosmos_controller", cosmos},
    {"/interaction_profiles/valve/frame_controller_valve", frame},
    {"/interaction_profiles/oculus/touch_controller", touch},
    {"/interaction_profiles/valve/index_controller", index_},
    {"/interaction_profiles/microsoft/motion_controller", wmr},
    {"/interaction_profiles/htc/vive_controller", vive},
    {"/interaction_profiles/khr/simple_controller", simple},
};

static struct {
    bool ready;
    XrSession session;
    XrActionSet set;
    XrAction a[A_COUNT];
    pthread_mutex_t lock;
    uint64_t synced_ns;
    vr_pad_state pad;
    bool angvel_fix;       // the hands' angular velocity turned back (BetterVR's fix for the Oculus runtime)
    uint32_t no_shake;     // presses of Y / ZR kept from the game: nothing of the kind on Link
    // throwing by hand (BetterVR's ProcessHandThrow): the right hand's last velocities (tracking space), the trigger
    // pressed while carrying something, R held a moment after it is let go
    double vel_t[VR_THROW_SAMPLES];
    vr_vec3 vel[VR_THROW_SAMPLES], vel_p[VR_THROW_SAMPLES];
    uint32_t vel_next;
    bool throw_armed, throw_trig_was;
    double throw_r_until;
    uint32_t hand_throws;  // throws given the hand's velocity
    vr_pad_out out;
    bool active;  // the last sync gave the actions (the session has the focus)
    uint64_t syncs;
    float snap_deg;
    // motion combat
    bool motion;           // SAVAGEWIND_VR_MOTION (on)
    bool swing_attack;     // SAVAGEWIND_VR_SWING_ATTACK (on): a swing also presses the game's attack (Y)
    int easy;              // the swing profile in use (vr_melee: 1 BetterVR's easy one; -1 none yet)
    bool shield_gesture;   // SAVAGEWIND_VR_SHIELD (on)
    XrSpace hand[2];       // the grips' poses, left and right
    XrPath hand_path[2];
    vr_melee melee;        // the right hand's slashes and stabs
    double attack_until;   // attack (Y) is held until this time (s)
    double quiet_until;    // no swing attacks until then: the hand coming back from a body slot with what it took out
                           // (a weapon drawn from the shoulder is brought forward fast: it read as a stab)
    bool shield;           // the left hand holds the guard (ZL)
    XrTime last_sample;    // (the hands are sampled once per display time)
    uint32_t attacks, slashes, stabs;
    float last_power;
    float lin_peak;        // the hand's fastest speed (m/s) since its motion locked as a slash or a stab
    bool swing_ok;         // the attack going passed the gates below (vr_swing: the weapon's sensor follows it)
    uint32_t swings_ignored;
    // BetterVR's scheme and gestures (vr_gestures.h)
    bool bettervr;         // SAVAGEWIND_VR_CONTROLS: quest (the default) or bettervr (gestures), or classic (vr_input_map.h)
    bool quest;            // ... the Quest layout (vr_gestures.h)
    bool left_handed;      // SAVAGEWIND_VR_LEFT_HANDED: the hands' roles swapped (sticks stay)
    bool drawing;          // the main hand's trigger held (the bow drawn: vr_skel.c vr_bow_arc)
    bool grip_now[2];      // each hand's grip (the hands' roles), this frame
    float turn_speed;      // SAVAGEWIND_VR_TURN_SPEED: the right stick's turning, scaled
    vr_gesture_state gest;
    uint32_t gestures;     // gestures that fired (for `vr`)
    bool menu_mode;        // a menu is up: the buttons by their letters (vr_pad_menu_buttons), no gestures
    uint32_t masked;       // buttons down when the mode changed (bits: A B X Y, the clicks, menu, triggers, grips), left out until let go
    uint32_t menus;        // times a menu mode began (for `vr`)
    int32_t raw_ry;        // the right stick's up-down as the player pushes it (before the gestures' magnesis)
    _Atomic bool magnesis; // vr_magnesis.c drives what Magnesis holds by the hand: the right stick is its pull only
} in = {.lock = PTHREAD_MUTEX_INITIALIZER, .turn_speed = 1};

// how much the sticks move or turn the view (0..1), for the comfort vignette (xr_vulkan.c vr_stick_motion): the walk
// stick's push and the turn stick's, as the game gets them (any controller), a snap turn for 0.3 s; roomscale walking
// (the player's own steps) is not counted
static struct {
    _Atomic float stick;   // the last input's
    double snap_s;         // the last snap turn (update's clock, s)
    _Atomic uint64_t turned_ns;   // the turn stick last pushed (plat_now_ns; vr_turn_stick_recent)
} moving;

static XrPath path(XrInstance inst, const char *s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(inst, s, &p);
    return p;
}

static void apply_options(void);
extern void menu_request_overlay_toggle(void);  // runtime/ui/menu.mm: applied on the window thread

void vr_input_setup(XrSession session) {
    XrInstance inst = vr_xr_instance();
    const char *e = getenv("SAVAGEWIND_VR_CONTROLLERS");
    if (inst == XR_NULL_HANDLE || (e && *e == '0')) return;
    XrActionSetCreateInfo si = {XR_TYPE_ACTION_SET_CREATE_INFO};
    snprintf(si.actionSetName, sizeof si.actionSetName, "gameplay");
    snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "Gameplay");
    if (!vr_xr_ok(xrCreateActionSet(inst, &si, &in.set), "xrCreateActionSet")) return;
    in.hand_path[0] = path(inst, "/user/hand/left");
    in.hand_path[1] = path(inst, "/user/hand/right");
    for (int i = 0; i < A_COUNT; i++) {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        ai.actionType = defs[i].type;
        if (i == A_HAND || i == A_HAPTIC) ai.countSubactionPaths = 2, ai.subactionPaths = in.hand_path;  // (per hand)
        snprintf(ai.actionName, sizeof ai.actionName, "%s", defs[i].name);
        snprintf(ai.localizedActionName, sizeof ai.localizedActionName, "%s", defs[i].label);
        if (!vr_xr_ok(xrCreateAction(in.set, &ai, &in.a[i]), "xrCreateAction")) return;
    }
    int suggested = 0;
    for (size_t p = 0; p < sizeof profiles / sizeof *profiles; p++) {
        XrActionSuggestedBinding sb[32];
        uint32_t n = 0;
        for (const bind *b = profiles[p].b; b->path && n < 32; b++) {
            char full[128];
            snprintf(full, sizeof full, "/user/hand/%s", b->path);
            sb[n++] = (XrActionSuggestedBinding){in.a[b->action], path(inst, full)};
        }
        sb[n++] = (XrActionSuggestedBinding){in.a[A_HAPTIC], path(inst, "/user/hand/left/output/haptic")};
        sb[n++] = (XrActionSuggestedBinding){in.a[A_HAPTIC], path(inst, "/user/hand/right/output/haptic")};
        XrInteractionProfileSuggestedBinding ps = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, NULL,
                                                   path(inst, profiles[p].profile), n, sb};
        // (a runtime may not know a profile: that one is left out, the rest still count)
        XrResult r = xrSuggestInteractionProfileBindings(inst, &ps);
        if (XR_SUCCEEDED(r)) suggested++;
        else LOG("[vr] controllers: %s left out (%d)\n", profiles[p].profile, (int)r);
    }
    XrSessionActionSetsAttachInfo at = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, NULL, 1, &in.set};
    if (!vr_xr_ok(xrAttachSessionActionSets(session, &at), "xrAttachSessionActionSets")) return;
    in.session = session;
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo sc = {XR_TYPE_ACTION_SPACE_CREATE_INFO, NULL, in.a[A_HAND], in.hand_path[h],
                                      {{0, 0, 0, 1}, {0, 0, 0}}};
        if (!vr_xr_ok(xrCreateActionSpace(session, &sc, &in.hand[h]), "xrCreateActionSpace")) in.hand[h] = XR_NULL_HANDLE;
    }
    in.easy = -1;
    apply_options();
    if (in.motion)
        LOG("[vr] motion combat: slashes and stabs of the right hand attack (%s swing profile)%s\n",
            in.easy ? "easy" : "normal", in.shield_gesture ? "; the left hand raised guards" : "");
    in.ready = true;
    if (in.pad.snap) LOG("[vr] snap turning: %.0f degrees a step\n", in.snap_deg);
    // BetterVR's control scheme and body gestures (vr_gestures.h), or the earlier one (classic; at start only)
    const char *cs = getenv("SAVAGEWIND_VR_CONTROLS");
    in.bettervr = !(cs && !strcmp(cs, "classic"));
    in.quest = in.bettervr && !(cs && !strcmp(cs, "bettervr"));
    LOG("[vr] controls: %s%s%s\n", in.quest ? "the Quest layout (A accept, B cancel, the triggers the hands' weapons; "
                                             "SAVAGEWIND_VR_CONTROLS=bettervr: BetterVR's) with "
                                             : "",
        in.bettervr ? "BetterVR's gestures (weapon over the right shoulder, bow over the left, rune at the left hip, "
                      "whistle at the mouth)"
                    : "classic (SAVAGEWIND_VR_CONTROLS=classic)",
        in.left_handed ? ", left-handed" : "");
    LOG("[vr] controllers: %d actions, bindings for %d kinds of controller (SAVAGEWIND_VR_CONTROLLERS=0: off)\n",
        A_COUNT, suggested);
}

// the options (vr_options.c: the Options screen's VR page or SAVAGEWIND_VR_*), each frame: a change applies at once
static void apply_options(void) {
    in.motion = vr_opt_on(VR_OPT_MOTION) && in.hand[1] != XR_NULL_HANDLE;
    in.swing_attack = vr_opt_on(VR_OPT_SWING_ATTACK);
    in.shield_gesture = in.motion && vr_opt_on(VR_OPT_SHIELD) && in.hand[0] != XR_NULL_HANDLE;
    int easy = vr_opt(VR_OPT_SWING) == 1;
    if (easy != in.easy) vr_melee_init(&in.melee, easy), in.easy = easy;
    in.pad.snap = vr_opt(VR_OPT_TURN) == 1;
    int deg = vr_opt(VR_OPT_SNAP_DEGREES);
    in.snap_deg = deg >= 5 && deg <= 180 ? (float)deg : 45.0f;
    int ts = vr_opt(VR_OPT_TURN_SPEED);
    in.turn_speed = ts >= 20 && ts <= 300 ? ts / 100.0f : 1.0f;
    in.left_handed = vr_opt(VR_OPT_LEFT_HANDED) == 1;
    int dz = vr_opt(VR_OPT_DEADZONE);
    in.pad.dead = dz >= 1 && dz <= 60 ? dz / 100.0f : VR_PAD_DEAD;
    extern bool vr_runtime_is_oculus(void);   // (vr.c)
    int af = vr_opt(VR_OPT_ANGVEL_FIX);
    in.angvel_fix = af == 1 || (af == 0 && vr_runtime_is_oculus());
}

static bool get_bool(int i) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO, NULL, in.a[i], XR_NULL_PATH};
    XrActionStateBoolean st = {XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(in.session, &gi, &st)) && st.isActive && st.currentState;
}

static float get_float(int i) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO, NULL, in.a[i], XR_NULL_PATH};
    XrActionStateFloat st = {XR_TYPE_ACTION_STATE_FLOAT};
    return XR_SUCCEEDED(xrGetActionStateFloat(in.session, &gi, &st)) && st.isActive ? st.currentState : 0;
}

static void get_vec(int i, float *x, float *y) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO, NULL, in.a[i], XR_NULL_PATH};
    XrActionStateVector2f st = {XR_TYPE_ACTION_STATE_VECTOR2F};
    bool ok = XR_SUCCEEDED(xrGetActionStateVector2f(in.session, &gi, &st)) && st.isActive;
    *x = ok ? st.currentState.x : 0, *y = ok ? st.currentState.y : 0;
}

// a vibration of one hand (0 left, 1 right; the left-handed swap applied by the caller)
static void pulse(int hand, float amplitude, double seconds) {
    if (in.a[A_HAPTIC] == XR_NULL_HANDLE) return;
    XrHapticActionInfo hi = {XR_TYPE_HAPTIC_ACTION_INFO, NULL, in.a[A_HAPTIC], in.hand_path[hand]};
    XrHapticVibration v = {XR_TYPE_HAPTIC_VIBRATION, NULL, (XrDuration)(seconds * 1e9), XR_FREQUENCY_UNSPECIFIED,
                           amplitude};
    xrApplyHapticFeedback(in.session, &hi, (const XrHapticBaseHeader *)&v);
}

// The game's own rumble (runtime/hle/vibration.c, as BetterVR's patch_CTRL_Rumble): each Joy-Con's strongest band, on
// that hand's controller, re-applied in short pulses while the game holds it (SAVAGEWIND_VR_RUMBLE=0: off;
// SAVAGEWIND_VR_RUMBLE_GAIN, 1).
static struct {
    _Atomic float amp[2];
    uint64_t last_ns[2];
    int on;          // -1 not read
    float gain;
    uint64_t pulses;
} rumble = {.on = -1};

void vr_rumble(int hand, float amplitude) {
    if (hand >= 0 && hand < 2) rumble.amp[hand] = amplitude;
}

static void rumble_tick(uint64_t now) {
    rumble.on = vr_opt_on(VR_OPT_RUMBLE);   // (live: the Options screen)
    int g = vr_opt(VR_OPT_RUMBLE_GAIN);
    rumble.gain = g > 0 && g <= 400 ? g / 100.0f : 1.0f;
    if (!rumble.on) return;
    for (int h = 0; h < 2; h++) {
        float a = rumble.amp[h] * rumble.gain;
        if (a < 0.02f || now - rumble.last_ns[h] < 40000000ull) continue;
        rumble.last_ns[h] = now;
        rumble.pulses++;
        pulse(in.left_handed ? 1 - h : h, a > 1 ? 1 : a, 0.05);
    }
}

uint64_t vr_rumble_pulses(void) { return rumble.pulses; }

// BetterVR's walking direction: SAVAGEWIND_VR_WALK=controller makes the left stick walk the way the left controller
// points instead of the way the head looks. The game walks Link relative to the view it draws (the head's), so the
// stick is turned by the controller's heading less the head's (radians, + to the left), kept in in_walk.
static struct {
    int mode;        // -1 not read; 1: by the controller
    _Atomic float turn;
} walk = {.mode = -1};

static vr_pose pose_of(XrPosef p);
// the walking hand's heading (vr_quat_heading's sense). Pointing nearly straight up or down its forward has no heading:
// the last one is kept; raised overhead and tipped back past vertical, the way it faces is its forward turned round
// (BetterVR 1.0.1, "keep walking where the hand points when it's raised overhead")
static float controller_walk_heading(vr_quat q) {
    static float last;
    static bool tipped;
    vr_vec3 f = vr_quat_rotate(q, vr_v3(0, 0, -1)), up = vr_quat_rotate(q, vr_v3(0, 1, 0));
    vr_vec3 flat = vr_v3(f.x, 0, f.z);
    if (vr_v3_len(flat) < 0.2f) return last;
    vr_vec3 upright = vr_v3_normalize(vr_v3_sub(vr_v3(0, 1, 0), vr_v3_scale(f, f.y)));
    float align = vr_v3_dot(up, upright);
    if (fabsf(f.y) < 0.5f || align > 0.3f) tipped = false;
    else if (align < -0.3f) tipped = true;
    vr_vec3 h = tipped ? vr_v3_scale(flat, -1) : flat;
    last = atan2f(-h.x, -h.z);
    return last;
}

static uint64_t ladder_frames;   // frames the walk stick was turned for a ladder (the `vr` command)
uint64_t vr_ladder_frames(void) { return ladder_frames; }

static void walk_turn(void) {
    int mode = vr_opt(VR_OPT_WALK) == 1;   // (live: the Options screen)
    if (mode != walk.mode && mode) LOG("[vr] walking: the left stick walks the way the left controller points\n");
    walk.mode = mode;
    if (!walk.mode) return;
    XrTime t = vr_next_display_time();
    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION}, hand = {XR_TYPE_SPACE_LOCATION};
    int src = in.left_handed ? 1 : 0;   // (the movement hand)
    if (!t || !in.hand[src] || XR_FAILED(xrLocateSpace(vr_xr_view(), vr_xr_local(), t, &head)) ||
        XR_FAILED(xrLocateSpace(in.hand[src], vr_xr_local(), t, &hand)) ||
        !(hand.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return;
    walk.turn = remainderf(controller_walk_heading(pose_of(hand.pose).q) - vr_quat_heading(pose_of(head.pose).q),
                           2 * 3.14159265f);
}

static vr_pose pose_of(XrPosef p) {
    vr_pose r = {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                 {p.position.x, p.position.y, p.position.z}};
    return r;
}

int vr_skel_held(int hand);   // vr_skel.c

#define VR_SLASH_MIN_SPEED 2.0f   // m/s: the hand's peak speed for a slash to attack
#define VR_STAB_MIN_SPEED 1.5f    // m/s: ... for a stab

// the hands, once per display time: the right one's slashes and stabs, the left one raised (in.out gets Y / ZL)
static void motion(double now) {
    XrTime t = vr_next_display_time();
    if (t && t != in.last_sample) {
        in.last_sample = t;
        XrSpace local = vr_xr_local(), view = vr_xr_view();
        XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
        bool head_ok = view && XR_SUCCEEDED(xrLocateSpace(view, local, t, &head)) &&
                       (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
        XrSpaceVelocity vel = {XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation right = {XR_TYPE_SPACE_LOCATION, &vel};
        if (head_ok && XR_SUCCEEDED(xrLocateSpace(in.hand[in.left_handed ? 0 : 1], local, t, &right))) {
            const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            bool tracked = (right.locationFlags & need) == need &&
                           (vel.velocityFlags & (XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT));
            uint32_t before = in.melee.attacks;
            vr_vec3 lin = vr_v3(vel.linearVelocity.x, vel.linearVelocity.y, vel.linearVelocity.z);
            vr_vec3 ang = vr_v3(vel.angularVelocity.x, vel.angularVelocity.y, vel.angularVelocity.z);
            // BetterVR's "Fix controller rotation" (openxr.cpp): the Oculus runtime gives the hands' angular velocity
            // in the controller's own frame, tilted 45 degrees about its x axis; turned back into the tracking space
            if (in.angvel_fix) {
                const vr_quat tilt = {-0.38268343f, 0, 0, 0.92387953f};
                ang = vr_quat_rotate(pose_of(right.pose).q, vr_quat_rotate(tilt, ang));
            }
            vr_melee_update(&in.melee, pose_of(right.pose), lin, ang, pose_of(head.pose).p, t / 1e9, tracked);
            if (vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {   // (for a throw by hand: the last moments)
                int k = in.vel_next++ % VR_THROW_SAMPLES;
                in.vel_t[k] = now, in.vel_p[k] = pose_of(right.pose).p, in.vel[k] = lin;
            }
            if (in.melee.locked == VR_MELEE_NONE) in.lin_peak = 0;
            else if (in.melee.hand_speed > in.lin_peak) in.lin_peak = in.melee.hand_speed;
            if (!in.melee.active) in.swing_ok = false;
            // the analysis alone (BetterVR's, for its weapon collisions) takes a flick of the wrist or a small quick move
            // for a slash: the game's attack also asks the hand itself to move fast (a real slash's hand: 2-4 m/s; a
            // wrist flick's: under 1) and a melee weapon in Link's hand (unarmed, the attack button draws one)
            int held = vr_skel_held(1);
            bool real = in.lin_peak >= (in.melee.locked == VR_MELEE_STAB ? VR_STAB_MIN_SPEED : VR_SLASH_MIN_SPEED) &&
                        (held == 1 || held < 0);
            if (in.melee.attacks != before && now < in.quiet_until) {
                // (the draw's own motion: not an attack)
            } else if (in.melee.attacks != before) {  // (a motion read as an attack: counted - the analysis's own)
                in.attacks++;
                if (in.melee.locked == VR_MELEE_STAB) in.stabs++;
                else in.slashes++;
                in.last_power = in.melee.power;
                if (!real) {
                    in.swings_ignored++;
                } else {   // (the game's attack, held long enough to be seen, and the weapon's sensor)
                    in.swing_ok = true;
                    if (in.swing_attack) in.attack_until = now + 0.12;
                    pulse(in.left_handed ? 0 : 1, 0.5f + 0.5f * (in.melee.power > 1 ? 1 : in.melee.power), 0.06);
                }
            }
        }
        XrSpaceLocation left = {XR_TYPE_SPACE_LOCATION};
        if (in.shield_gesture && head_ok && XR_SUCCEEDED(xrLocateSpace(in.hand[in.left_handed ? 1 : 0], local, t, &left)) &&
            (left.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT))
            in.shield = vr_shield_raised(in.shield, pose_of(head.pose), pose_of(left.pose).p) &&
                        vr_shield_allowed(vr_skel_held(0), vr_skel_held(1));   // (Link's hands: his shield is in his left)
        else
            in.shield = false;
    }
    if (now < in.attack_until) in.out.buttons |= 1ull << VRB_Y;
    if (in.shield) in.out.buttons |= 1ull << VRB_ZL;
}

// BetterVR's scheme and gestures: the head and both hands now (tracking space), into vr_gestures
// ---- throwing a lifted object by hand (BetterVR's hand throw: controls.cpp ProcessHandThrow, player.cpp
// hook_OverrideThrowVelocity) ----
// Carrying something (vr_skel.c vr_player_carrying), the right trigger pressed (again: one held when it was picked up
// does not count) and let go throws it (R): the game's throw, at its animation's throw frame, then gets the hand's
// fastest velocity in the last 0.1 s before the release (in the world: the tracking-to-world turn), boosted up to 1.8x
// from 4 to 8 m/s, at most 1.2x the game's own throw speed (tools/patches.py _grab_throw -> vr_grab_throw).
// SAVAGEWIND_VR_HAND_THROW=0: the trigger's release still throws, at the game's velocity.
static pthread_mutex_t throw_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    float v[3];
    double until;
} hand_throw;

static void throw_by_hand(double now) {
    vr_vec3 best = vr_v3(0, 0, 0), at = vr_v3(0, 0, 0);
    for (int k = 0; k < VR_THROW_SAMPLES; k++)
        if (in.vel_t[k] > 0 && now - in.vel_t[k] < 0.1 && vr_v3_len(in.vel[k]) > vr_v3_len(best)) best = in.vel[k], at = in.vel_p[k];
    float sp = vr_v3_len(best);
    static int on = -1;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_HAND_THROW") && *getenv("SAVAGEWIND_VR_HAND_THROW") == '0');
    if (!on || sp < 0.8f) {
        LOG("[vr] throw: the game's (hand %.2f m/s)\n", sp);
        return;
    }
    float t = (sp - 4.0f) / 4.0f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float boost = 1.0f + 0.8f * t * t * (3 - 2 * t);
    extern bool vr_tracking_to_world(const vr_pose *in, vr_pose *out);
    vr_pose a = {{0, 0, 0, 1}, at}, b = {{0, 0, 0, 1}, vr_v3_add(at, vr_v3_scale(best, 0.1f))}, wa, wb;
    if (!vr_tracking_to_world(&a, &wa) || !vr_tracking_to_world(&b, &wb)) return;
    vr_vec3 w = vr_v3_scale(vr_v3_sub(wb.p, wa.p), 10.0f * boost);
    pthread_mutex_lock(&throw_lock);
    hand_throw.v[0] = w.x, hand_throw.v[1] = w.y, hand_throw.v[2] = w.z;
    hand_throw.until = now + 1.5;
    pthread_mutex_unlock(&throw_lock);
    LOG("[vr] throw by hand: %.2f m/s (x%.2f)\n", sp, boost);
}

void vr_grab_throw(float *vel) {
    double now = (double)plat_now_ns() / 1e9;
    float v[3];
    pthread_mutex_lock(&throw_lock);
    bool have = hand_throw.until > now;
    memcpy(v, hand_throw.v, sizeof v);
    hand_throw.until = 0;
    pthread_mutex_unlock(&throw_lock);
    if (!have) {
        // the game's own throw goes along Link's facing, which in first person is not where the player looks (his body
        // is turned to the view only as drawn): a bomb thrown without a swing went off to a side (2026-11-24). Its
        // ground part is turned to the view's heading, its speed and its rise kept
        extern bool vr_first_person_now(void), vr_view_heading(float *yaw);
        float yaw, flat = sqrtf(vel[0] * vel[0] + vel[2] * vel[2]);
        if (vr_first_person_now() && vr_view_heading(&yaw) && flat > 1e-3f) {
            float from = atan2f(vel[0], vel[2]);
            vel[0] = sinf(yaw) * flat, vel[2] = cosf(yaw) * flat;
            LOG("[vr] throw: the game's, turned to the view (%.0f deg)\n", remainderf(yaw - from, 6.2831853f) * 57.29578f);
        }
        return;
    }
    float game = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    float hand = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (game > 0 && hand > game * 1.2f)
        for (int i = 0; i < 3; i++) v[i] *= game * 1.2f / hand;
    LOG("[vr] throw: the hand's (%.2f %.2f %.2f) in place of the game's (%.2f %.2f %.2f)\n", v[0], v[1], v[2], vel[0],
        vel[1], vel[2]);
    memcpy(vel, v, sizeof v);
    in.hand_throws++;
}
uint32_t vr_hand_throws(void) { return in.hand_throws; }

static pthread_mutex_t guide_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int n;
    float p[5][3], head[3];
    int slot[5];
    bool on[5];      // a hand is over it now
} guide;

// xr_vulkan.c: the body slots to show this frame (tracking space; LOCAL): their places, whether a hand is over each,
// and the head they face. 0 outside play or with the guide off
int vr_slot_guide(float (*p)[3], bool *on, float head[3], int max) {
    extern bool vr_world_view(void), vr_first_person_now(void);
    if (!in.ready || !in.bettervr || in.menu_mode || !vr_world_view() || !vr_first_person_now()) return 0;
    pthread_mutex_lock(&guide_lock);
    int n = guide.n < max ? guide.n : max;
    memcpy(p, guide.p, sizeof(float[3]) * (size_t)n);
    memcpy(on, guide.on, sizeof(bool) * (size_t)n);
    memcpy(head, guide.head, sizeof(float[3]));
    pthread_mutex_unlock(&guide_lock);
    return n;
}

static uint64_t gestures(const vr_pad_in *p, double now) {
    vr_gesture_in g = {0};
    g.now = now;
    g.grip[0] = p->lgrip >= VR_PAD_PRESS, g.grip[1] = p->rgrip >= VR_PAD_PRESS;
    in.grip_now[0] = g.grip[0], in.grip_now[1] = g.grip[1];
    {   // the grip that takes hold of the weapon with the other hand does not grab (vr_skel.c two_hand)
        extern bool vr_two_hand_zone(int hand);
        for (int h = 0; h < 2; h++) g.no_grab[h] = vr_two_hand_zone(in.left_handed ? 1 - h : h);
    }
    g.trigger[0] = p->ltrigger >= VR_PAD_PRESS, g.trigger[1] = p->rtrigger >= VR_PAD_PRESS;
    g.a = p->a, g.b = p->b, g.x = p->x, g.y = p->y, g.lclick = p->lclick, g.rclick = p->rclick;
    extern bool framepace_quick_ui(void);
    g.quest = in.quest, g.ry = p->ry, g.magnesis = in.magnesis, g.shield_up = in.shield;
    g.quick_ui = framepace_quick_ui();
    {
        static int tm = -1;
        if (tm < 0) tm = getenv("SAVAGEWIND_VR_TRIGGER_MENUS") && *getenv("SAVAGEWIND_VR_TRIGGER_MENUS") == '1';
        g.trigger_menus = tm;
    }
    g.drop_enabled = vr_opt_on(VR_OPT_DROP);
    {   // what Link is doing (vr_skel.c vr_player_flags): climbing a wall or gliding, on a horse
        extern bool vr_player_flags(uint32_t *f);
        uint32_t mf;
        if (vr_player_flags(&mf)) g.climbing = (mf & ((1u << 7) | (1u << 15))) != 0, g.riding = (mf & (1u << 20)) != 0;
    }
    XrTime t = vr_next_display_time();
    XrSpace local = vr_xr_local(), view = vr_xr_view();
    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
    bool ok = t && view && XR_SUCCEEDED(xrLocateSpace(view, local, t, &head)) &&
              (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
    if (ok) {
        vr_pose hp = pose_of(head.pose);
        vr_vec3 f = vr_quat_rotate(hp.q, vr_v3(0, 0, -1)), r = vr_quat_rotate(hp.q, vr_v3(1, 0, 0));
        g.head = vr_g3v(hp.p.x, hp.p.y, hp.p.z), g.fwd = vr_g3v(f.x, f.y, f.z), g.right = vr_g3v(r.x, r.y, r.z);
        for (int h = 0; h < 2; h++) {
            int src = in.left_handed ? 1 - h : h;   // (left-handed: the left hand plays the right one's part)
            XrSpaceVelocity vel = {XR_TYPE_SPACE_VELOCITY};
            XrSpaceLocation hl = {XR_TYPE_SPACE_LOCATION, &vel};
            if (in.hand[src] && XR_SUCCEEDED(xrLocateSpace(in.hand[src], local, t, &hl)) &&
                (hl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                g.hand[h] = vr_g3v(hl.pose.position.x, hl.pose.position.y, hl.pose.position.z);
                if (vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
                    g.hand_speed[h] = sqrtf(vel.linearVelocity.x * vel.linearVelocity.x +
                                            vel.linearVelocity.y * vel.linearVelocity.y +
                                            vel.linearVelocity.z * vel.linearVelocity.z);
            } else {
                g.hand[h] = vr_g3v(g.head.x + (h ? 0.3f : -0.3f), g.head.y - 0.8f, g.head.z);   // (untracked: no slot)
            }
        }
    } else {
        g.head = vr_g3v(0, 1.7f, 0), g.fwd = vr_g3v(0, 0, -1), g.right = vr_g3v(1, 0, 0);
        g.hand[0] = vr_g3v(-0.3f, 0.9f, 0), g.hand[1] = vr_g3v(0.3f, 0.9f, 0);
    }
    // what Link holds, read from the game where it can be (runtime/vr/vr_skel.c: the weapon models at his hands);
    // the gestures' own tracking otherwise (a rune, or before his skeleton is seen)
    extern int vr_skel_held(int hand);
    int rh = vr_skel_held(1), lh = vr_skel_held(0);
    g.hand_held[0] = in.left_handed ? rh : lh;
    g.hand_held[1] = in.left_handed ? lh : rh;
    if (rh >= 0) {
        if (rh == 1) in.gest.held = VR_HELD_MELEE;            // (VR_SKEL_MELEE)
        else if (lh == 2) in.gest.held = VR_HELD_BOW;         // (VR_SKEL_BOW, in the left hand)
        // (a bow taken out by the gesture stays the bow while nothing else is seen in the hands: a frame or two with
        // its model away from the hand's bone - drawing it, a turn - made it nothing, and the right trigger then
        // pressed the attack, which took the sword out instead of drawing; 2026-11-20, "I can't use the bow")
        else if (in.gest.held != VR_HELD_RUNE && in.gest.held != VR_HELD_BOW) in.gest.held = VR_HELD_NONE;
    }
    int before = in.gest.held;
    uint64_t b = vr_gestures(&in.gest, &g);
    {   // carrying something lifted: the right trigger throws it on its release (by hand; throw_by_hand above)
        extern bool vr_player_carrying(void);
        extern bool vr_first_person_now(void);
        bool carry = vr_player_carrying();
        if (carry) {
            if (g.trigger[1] && !in.throw_trig_was) in.throw_armed = true, pulse(in.left_handed ? 0 : 1, 0.3f, 0.05);
            if (!g.trigger[1] && in.throw_trig_was && in.throw_armed) {
                in.throw_armed = false;
                in.throw_r_until = now + 0.15;
                if (vr_first_person_now()) throw_by_hand(now);
                pulse(in.left_handed ? 0 : 1, 0.8f, 0.1);
            }
            b &= ~((1ull << VRB_Y) | (1ull << VRB_ZR) | (1ull << VRB_A));   // (the trigger is the throw's)
        } else {
            in.throw_armed = false;
        }
        in.throw_trig_was = g.trigger[1];
        if (now < in.throw_r_until) b |= 1ull << VRB_R;
    }
    {   // BetterVR's "nothing to draw" (weapon.cpp CanDrawFromBack): Y or ZR with no weapon or no bow on Link plays
        // the game's head shake and puts a rune away; a press of either is kept from the game until let go when his
        // hands hold nothing of the kind and none is on him (the weapon models vr_skel.c sees). SAVAGEWIND_VR_NO_SHAKE=0:
        // as before
        extern int vr_skel_owns(int kind);
        static uint64_t was;
        static bool y_off, zr_off;
        static int on = -1;
        if (on < 0) on = !getenv("SAVAGEWIND_VR_NO_SHAKE") || strcmp(getenv("SAVAGEWIND_VR_NO_SHAKE"), "0") != 0;
        uint64_t press = b & ~was;
        was = b;
        // (a quick menu's ZR is the bows' and arrows' menus, not a draw: kept for the game)
        if (vr_g_quick_menu(&in.gest) || in.gest.bow_close > 0) press &= ~(1ull << VRB_ZR), zr_off = false;
        if (press & (1ull << VRB_Y)) y_off = on && rh == 0 && lh != 1 && vr_skel_owns(1) == 0;   // (VR_SKEL_MELEE)
        if (press & (1ull << VRB_ZR)) zr_off = on && rh != 2 && lh != 2 && vr_skel_owns(2) == 0; // (VR_SKEL_BOW)
        if (!(b & (1ull << VRB_Y))) y_off = false;
        if (!(b & (1ull << VRB_ZR))) zr_off = false;
        if ((press & (1ull << VRB_Y) && y_off) || (press & (1ull << VRB_ZR) && zr_off)) {
            in.no_shake++;
            LOG("[vr] %s kept from the game: nothing of the kind on Link (no head shake)\n", y_off ? "Y" : "ZR");
        }
        if (y_off) b &= ~(1ull << VRB_Y);
        if (zr_off) b &= ~(1ull << VRB_ZR);
    }
    {   // each press of the right trigger, in the log: what it did and what the game showed in the hands
        static bool was;
        if (g.trigger[1] && !was)
            LOG("[vr] right trigger: %s (in hand %d; the game's left hand %d, right %d)\n",
                b & (1ull << VRB_ZR) ? "the bow (ZR)" : b & (1ull << VRB_R) ? "throw (R)" : b & (1ull << VRB_A) ? "the rune (A)"
                : b & (1ull << VRB_Y) ? "attack (Y)" : "nothing", in.gest.held, lh, rh);
        was = g.trigger[1];
    }
    if (g.trigger[1] && getenv("SAVAGEWIND_VR_TRACE_GESTURES")) {
        static uint64_t last;
        if (plat_now_ns() - last > 500000000ull)
            last = plat_now_ns(), LOG("[vr] right trigger: in hand %d (game: left %d right %d)\n", in.gest.held, lh, rh);
    }
    {   // the body slots' guide (BetterVR's world guide): where each slot is, shown when a hand comes near it
        // (Options > VR > Body slot guide); for the frame's layers (xr_vulkan.c vr_slot_guide)
        int mode = vr_opt(VR_OPT_SLOT_GUIDE);
        vr_g3 c[VR_SLOT_MOUTH + 1];
        vr_gesture_slot_centres(g.head, g.fwd, g.right, g.seated, c);
        pthread_mutex_lock(&guide_lock);
        guide.n = 0;
        if (mode != 1 && g.head.y != 0) {
            for (int sl = VR_SLOT_RSHOULDER; sl <= VR_SLOT_MOUTH; sl++) {
                if (sl == VR_SLOT_RWAIST && !g.drop_enabled) continue;
                float near = 9;
                for (int h = 0; h < 2; h++) {
                    vr_g3 d = vr_g3sub(g.hand[h], c[sl]);
                    float l = sqrtf(vr_g3dot(d, d));
                    if (l < near) near = l;
                }
                if (mode == 2 || near < 0.35f) {
                    guide.p[guide.n][0] = c[sl].x, guide.p[guide.n][1] = c[sl].y, guide.p[guide.n][2] = c[sl].z;
                    guide.slot[guide.n] = sl, guide.on[guide.n] = in.gest.slot_now[0] == sl || in.gest.slot_now[1] == sl;
                    guide.n++;
                }
            }
            guide.head[0] = g.head.x, guide.head[1] = g.head.y, guide.head[2] = g.head.z;
        }
        pthread_mutex_unlock(&guide_lock);
    }
    for (int h = 0; h < 2; h++)
        if (in.gest.haptic[h] == 2) in.quiet_until = now + 0.7;   // (something taken out of or put in a body slot)
    for (int h = 0; h < 2; h++)
        if (in.gest.haptic[h]) {
            pulse(in.left_handed ? 1 - h : h, in.gest.haptic[h] == 2 ? 0.8f : 0.3f, in.gest.haptic[h] == 2 ? 0.08 : 0.03);
            if (in.gest.haptic[h] == 2) in.gestures++;
        }
    if (g.riding && (b & (1ull << VRB_L)) && getenv("SAVAGEWIND_VR_TRACE_GESTURES")) {   // (tests: a pat on the horse)
        static double said;
        if (now - said > 0.3) said = now, LOG("[vr] riding: L (pet the horse)\n");
    }
    if (in.gest.held != before && getenv("SAVAGEWIND_VR_TRACE_GESTURES"))
        LOG("[vr] gesture: in hand %d -> %d\n", before, in.gest.held);
    return b;
}

// the controllers' state now (synced at most every 2 ms: the game polls its input several times a frame)
static void update(void) {
    uint64_t now = plat_now_ns();
    if (in.synced_ns && now - in.synced_ns < 2000000) return;
    in.synced_ns = now;
    XrActiveActionSet as = {in.set, XR_NULL_PATH};
    XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO, NULL, 1, &as};
    XrResult r = xrSyncActions(in.session, &si);
    in.active = r == XR_SUCCESS;  // (XR_SESSION_NOT_FOCUSED: the runtime's own menu has the controllers)
    if (!in.active) {
        memset(&in.out, 0, sizeof in.out);
        in.pad.menu = false, in.pad.overlay_fired = false, in.pad.plus_until = 0;
        return;
    }
    in.syncs++;
    apply_options();
    vr_pad_in p = {0};
    get_vec(A_MOVE, &p.lx, &p.ly);
    get_vec(A_CAMERA, &p.rx, &p.ry);
    p.a = get_bool(A_A), p.b = get_bool(A_B), p.x = get_bool(A_X), p.y = get_bool(A_Y);
    p.lclick = get_bool(A_LCLICK), p.rclick = get_bool(A_RCLICK), p.menu = get_bool(A_MENU);
    p.ltrigger = get_float(A_LTRIGGER), p.rtrigger = get_float(A_RTRIGGER);
    p.lgrip = get_float(A_LGRIP), p.rgrip = get_float(A_RGRIP);
    if (in.left_handed) {   // the hands' roles swapped; the sticks stay where they are
        bool t;
        float f;
        f = p.ltrigger, p.ltrigger = p.rtrigger, p.rtrigger = f;
        f = p.lgrip, p.lgrip = p.rgrip, p.rgrip = f;
        t = p.a, p.a = p.x, p.x = t;
        t = p.b, p.b = p.y, p.y = t;
    }
    in.drawing = p.rtrigger >= VR_PAD_PRESS;
    // a menu up (the pause menu's pages, the map, the start screen, a dialogue): BetterVR's menu buttons, by their
    // letters (B goes back, the triggers turn the pages). A button down when the mode changes is left out until it is
    // let go: the right stick's click held for the map would close it (+), the A that closed a dialogue would grab
    extern bool framepace_menu_ui(void), framepace_map_ui(void), framepace_title_ui(void), framepace_dialogue_ui(void);
    bool menu = in.bettervr && (framepace_menu_ui() || framepace_title_ui() || framepace_dialogue_ui());
    {
        uint32_t down = (uint32_t)p.a | (uint32_t)p.b << 1 | (uint32_t)p.x << 2 | (uint32_t)p.y << 3 |
                        (uint32_t)p.lclick << 4 | (uint32_t)p.rclick << 5 | (uint32_t)p.menu << 6 |
                        (uint32_t)(p.ltrigger >= VR_PAD_PRESS) << 7 | (uint32_t)(p.rtrigger >= VR_PAD_PRESS) << 8 |
                        (uint32_t)(p.lgrip >= VR_PAD_PRESS) << 9 | (uint32_t)(p.rgrip >= VR_PAD_PRESS) << 10;
        if (menu != in.menu_mode) {
            in.masked = down;
            in.menu_mode = menu;
            if (menu) in.menus++;
            if (getenv("SAVAGEWIND_VR_TRACE_GESTURES")) LOG("[vr] controls: %s\n", menu ? "menu (by letters)" : "play (gestures)");
        }
        in.masked &= down;
        uint32_t m = in.masked;
        if (m & 1) p.a = false;
        if (m & 2) p.b = false;
        if (m & 4) p.x = false;
        if (m & 8) p.y = false;
        if (m & 16) p.lclick = false;
        if (m & 32) p.rclick = false;
        if (m & 64) p.menu = false;
        if (m & 128) p.ltrigger = 0;
        if (m & 256) p.rtrigger = 0;
        if (m & 512) p.lgrip = 0;
        if (m & 1024) p.rgrip = 0;
        extern void input_note_vr(void);
        if ((down & ~m) || fabsf(p.lx) + fabsf(p.ly) + fabsf(p.rx) + fabsf(p.ry) > 0.6f) input_note_vr();   // (the button prompts: a controller's, not the keyboard's)
    }
    if (menu) {
        vr_pad_in sticks = p;
        sticks.a = sticks.b = sticks.x = sticks.y = sticks.lclick = sticks.rclick = false;
        sticks.ltrigger = sticks.rtrigger = sticks.lgrip = sticks.rgrip = 0;
        bool snap = in.pad.snap;
        in.pad.snap = false;   // (the right stick scrolls; no turning in a menu)
        vr_pad_map(&in.pad, &sticks, now / 1e9, &in.out);
        in.pad.snap = snap;
        p.menu = false;  // menu taps are emitted on release; a hold is the overlay shortcut
        in.out.buttons = vr_pad_menu_buttons(&p, framepace_map_ui());
        if (now / 1e9 < in.pad.plus_until) in.out.buttons |= 1ull << VRB_PLUS;
        in.out.snap = 0;
        if (in.out.overlay) menu_request_overlay_toggle();
        in.shield = false, in.attack_until = 0;
        rumble_tick(now);
        return;
    }
    if (in.bettervr) {
        vr_pad_in sticks = p;   // (the sticks and snap turning from the classic map; the buttons from the gestures)
        sticks.a = sticks.b = sticks.x = sticks.y = sticks.lclick = sticks.rclick = false;
        sticks.ltrigger = sticks.rtrigger = sticks.lgrip = sticks.rgrip = 0;
        uint64_t buttons = gestures(&p, now / 1e9);
        // a quick menu held open (a shoulder's grip, a trigger): the right stick picks in it, so snap turning lets its
        // sideways push through (it took it for turning, and the weapons could not be chosen; 2026-11-24)
        bool snap = in.pad.snap;
        if (vr_g_quick_menu(&in.gest) || framepace_quick_ui()) in.pad.snap = false;
        vr_pad_map(&in.pad, &sticks, now / 1e9, &in.out);
        in.pad.snap = snap;
        in.out.buttons = buttons;
        in.raw_ry = in.out.ry;
        if (in.magnesis) in.gest.stick_ry_set = false;   // (the object follows the hand: vr_magnesis.c)
        if (in.gest.stick_ry_set) in.out.ry = (int32_t)(in.gest.stick_ry * 32767);   // (magnesis by hand)
        if (now / 1e9 < in.pad.plus_until) in.out.buttons |= 1ull << VRB_PLUS;
    } else {
        vr_pad_map(&in.pad, &p, now / 1e9, &in.out);
        in.raw_ry = in.out.ry;
    }
    if (in.out.overlay) menu_request_overlay_toggle();
    bool quick = vr_g_quick_menu(&in.gest) || framepace_quick_ui();
    if (in.magnesis && !quick) in.out.rx = in.out.ry = 0, in.out.snap = 0;   // (the right stick pulls or pushes it)
    if (!quick) in.out.rx = (int32_t)(in.out.rx * in.turn_speed);
    if (vr_g_quick_menu(&in.gest) && getenv("SAVAGEWIND_VR_TRACE_GESTURES")) {   // (what the picking stick gives the game)
        static uint64_t last;
        if (now - last > 300000000ull)
            last = now, LOG("[vr] quick menu open: right stick %.2f %.2f -> game %d %d, snap %d (turning %s)\n", p.rx, p.ry,
                            in.out.rx, in.out.ry, in.out.snap, in.pad.snap ? "by steps" : "smooth");
    }
    if (in.out.snap) {
        vr_turn(-in.out.snap * in.snap_deg * 3.14159265f / 180);  // (right: clockwise from above)
        moving.snap_s = now / 1e9;
    }
    if (in.motion) motion(now / 1e9);
    rumble_tick(now);
    walk_turn();
}

static int32_t clamp_axis(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }


float vr_stick_motion(void) {
    double now = (double)plat_now_ns() / 1e9, since = now - moving.snap_s;
    float snap = moving.snap_s > 0 && since >= 0 && since < 0.3 ? 1.0f : 0.0f;
    float s = moving.stick;
    return s > snap ? s : snap;
}

// runtime/hle/input.c, where the game's input is put together: the controllers' buttons and sticks added to it
// (lx... NULL: the buttons only)
void vr_input_apply(uint64_t *b, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    if (!in.ready || !vr_enabled() || !vr_session_running()) return;
    pthread_mutex_lock(&in.lock);
    update();
    vr_pad_out o = in.out;
    int32_t pull = in.raw_ry;
    pthread_mutex_unlock(&in.lock);
    // Magnesis holding something: moved by the main hand, the right stick's up-down nearer / further (vr_magnesis.c)
    extern bool vr_magnesis_drive(int hand, float pull);
    if (lx) in.magnesis = vr_magnesis_drive(in.left_handed ? 0 : 1, (float)pull / 32767.0f);
    if (in.magnesis) o.buttons &= ~(1ull << VRB_UP | 1ull << VRB_DOWN);
    *b |= o.buttons;
    if (!lx) return;
    // (no turning of the left stick for the head: the game walks Link relative to the view it draws, which already
    // has the head's turn; tools/vr/test_vr_game.py firstperson)
    // Ladders (C34, as BetterVR's hook_FixLadder): on one (the move flags' bit 4 alone, BetterVR's "climbing ladder";
    // 0x10, 0x12 stepping on or off) the game reads the stick against the view, so looking aside while climbing turned
    // "up" into a step off the side. There the stick is turned from the view's way to the way Link faces: forward goes
    // up the ladder, back down, wherever the player looks. SAVAGEWIND_VR_LADDER_TEST=1 does it everywhere (the sign
    // checked walking on flat ground: tools/vr/test_vr_ladder.py)
    bool ladder = false;
    if (o.lx || o.ly) {
        extern bool vr_player_flags(uint32_t *f), vr_skel_facing(float *yaw), vr_view_heading(float *yaw);
        static int test = -1;
        if (test < 0) test = getenv("SAVAGEWIND_VR_LADDER_TEST") && *getenv("SAVAGEWIND_VR_LADDER_TEST") == '1';
        uint32_t mf;
        float face, view;
        bool on = test || (vr_player_flags(&mf) && (mf & ((1u << 4) | (1u << 7) | (1u << 10))) == (1u << 4));
        if (on && vr_skel_facing(&face) && vr_view_heading(&view)) {
            float a = remainderf(face - view, 2 * 3.14159265f), cs = cosf(a), sn = sinf(a), x = (float)o.lx, y = (float)o.ly;
            o.lx = (int32_t)(x * cs - y * sn), o.ly = (int32_t)(x * sn + y * cs);
            ladder = true;
            ladder_frames++;
        }
    }
    if (!ladder && walk.mode == 1 && (o.lx || o.ly)) {   // (by the controller: turned from the head's way to the controller's)
        float a = walk.turn, cs = cosf(a), sn = sinf(a), x = (float)o.lx, y = (float)o.ly;
        o.lx = (int32_t)(x * cs - y * sn), o.ly = (int32_t)(x * sn + y * cs);
    }
    // roomscale (vr_camera.c): the player walking in the room walks Link, while no stick is pushed
    extern bool vr_roomscale_stick(float *x, float *y);
    float rsx, rsy;
    bool room = false;
    if (vr_roomscale_stick(&rsx, &rsy) && abs(o.lx) < 3000 && abs(o.ly) < 3000 && abs(*lx) < 3000 && abs(*ly) < 3000)
        o.lx = (int32_t)(rsx * 32767), o.ly = (int32_t)(rsy * 32767), room = true;
    *lx = clamp_axis(*lx + o.lx), *ly = clamp_axis(*ly + o.ly);
    float walking = room ? 0 : sqrtf((float)*lx * (float)*lx + (float)*ly * (float)*ly) / 32767.0f;
    // Head-only turning disables the horizontal camera stick from every input source in first-person play.
    // Vertical input remains available for aiming and Magnesis; menus and third person keep their normal controls.
    extern bool framepace_quick_ui(void);
    if (vr_opt(VR_OPT_TURN) == 2 && vr_opt(VR_OPT_VIEW) == 0 && vr_world_view() &&
        !vr_g_quick_menu(&in.gest) && !framepace_quick_ui())
        *rx = 0, o.rx = 0;   // (a quick menu open: the stick picks in it)
    *rx = clamp_axis(*rx + o.rx), *ry = clamp_axis(*ry + o.ry);
    if (in.magnesis && !framepace_quick_ui() && !vr_g_quick_menu(&in.gest)) *rx = *ry = 0;
    float turning = (float)abs(*rx) / 32767.0f;
    walking = walking < 0.15f ? 0 : walking > 1 ? 1 : walking;   // (the sticks' rest: none)
    turning = turning < 0.15f ? 0 : turning;
    moving.stick = walking > turning ? walking : turning;
    if (turning > 0) moving.turned_ns = plat_now_ns();
}

// Savage Wind Configuration in the headset (ui/launcher.mm): the controllers as they are, none of the game's mapping or
// gestures - bits: 0 A, 1 B, 2 X, 3 Y, 4 left stick click, 5 right stick click, 6 menu, 7 left trigger, 8 right trigger,
// 9 left grip, 10 right grip; sticks: left x y, right x y (-1..1, +y up). False while the controllers aren't ours.
bool vr_menu_pad(uint32_t *buttons, float sticks[4]) {
    *buttons = 0;
    sticks[0] = sticks[1] = sticks[2] = sticks[3] = 0;
    if (!in.ready || !vr_enabled() || !vr_session_running()) return false;
    // (its own sync and reads: update() goes on to the gestures, which read the game - not loaded on this screen)
    pthread_mutex_lock(&in.lock);
    XrActiveActionSet as = {in.set, XR_NULL_PATH};
    XrActionsSyncInfo si = {XR_TYPE_ACTIONS_SYNC_INFO, NULL, 1, &as};
    bool ok = xrSyncActions(in.session, &si) == XR_SUCCESS;
    vr_pad_in p = {0};
    if (ok) {
        get_vec(A_MOVE, &p.lx, &p.ly);
        get_vec(A_CAMERA, &p.rx, &p.ry);
        p.a = get_bool(A_A), p.b = get_bool(A_B), p.x = get_bool(A_X), p.y = get_bool(A_Y);
        p.lclick = get_bool(A_LCLICK), p.rclick = get_bool(A_RCLICK), p.menu = get_bool(A_MENU);
        p.ltrigger = get_float(A_LTRIGGER), p.rtrigger = get_float(A_RTRIGGER);
        p.lgrip = get_float(A_LGRIP), p.rgrip = get_float(A_RGRIP);
    }
    pthread_mutex_unlock(&in.lock);
    if (!ok) return false;
    *buttons = (uint32_t)p.a | (uint32_t)p.b << 1 | (uint32_t)p.x << 2 | (uint32_t)p.y << 3 | (uint32_t)p.lclick << 4 |
               (uint32_t)p.rclick << 5 | (uint32_t)p.menu << 6 | (uint32_t)(p.ltrigger >= VR_PAD_PRESS) << 7 |
               (uint32_t)(p.rtrigger >= VR_PAD_PRESS) << 8 | (uint32_t)(p.lgrip >= VR_PAD_PRESS) << 9 |
               (uint32_t)(p.rgrip >= VR_PAD_PRESS) << 10;
    sticks[0] = p.lx, sticks[1] = p.ly, sticks[2] = p.rx, sticks[3] = p.ry;
    return true;
}

// for `vr`: a quick menu is held open by a hand (vr_gestures.h)
bool vr_quick_menu_open(void) { return vr_g_quick_menu(&in.gest); }

// vr_camera.c: the turn stick (any controller) was pushed in the last 0.4 s - the game's camera may still be turning
bool vr_turn_stick_recent(void) {
    uint64_t t = moving.turned_ns;
    return t && plat_now_ns() - t < 400000000ull;
}

// for `vr`: the controllers' state as the game last got it
uint32_t vr_swings_ignored(void) { return in.swings_ignored; }
uint32_t vr_no_shake(void) { return in.no_shake; }

void vr_motion_state(uint32_t *attacks, uint32_t *slashes, uint32_t *stabs, float *power, bool *shield, bool *on) {
    pthread_mutex_lock(&in.lock);
    *attacks = in.attacks, *slashes = in.slashes, *stabs = in.stabs, *power = in.last_power;
    *shield = in.shield, *on = in.motion;
    pthread_mutex_unlock(&in.lock);
}

bool vr_input_state(uint64_t *buttons, int32_t sticks[4], uint64_t *syncs) {
    pthread_mutex_lock(&in.lock);
    *buttons = in.out.buttons;
    sticks[0] = in.out.lx, sticks[1] = in.out.ly, sticks[2] = in.out.rx, sticks[3] = in.out.ry;
    *syncs = in.syncs;
    bool ok = in.ready && in.active;
    pthread_mutex_unlock(&in.lock);
    return ok;
}

// The action set can sync while neither controller has a usable grip pose (for example, controls asleep in VDXR).
// Keep these three states separate in `vr` so a headset run tells binding, validity and tracking apart.
void vr_hand_tracking_state(bool active[2], bool valid[2], bool tracked[2]) {
    active[0] = active[1] = valid[0] = valid[1] = tracked[0] = tracked[1] = false;
    if (!vr_session_running()) return;
    pthread_mutex_lock(&in.lock);
    XrTime t = vr_next_display_time();
    if (in.ready && in.active && t) for (int h = 0; h < 2; h++) {
        if (!in.hand[h]) continue;
        XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO, NULL, in.a[A_HAND], in.hand_path[h]};
        XrActionStatePose st = {XR_TYPE_ACTION_STATE_POSE};
        if (XR_FAILED(xrGetActionStatePose(in.session, &gi, &st)) || !st.isActive) continue;
        active[h] = true;
        XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(in.hand[h], vr_xr_local(), t, &loc))) continue;
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                          XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        const XrSpaceLocationFlags tracking = XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
                                              XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        valid[h] = (loc.locationFlags & need) == need;
        tracked[h] = valid[h] && (loc.locationFlags & tracking) == tracking;
    }
    pthread_mutex_unlock(&in.lock);
}

// xr_vulkan.c: a hand's grip space (0 left, 1 right), for what is drawn at the controller
XrSpace vr_hand_space(int hand) { return in.ready && hand >= 0 && hand < 2 ? in.hand[hand] : XR_NULL_HANDLE; }

// vr_skel.c: a hand's grip pose now (tracking space), 0 left 1 right (the left-handed swap not applied: the real hand)
// SAVAGEWIND_VR_SIM_HANDS=<file> (tests on a real headset, tools/vr/handsim.py --runtime-json): the hands' poses come
// from the file instead of the controllers, relative to the head (its position and heading): lines
// `hand left|right x y z yaw pitch roll` (m, degrees: x right, y up, -z ahead; fake_xr's euler order) or `handq left|right
// x y z qx qy qz qw`. Read again at
// most every 50 ms when it changed. The gestures and motion combat still read the real controllers
static struct { bool on, have[2]; vr_pose rel[2]; char path[1024]; uint64_t read_ns; long size; pthread_mutex_t lock; }
    sim = {.lock = PTHREAD_MUTEX_INITIALIZER};
static void sim_read(void) {
    uint64_t now = plat_now_ns();
    if (now - sim.read_ns < 50000000ull) return;
    sim.read_ns = now;
    FILE *f = fopen(sim.path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char side[8];
        float v[7];
        if (sscanf(line, " handq %7s %f %f %f %f %f %f %f", side, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 8) {
            int h = !strcmp(side, "right");
            sim.rel[h] = (vr_pose){vr_quat_normalize((vr_quat){v[3], v[4], v[5], v[6]}), vr_v3(v[0], v[1], v[2])};
            sim.have[h] = true;
            continue;
        }
        if (sscanf(line, " hand %7s %f %f %f %f %f %f", side, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 7) continue;
        int h = !strcmp(side, "right");
        const float d = VR_PI / 180;
        vr_quat q = vr_quat_mul(vr_quat_yaw(v[3] * d), vr_quat_mul(vr_quat_axis_angle(vr_v3(1, 0, 0), v[4] * d),
                                                                  vr_quat_axis_angle(vr_v3(0, 0, 1), v[5] * d)));
        sim.rel[h] = (vr_pose){vr_quat_normalize(q), vr_v3(v[0], v[1], v[2])};
        sim.have[h] = true;
    }
    fclose(f);
}
static bool sim_hand(int hand, vr_pose *out) {
    static int init;
    if (!init) {
        init = 1;
        const char *e = getenv("SAVAGEWIND_VR_SIM_HANDS");
        if (e && *e) snprintf(sim.path, sizeof sim.path, "%s", e), sim.on = true;
    }
    if (!sim.on) return false;
    XrTime t = vr_next_display_time();
    XrSpaceLocation hl = {XR_TYPE_SPACE_LOCATION};
    if (!t || XR_FAILED(xrLocateSpace(vr_xr_view(), vr_xr_local(), t, &hl)) ||
        !(hl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        return false;
    vr_pose head = pose_of(hl.pose);
    pthread_mutex_lock(&sim.lock);
    sim_read();
    bool ok = sim.have[hand];
    if (ok) {
        vr_quat yaw = vr_quat_yaw(vr_quat_heading(head.q));
        out->p = vr_v3_add(head.p, vr_quat_rotate(yaw, sim.rel[hand].p));
        out->q = vr_quat_normalize(vr_quat_mul(yaw, sim.rel[hand].q));
    }
    pthread_mutex_unlock(&sim.lock);
    return ok;
}

bool vr_hand_pose(int hand, vr_pose *out) {
    extern bool vr_pair_hand(int hand, vr_pose *io, bool located);   // (vr_camera.c: Stereo, once a pair)
    if (in.ready && sim_hand(hand, out)) return vr_pair_hand(hand, out, true);
    if (!in.ready || !in.hand[hand]) return false;
    XrTime t = vr_next_display_time();
    XrSpaceLocation l = {XR_TYPE_SPACE_LOCATION};
    bool ok = t && XR_SUCCEEDED(xrLocateSpace(in.hand[hand], vr_xr_local(), t, &l)) &&
              (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (l.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    if (ok) *out = pose_of(l.pose);
    return vr_pair_hand(hand, out, ok);
}


// nvn/glyphs.c: the game's button prompts as the Quest Touch controls - 0 not in use (no session, the controllers off or
// not giving input), 1 BetterVR's controls in play, 3 the Quest layout's in play, 2 the buttons by their letters (a menu,
// a dialogue: menu_mode; the classic map)
int vr_touch_prompts(void) {
    if (!in.ready || !in.active || !vr_enabled() || !vr_session_running()) return 0;
    return in.bettervr && !in.menu_mode ? (in.quest ? 3 : 1) : 2;
}

// the main hand's trigger held (in the BetterVR scheme with the bow out: drawing it)
bool vr_drawing(void) { return in.ready && in.drawing; }

// the main hand's swing as the motion analysis sees it (vr_melee.h, BetterVR's): on (an attack going), its power and
// the number of the attack (one more for each swing begun); vr_skel.c turns the held weapon's attack sensor on with it
bool vr_swing(float *power, uint32_t *id) {
    if (!in.ready || !in.motion) return false;
    pthread_mutex_lock(&in.lock);
    bool on = in.melee.active && in.swing_ok && !(plat_now_ns() / 1e9 < in.quiet_until);
    *power = in.melee.power, *id = in.melee.attacks;
    pthread_mutex_unlock(&in.lock);
    return on;
}

// the drop gesture (vr_gestures.h, the right waist held) is on: in play with the BetterVR scheme, unless
// SAVAGEWIND_VR_DROP=0; it uses the quick selector's native drop (runtime/quick_drop_action.c) even when that setting
// is off
bool vr_drop_gesture(void) {
    return vr_opt_on(VR_OPT_DROP) && in.ready && in.bettervr && vr_enabled() && vr_session_running();
}

// a hand's grip held now (0 left, 1 right; the hands' roles as the player set them)
bool vr_grip_held(int hand) {
    if (!in.ready) return false;
    return in.grip_now[in.left_handed ? 1 - hand : hand];
}
