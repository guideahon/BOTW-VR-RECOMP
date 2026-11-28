// VR mode, phases 2 and 3: the headset moves the game's camera, the game draws the headset's field of view, one eye a
// frame.
//
// In play (not paused, no event, no title, map or loading screen) the world's view cameras (runtime/hle/freecam.c
// rt_lookat: sead::LookAtCamera eye / at / up) take an eye's pose on top of the game's own camera: the game's camera
// keeps its position and heading (it still follows Link and turns with the stick), the head adds its turn and its
// movement since the last recentring, and gives the pitch and roll itself (a view that tilts without the head tilting
// makes people sick). The world camera's projection (runtime/nvn/output_scale.c rt_persp) becomes a symmetric frustum
// that holds the eye's field, in the window's shape (the window takes the headset's shape at start: vr.c).
//
// Stereo (phase 3): the game's frames come in pairs, left eye then right eye (SAVAGEWIND_VR_STEREO=0: one picture for
// both, phase 2). The game's clock (runtime/hle/framepace.c rt_vfr_update, vr_frame_ratio) takes both frames' time in
// each taking half the pair's time, so the game's world keeps its speed at twice the headset refresh rate. The headset's
// frame is made once a pair (xr_vulkan.c), so at 90 Hz the game runs at 180 fps.
//
// Which eye, pose and frustum a frame was drawn with travels with it: nvnQueuePresentTexture (the game's thread, at the
// end of its frame: vr_present_mark) queues it, the present on the queue thread (vr_present_take) takes it in the same
// order, and the headset is told exactly what each picture is.
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "../framepace.h"
#include "../platform.h"
#include "../game_sites.h"
#include "../control_cells.h"
#include "vr_event_table.h"
#include "vr_options.h"
#include "../settings.h"
#include "../color_grade.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

#include "vr.h"
#include "vr_internal.h"
#include "vr_math.h"
#include "vr_pair.h"

#define QUEUE 32

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int stereo;            // -1 not read yet
    bool have_neutral;     // the head's pose at the last recentring
    vr_pose neutral;
    bool recenter;         // asked for (vr_recenter): taken at the next camera update
    float turn;            // snap turning's turns so far (radians, +: to the left), on top of the game camera's heading
    float yaw_prev;        // the game camera's heading at the last camera (radians), and when (plat_now_ns)
    uint64_t yaw_prev_ns;
    uint64_t kept_turns;   // cameras whose own fast turn was kept off the view (the left trigger's recentre)
    float window_aspect;   // the window image's width / height
    float out[9];          // the last eye / at / up written
    float outs[8][9];      // ... and the ones before it (both eyes', the frame's other cameras): what the game may hand back
    int outs_n;
    uint64_t pair_hands_kept;   // right frames whose hands were their left frame's (vr_pair_hand)
    float view_out[9];     // ... the view's own, before a debug camera moved it (vr_view_heading)
    bool out_ok;
    float game[9];         // the game's own eye / at / up, as it last set them: a camera that holds still is not set
                           // again, and what rt_lookat then sees is what was written here last (so is the camera's copy)
    vr_pair pair;          // stereo: which eye each frame draws, and its game time (vr_pair.h)
    bool cam_set;          // a camera of this frame has been set, not yet presented
    vr_view cur;           // what the frame is drawn as (its cameras' last setting)
    float tan_x, tan_y;    // the frustum this frame's world camera gets
    vr_view q[QUEUE];      // frames presented by the game, not yet by the queue thread
    unsigned q_head, q_count;
    uint64_t updates;
    int first;             // -1 not read yet: SAVAGEWIND_VR_VIEW (first, the default, or third)
    float eye_height, forward;  // first person: the eye above Link's feet, and ahead of his head's centre (m)
    float eye_used;             // ... the height used last (Standing: the player's own)
    float climb_eye;            // climbing: how far the eye is put back from the wall (0..1)
    bool fp_now;           // the last camera was set in first person
    int events;            // -1 not read yet: SAVAGEWIND_VR_EVENTS (EV_*)
    bool in_event;         // the last camera was set in an event
    float event_yaw;       // the view's turn kept through a still first-person event (the one before it)
    float last_yaw;
    float off_x, off_y;    // the frustum's centre off the view's axis (tan units; 0: symmetric)
    uint64_t ui_projected; // HUD labels and markers moved onto the floating panel (vr_ui_project)
    uint64_t ui_stamina;   // ... the stamina wheel kept under the hearts in first person
    struct {               // roomscale: Link walked under the head (vr_roomscale_stick)
        bool driving, have_lp;
        double lp[3];      // Link's position at the last camera
        float x, y;        // the walk stick it asks for (-1..1; y ahead)
        float gx, gz;      // the head from Link's eye on the ground at the last camera (m, the game's world)
        float best;        // the nearest the head got while driving, and when (plat_now_ns): a stall gives up
        uint64_t best_ns;
        uint64_t frames;   // cameras it walked him on
    } rs;
    vr_anchor anchor;      // the tracking space -> the game's world, as the last camera used it
    bool anchor_ok;
    XrView pair_views[2];  // stereo: the eyes as the left frame located them, which the right frame of the pair uses too
    uint64_t pair_views_id;
    vr_anchor pair_anchor; // stereo: the left frame's anchor, which the right frame of the same pair draws with too
    uint64_t pair_anchor_id;   // ... that pair's id (0: none)
    bool pair_anchor_fp;       // ... and whether it was first person
    double pair_anchor_lp[3];  // ... and Link's position it was made from (first person)
    bool pair_anchor_lp_ok;
    double anchor_lp[3];   // Link's position that camera was made from (first person)
    bool anchor_lp_ok;
    uint64_t pair_anchors;     // right frames drawn with their left frame's anchor (the `vr` command)
    float anchor_turn_yaw;     // the game heading (with the turns) c.anchor was made with
    bool anchor_yaw_ok, anchor_fp_was;
    uint64_t anchor_frames, anchor_mismatch;  // first-person stereo frames, and those drawn turned against their skeleton
    bool skel_before_cam;  // the hands were placed (vr_tracking_to_world) in this frame before its camera
    uint64_t skel_first_frames;  // first-person frames whose hands were placed before their camera (the `vr` command)
    uint64_t pair_anchor_counted;
    vr_view sync_cur1;     // synchronous stereo (vr_sync.c): the right eye of this frame, presented after the left
    bool sync_cur1_ok;
} c = {.stereo = -1, .window_aspect = 1, .first = -1, .events = -1};

static float env_f(const char *name, float def, float lo, float hi) {
    const char *e = getenv(name);
    float v = e ? (float)atof(e) : def;
    return v >= lo && v <= hi ? v : def;
}

// SAVAGEWIND_VR_VIEW=first (the default): the eye is in Link's head, as in BetterVR; third: on the game's own camera
// behind him. SAVAGEWIND_VR_EYE_HEIGHT (1.55 m above his feet) and SAVAGEWIND_VR_EYE_FORWARD (0.22 m ahead, out of his
// head's model) place it.
static bool first_person(void) {
    bool first = vr_opt(VR_OPT_VIEW) == 0;   // (live: the Options screen's VR page)
    if (c.first < 0 || first != c.first) {
        c.first = first;
        c.eye_height = env_f("SAVAGEWIND_VR_EYE_HEIGHT", 1.55f, 0.3f, 3.0f);
        c.forward = env_f("SAVAGEWIND_VR_EYE_FORWARD", 0.22f, 0.0f, 1.0f);
        if (c.first) LOG("[vr] first person: the eye in Link's head (%.2f m up, %.2f m ahead; SAVAGEWIND_VR_VIEW=third: "
                         "the game's camera)\n", c.eye_height, c.forward);
        else LOG("[vr] third person: the view on the game's camera (SAVAGEWIND_VR_VIEW=third)\n");
    }
    return c.first;
}


static int trace_left = -1;
static void trace(const char *what, int eye, int world) {
    if (trace_left < 0) trace_left = getenv("SAVAGEWIND_VR_TRACE") ? atoi(getenv("SAVAGEWIND_VR_TRACE")) : 0;
    if (trace_left > 0 && vr_world_view()) {
        trace_left--;
        LOG("[vr-trace] %s eye %d world %d pair %llu\n", what, eye, world,
            (unsigned long long)vr_pair_id(&c.pair));
    }
}

// Synchronous stereo (SAVAGEWIND_VR_SYNC=1, in progress: docs/vr/SYNC_STEREO.md): every game frame is a pair's left
// frame, with the whole step; the right eye is the same frame's lists translated again with the right eye's camera
// (runtime/nvn/vulkan/exec.c vk_sync_replay), presented after the left one
// How the eyes are drawn (Options > VR > Rendering, vr_render_mode; SAVAGEWIND_VR_RENDER=auto|stereo|multiview|mono),
// decided once (a change applies after a restart):
//   stereo     a game frame per eye, in turn (the game's clock once a pair)
//   multiview  one game frame a pair, both eyes drawn in one pass into two layers (runtime/nvn/vulkan/exec.c mv_on)
//   mono       one picture from the head's centre for both eyes: no stereoscopic depth. Only when chosen, never automatic
//   automatic  stereo for now: in a headset multiview still showed things in one eye only (smoke, part of the floor,
//              an enemy's marker moved; 2026-11-25). With VR_AUTO_MULTIVIEW 1: multiview when the driver has it and
//              Video's Upscaler is off (multiview does not apply it yet; Sharpening and Colour it does); else stereo
// The older variables still win: SAVAGEWIND_VR_STEREO=0 mono; SAVAGEWIND_VR_SYNC=1 with SAVAGEWIND_VR_MV=1 multiview,
// alone the synchronous stereo prototype (a replay for the right eye: research only); SAVAGEWIND_VR_SYNC=0 or
// SAVAGEWIND_VR_MV=0 stereo. Multiview asked for on a driver without it falls back to stereo. Until the Vulkan device
// exists the driver's answer is unknown: VR_PATH_PENDING, asked again later.
enum { VR_PATH_PENDING = -1, VR_PATH_STEREO, VR_PATH_MULTIVIEW, VR_PATH_SYNC, VR_PATH_MONO };
#define VR_AUTO_MULTIVIEW 0   // (Automatic picks multiview: once both eyes match in every pass)
static const char *const path_names[] = {"stereo (a game frame per eye)", "multiview (both eyes in one pass)",
                                         "synchronous stereo prototype (SAVAGEWIND_VR_SYNC)",
                                         "mono (one picture for both eyes, no stereoscopic depth)"};

static bool env_is(const char *name, char v) {
    const char *e = getenv(name);
    return e && *e == v;
}

// what was asked for, before the driver is known: VR_PATH_* (multiview for automatic too) and whether it was automatic
static int path_asked(bool *autom) {
    *autom = false;
    if (env_is("SAVAGEWIND_VR_STEREO", '0')) return VR_PATH_MONO;
    if (env_is("SAVAGEWIND_VR_SYNC", '1')) return env_is("SAVAGEWIND_VR_MV", '1') ? VR_PATH_MULTIVIEW : VR_PATH_SYNC;
    if (env_is("SAVAGEWIND_VR_SYNC", '0') || env_is("SAVAGEWIND_VR_MV", '0')) return VR_PATH_STEREO;
    switch (vr_opt(VR_OPT_RENDER)) {
    case 1: return VR_PATH_STEREO;
    case 2:
        // (experimental: only with Options > VR > Experimental features on - a tester picking it should not get a
        // degraded game by surprise; the variables above stay for development)
        if (vr_opt(VR_OPT_EXPERIMENTAL) != 1) {
            static bool said;
            if (!said) said = true, LOG("[vr] Multiview chosen, but Options > VR > Experimental features is off: stereo\n");
            return VR_PATH_STEREO;
        }
        return VR_PATH_MULTIVIEW;
    case 3: return VR_PATH_MONO;
    default: *autom = true; return VR_AUTO_MULTIVIEW ? VR_PATH_MULTIVIEW : VR_PATH_STEREO;
    }
}

// Video's options multiview does not apply to its two-layer scene yet: the upscalers (they change the scene's size)
static const char *post_effect_on(void) {
    // (the eyes' depth to the compositor, opt-in: multiview does not attach it yet)
    if (env_is("SAVAGEWIND_VR_DEPTH", '1')) return "SAVAGEWIND_VR_DEPTH (the eyes' depth)";
    if (g_settings.upscaler != UPSCALE_OFF) return "Video > Upscaler";
    // (Sharpening and Colour: per eye in multiview too since 2026-11-26, exec.c mv_scene_post)
    return NULL;
}

static int render_option_started = -1;   // (Options > VR > Rendering as this run started with it)
// the Options page (game_options.mm): the Rendering row's choice this run is drawing with; -1 before VR draws
int vr_render_option_started(void) { return render_option_started; }

int vr_render_path(void) {
    static int path = VR_PATH_PENDING;
    if (path != VR_PATH_PENDING) return path;
    render_option_started = vr_opt(VR_OPT_RENDER);
    bool autom;
    int p = path_asked(&autom);
    if (p == VR_PATH_MULTIVIEW) {
        extern int vk_multiview_supported(void);
        int have = vk_multiview_supported();
        if (have < 0) return VR_PATH_PENDING;
        const char *fx = autom ? post_effect_on() : NULL;
        if (!have) {
            LOG("[vr] multiview %s, but the driver has no multiview: stereo\n", autom ? "(automatic)" : "asked for");
            p = VR_PATH_STEREO;
        } else if (fx) {
            LOG("[vr] rendering automatic: stereo, as %s is on (multiview does not apply it yet)\n", fx);
            p = VR_PATH_STEREO;
        }
    }
    path = p;
    LOG("[vr] rendering: %s%s\n", path_names[path], autom ? " (automatic)" : "");
    return path;
}

// mono or not is known before the driver (stereo_on): only the stereo paths wait for it
static bool mono_asked(void) {
    bool autom;
    return path_asked(&autom) == VR_PATH_MONO;
}

bool vr_mv_path(void) { return vr_render_path() == VR_PATH_MULTIVIEW; }

bool vr_sync_mode(void) {
    int p = vr_render_path();
    return p == VR_PATH_MULTIVIEW || p == VR_PATH_SYNC;
}

static bool stereo_on(void) {
    if (c.stereo < 0) {
        c.stereo = !mono_asked();
        LOG("[vr] %s\n", c.stereo ? "stereo: a frame per eye, the game's clock once a pair"
                                  : "mono: one picture for both eyes (SAVAGEWIND_VR_STEREO=0)");
    }
    return c.stereo;
}

// (vr_input.c vr_hand_pose) Stereo: the controllers located once a pair, as the head is (c.pair_views). Each game
// frame located them again, the right one a little later: a real runtime's prediction for the same display time moves
// on as new tracking comes, so with the hands moving the right eye drew them somewhere else than the left one -
// "the left and right hand rendering when moving gives a crosseyed effect" (26.2's tester, Stereo). fake_xr gives the
// same pose for the same time every time, which hid it. The pair's left frame locates and keeps them; its right frame
// draws with the kept ones. Not in synchronous stereo / multiview (one frame a pair) or mono, nor with
// SAVAGEWIND_VR_PAIR_ANCHOR=0 (each eye its own, as before)
static struct { uint64_t id; bool ok; vr_pose p; } pair_hand[2];
bool vr_pair_hand(int hand, vr_pose *io, bool located) {
    static int same = -1;
    if (same < 0) same = !(getenv("SAVAGEWIND_VR_PAIR_ANCHOR") && *getenv("SAVAGEWIND_VR_PAIR_ANCHOR") == '0');
    static int logn = -1;   // (SAVAGEWIND_VR_LOG_PAIR_HANDS=<n>: the first n locations, the eye and where, to the log)
    if (logn < 0) logn = getenv("SAVAGEWIND_VR_LOG_PAIR_HANDS") ? atoi(getenv("SAVAGEWIND_VR_LOG_PAIR_HANDS")) : 0;
    if (logn > 0 && located && hand == 1) {
        logn--;
        LOG("[vr-hand] eye %d pair %llu located x %.4f\n", c.pair.open ? c.pair.eye : -1,
            (unsigned long long)vr_pair_id(&c.pair), io->p.x);
    }
    if (hand < 0 || hand > 1 || !same || !stereo_on() || vr_sync_mode() || !vr_world_view()) return located;
    pthread_mutex_lock(&lock);
    // (the frame's eye as its clock step chose it, rt_vfr_update at the frame's start: not chosen here, as a call from
    // another thread would choose it at the wrong time)
    int eye = c.pair.open ? c.pair.eye : -1;
    uint64_t id = vr_pair_id(&c.pair);
    bool ok = located;
    if (eye == 1 && id && pair_hand[hand].id == id && pair_hand[hand].ok) {
        *io = pair_hand[hand].p, ok = true;
        c.pair_hands_kept++;
    } else if (eye == 0 && id && located) {
        pair_hand[hand].id = id, pair_hand[hand].ok = true, pair_hand[hand].p = *io;
    }
    pthread_mutex_unlock(&lock);
    return ok;
}
uint64_t vr_pair_hands_kept(void) { return c.pair_hands_kept; }

// ---- events (cutscenes): BetterVR's modes (SAVAGEWIND_VR_EVENTS) ----
//   auto    (the default) as BetterVR's table says for each event (runtime/vr/vr_event_table.h, from BetterVR's
//           patch_Settings_Cutscenes.asm): in first person or on the event's camera; unknown events on its camera
//   first   always in first person (in Link's head)
//   third   always on the event's camera, in 3D, the head free to look around
//   screen  on the virtual screen, flat (as before)
// In first person an event the table marks still (PAN_OFF) does not turn the view with its camera (the turn before it
// is kept), and one that leaves Link's hands to the animation (HND_OFF) gets no arms from the controllers.
enum { EV_AUTO, EV_FIRST, EV_THIRD, EV_SCREEN };

static int events_mode(void) {
    int m = vr_opt(VR_OPT_EVENTS);   // (live; EV_* in the option's order)
    if (m < EV_AUTO || m > EV_SCREEN) m = EV_AUTO;
    if (c.events != m) {
        c.events = m;
        static const char *const names[] = {"as BetterVR's table says", "in first person", "on the event's camera",
                                            "on the virtual screen"};
        LOG("[vr] events (cutscenes): %s (SAVAGEWIND_VR_EVENTS)\n", names[c.events]);
    }
    return c.events;
}

static const vr_event_setting *event_setting(const char *name) {
    int lo = 0, hi = (int)(sizeof vr_event_table / sizeof vr_event_table[0]) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, d = strcmp(name, vr_event_table[mid].name);
        if (!d) return &vr_event_table[mid];
        if (d < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return NULL;
}

// in an event now, how it is shown: fp in first person, still (its camera's turns not followed), anim_hands (no arms)
static bool event_now(bool *fp, bool *still, bool *anim_hands) {
    *fp = *still = *anim_hands = false;
    if (!framepace_in_event()) return false;
    int m = events_mode();
    if (m == EV_FIRST || m == EV_AUTO) {
        const vr_event_setting *s = event_setting(framepace_event_name());
        if (m == EV_FIRST || (s && s->fp)) {
            *fp = true;
            *still = !s || s->still;
            *anim_hands = !s || s->anim_hands;
        }
    }
    return true;
}

// First person: the eye follows Link's head down and up while he is crouched (his head 0.68 m lower), swimming or on a
// horse - and not his steps or his attacks: his head bone's height above his feet (vr_skel.c, as the animation has it;
// 1.45 m standing) is smoothed (a quarter second), and changes under 15 cm (a walk's bob: +-10 cm) are left out,
// growing to the full change at 30 cm. BetterVR lowers the view by a fixed 0.8 m crouching and moves it riding and
// swimming the same way. SAVAGEWIND_VR_HEAD_FOLLOW=0: the eye's height fixed, as before
static bool vr_rune_aiming(void);   // (below: a rune being aimed)
#define VR_HEAD_STANDING 1.449f
static float head_follow(void) {
    static int on = -1;
    static float smooth;
    static uint64_t last_ns;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_HEAD_FOLLOW") && *getenv("SAVAGEWIND_VR_HEAD_FOLLOW") == '0');
    extern bool vr_skel_head_height(float *y);
    float y;
    uint64_t now = plat_now_ns();
    if (!on || !vr_skel_head_height(&y)) {
        smooth = 0, last_ns = 0;
        return 0;
    }
    float d = y - VR_HEAD_STANDING;
    d = d < -1.3f ? -1.3f : d > 0.6f ? 0.6f : d;
    // only in the states that keep his head there (crouched, swimming, riding: the movement flags): an attack's or a
    // landing's dip of the head bone (an attack: -33 cm for half a second) moved the view down as if he crouched -
    // with the swings read too eagerly, the view kept dropping while the player stood (2026-11-20)
    extern bool vr_player_flags(uint32_t *f);
    uint32_t mf;
    if (!vr_player_flags(&mf) || !(mf & (1u << 2 | 1u << 10 | 1u << 20))) d = 0;
    float dt = last_ns ? (float)(now - last_ns) / 1e9f : 1.0f;
    last_ns = now;
    float k = 1 - expf(-(dt > 0.5f ? 0.5f : dt) / 0.25f);
    smooth += (d - smooth) * k;
    float a = fabsf(smooth), t = (a - 0.15f) / 0.15f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return smooth * t * t * (3 - 2 * t);
}

// ---- roomscale (BetterVR's: the player walking in the room walks Link) ----
// In first person, with the head more than 10 cm (horizontally) from Link's eye, the walk stick is pushed toward it,
// harder the farther (full at 40 cm), until it is within 4 cm: the game walks Link there, with its own collisions
// (BetterVR moves his body itself, casting against the world). The eye stays at the head meanwhile (vr_camera_lookat
// moves the neutral head with Link). Off while the player pushes the stick or plays an event; option VR_OPT_ROOMSCALE.
static void roomscale(vr_anchor a, vr_pose head, vr_vec3 geye, bool ev, const double *lp, const float *f) {
    if (c.fp_now) memcpy(c.rs.lp, lp, sizeof c.rs.lp), c.rs.have_lp = true;
    else c.rs.have_lp = false;
    vr_vec3 hw = vr_xr_to_game_point(a, head.p);
    float gx = hw.x - geye.x, gz = hw.z - geye.z, d = sqrtf(gx * gx + gz * gz);
    c.rs.gx = gx, c.rs.gz = gz;
    bool on = c.fp_now && !ev && vr_opt_on(VR_OPT_ROOMSCALE);
    uint64_t now = plat_now_ns();
    if (!on || d < 0.04f) c.rs.driving = false;
    else if (d > 0.10f && !c.rs.driving) c.rs.driving = true, c.rs.best = d, c.rs.best_ns = now;
    if (c.rs.driving && d < c.rs.best - 0.02f) c.rs.best = d, c.rs.best_ns = now;
    // he cannot get there (a wall, a rock, the water's pull) or the head is far off (leaning out of a chair, a recentre
    // gone stale): give up and put the view back over him (the room's head moved to his eye; BetterVR casts his body
    // against the world and stops there). Walking on would push him into the rock and leave the eye inside it
    if (c.rs.driving && (d > 0.6f || now - c.rs.best_ns > 700000000ull)) {
        c.neutral.p = vr_v3_add(c.neutral.p, vr_v3_scale(vr_quat_rotate(vr_quat_conj(vr_anchor_rotation(a)), vr_v3(gx, 0, gz)),
                                                          1.0f / a.scale));
        c.rs.driving = false, c.rs.gx = c.rs.gz = 0;
        static uint64_t said;
        if (now - said > 5000000000ull) said = now, LOG("[vr] roomscale: Link could not reach the head (%.2f m): the view back over him\n", d);
    }
    if (!c.rs.driving) {
        c.rs.x = c.rs.y = 0;
        return;
    }
    // the stick in the view's frame: ahead along the drawn view's heading, right across it
    float vy = vr_game_yaw_of(vr_v3(f[3] - f[0], 0, f[5] - f[2]));
    float ahead = (gx * sinf(vy) + gz * cosf(vy)) / d, right = (-gx * cosf(vy) + gz * sinf(vy)) / d;
    float m = (d - 0.04f) / 0.36f;
    m = m < 0.3f ? 0.3f : m > 1 ? 1 : m;
    c.rs.x = right * m, c.rs.y = ahead * m;
    c.rs.frames++;
}

// vr_input.c: the walk stick roomscale asks for now (false: none)
bool vr_roomscale_stick(float *x, float *y) {
    pthread_mutex_lock(&lock);
    bool on = c.rs.driving && vr_world_view();
    *x = c.rs.x, *y = c.rs.y;
    pthread_mutex_unlock(&lock);
    return on;
}
uint64_t vr_roomscale_frames(void) { return c.rs.frames; }

// vr_skel.c: Link's hands left to the event's animation now
bool vr_event_anim_hands(void) {
    bool fp, still, hands;
    return event_now(&fp, &still, &hands) && hands;
}

// framepace.c: the headset paces the game's frames (in VR with a session going)
bool vr_paces(void) { return vr_enabled() && vr_session_running(); }

// why an event is on the virtual screen, in the log once each time it changes (an item's card did not show in a Quest
// 3, 2026-11-20: which screen or state the game was in)
static bool world_view(const char **why);
bool vr_world_view(void) {
    const char *why = NULL;
    bool v = world_view(&why);
    static const char *said = "";
    if (!v && why && strcmp(why, said) && framepace_in_event()) {   // (why: only once the game is up and in play)
        LOG("[vr] event %s on the virtual screen: %s\n", framepace_event_name() ? framepace_event_name() : "?", why);
        said = why;
    } else if (v) {
        said = "";
    }
    return v;
}

static bool world_view(const char **why) {
    if (!vr_enabled() || !vr_session_running()) return false;
    // Enter the virtual screen when an NPC conversation starts, before MessageDialog's first draw. Switching from
    // the eye picture only once its layout is drawing made the text and choices disappear from the presented image.
    const char *event = framepace_in_event() ? framepace_event_name() : NULL;
    if (event && (!strncmp(event, "Npc_", 4) || strstr(event, "Talk"))) return *why = "an NPC dialogue", false;
    if (framepace_dialogue_ui()) return *why = "a dialogue", false;
    if (framepace_in_event()) {
        if (events_mode() == EV_SCREEN) return *why = "the events' mode is screen", false;
        // an event held at 30 fps (framepace.c events_at_30: one of its waits needs every update) is drawn flat, one
        // frame a headset frame: in stereo it would get 15 a second in each eye
        if (framepace_event_held()) return *why = "held at 30 fps", false;
        // BetterVR's black bars: an event on its own camera (not in first person) shown flat, as the game frames it
        bool fp, still, hands;
        if (vr_opt(VR_OPT_EVENT_BARS) == 1 && event_now(&fp, &still, &hands) && !fp) return *why = "black bars", false;
    }
    if (framepace_title_ui() || framepace_loading_ui() || framepace_map_ui()) return *why = "a title, loading or map screen", false;
    // the Sheikah Slate's scope: on the virtual screen, as the game draws it - its zoom (the headset's own field undid
    // it), its lens and frame (BetterVR 1.0.1 shows it flat). Up while its lens drew in the last few frames
    // (nvn/vulkan/exec.c scope_mark). SAVAGEWIND_VR_SCOPE_3D=1: in the eyes as play
    static int scope_3d = -1;
    if (scope_3d < 0) scope_3d = getenv("SAVAGEWIND_VR_SCOPE_3D") && *getenv("SAVAGEWIND_VR_SCOPE_3D") == '1';
    extern uint64_t vk_scope_frame(void);
    extern uint64_t renderer_present_count(void);
    uint64_t sf = vk_scope_frame();
    // (not in an event: the scope cannot be up in one, and the wake-up, Demo102_0, draws shaders it shares)
    if (!scope_3d && sf && !framepace_in_event() && renderer_present_count() + 1 < sf + 6) return *why = "the scope", false;
    if (g_site.pause_state && LD32(g_site.pause_state) != 0) {
        static char ps[48];
        snprintf(ps, sizeof ps, "the game's pause state %u", LD32(g_site.pause_state));
        return *why = ps, false;
    }
    // Savage Wind's own overlay menu (Options, Controls...) is drawn on the window's picture, which the eyes do not get:
    // while it is open the headset shows that picture on the virtual screen
    extern bool menu_is_open(void);
    if (menu_is_open()) return *why = "Savage Wind's overlay menu", false;
    // Link in the world: the boot's logos come before the start screen is seen as such (framepace_title_ui), and the
    // eyes, stereo and the head on their camera are for play (read once a presented frame: this is asked often)
    // (an event plays in the world even before Link is placed in it: the new game's opening, Demo169_0)
    if (framepace_in_event()) return true;
    extern bool output_player_exists(void);
    extern uint64_t renderer_present_count(void);
    static uint64_t asked = ~0ull;
    static bool player;
    uint64_t now = renderer_present_count();
    if (now != asked) asked = now, player = output_player_exists();
    return player;
}

void vr_recenter(void) {
    pthread_mutex_lock(&lock);
    c.recenter = true;
    pthread_mutex_unlock(&lock);
}

void vr_turn(float radians) {
    pthread_mutex_lock(&lock);
    c.turn = remainderf(c.turn + radians, 2 * VR_PI);
    pthread_mutex_unlock(&lock);
}

void vr_window_aspect(float a) {
    if (a > 0.1f && a < 10) c.window_aspect = a;
}

// runtime/hle/framepace.c rt_vfr_update, at the start of each game frame: which eye this frame is, and the game's
// step for it (ratio: console frames of game time)
//
// The game calls this and sets its cameras in either order within a frame, so the eye is chosen by whichever comes
// first after the last present (vr_present_mark): the frame's eye is fixed once its first camera is set, until then it
// is the eye that comes next.
float vr_frame_ratio(float ratio, bool paused) {
    bool world = !paused && vr_world_view();
    if (!world || !stereo_on()) return ratio;
    pthread_mutex_lock(&lock);
    float step = vr_pair_step(&c.pair, ratio);  // (half the pair's time in each frame by default)
    {   // (SAVAGEWIND_VR_PAIR_SECOND=<0..0.5>: the right frame's share instead of VR_PAIR_SECOND, to compare)
        static float sec = -1;
        if (sec < 0) sec = getenv("SAVAGEWIND_VR_PAIR_SECOND") ? (float)atof(getenv("SAVAGEWIND_VR_PAIR_SECOND")) : 0;
        if (sec > 0) step = vr_pair_eye(&c.pair) == 0 ? ratio * 2 * (1 - sec) : ratio * 2 * sec;
    }
    trace("frame", c.pair.eye, c.cam_set);
    pthread_mutex_unlock(&lock);
    return step;
}

static vr_pose from_xr(XrPosef p) {
    vr_pose r = {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                 {p.position.x, p.position.y, p.position.z}};
    return r;
}

static XrPosef to_xr(vr_pose p) {
    XrPosef r = {{p.q.x, p.q.y, p.q.z, p.q.w}, {p.p.x, p.p.y, p.p.z}};
    return r;
}

// both eyes at the time the frame being made should be shown
static XrTime located_t;   // (that time, for the frame's view: xr_vulkan.c compares it with the time it is shown at)
static bool locate(XrView v[2]) {
    XrTime t = vr_next_display_time();
    located_t = t;
    XrSession s = vr_xr_session();
    if (!t || s == XR_NULL_HANDLE) return false;
    XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO, NULL, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, t, vr_xr_local()};
    XrViewState vs = {XR_TYPE_VIEW_STATE};
    v[0] = (XrView){XR_TYPE_VIEW};
    v[1] = (XrView){XR_TYPE_VIEW};
    uint32_t n = 0;
    if (XR_FAILED(xrLocateViews(s, &li, &vs, 2, &n, v)) || n != 2) return false;
    return (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
}

// one of ours handed back: the same eye and the same way of looking (the game may put its at point at another distance
// along the line, and the up vector is left out: it can renormalise them)
static bool same_view(const float *a, const float *b) {
    for (int i = 0; i < 3; i++)
        if (fabsf(a[i] - b[i]) > 1e-3f) return false;
    float da[3] = {a[3] - a[0], a[4] - a[1], a[5] - a[2]}, db[3] = {b[3] - b[0], b[4] - b[1], b[5] - b[2]};
    float la = sqrtf(da[0] * da[0] + da[1] * da[1] + da[2] * da[2]), lb = sqrtf(db[0] * db[0] + db[1] * db[1] + db[2] * db[2]);
    if (la < 1e-6f || lb < 1e-6f) return false;
    return (da[0] * db[0] + da[1] * db[1] + da[2] * db[2]) / (la * lb) > 0.999999f;
}

static bool near9(const float *a, const float *b) {
    for (int i = 0; i < 9; i++)
        if (fabsf(a[i] - b[i]) > 1e-4f) return false;
    return true;
}

// (reverse engineering, `vr`) the last view camera given the head: its sead::LookAtCamera (f - 0x38)
static _Atomic uint64_t last_cam;
uint64_t vr_lookat_camera(void) { return last_cam; }

// rt_lookat, for a view camera (eye off the origin, up near +y): f = eye[3], at[3], up[3]
static bool quick_held;   // a quick menu was up while the view was on the virtual screen (vr_camera_lookat, below)
void vr_camera_lookat(float *f) {
    // A UI layout's camera (ui2d: the eye on the z axis, looking at the origin - PauseMenuInfo's at 0, 0, 989; others at
    // 0, 0, 10 in play) is not the world's. Given the head, it moved the layout's panes away and shrank them (the
    // inventory's item description, the hearts, a chest's item box: small and off to a side, 2026-11-24), and it also
    // set the game's view (c.game) between the world camera's own updates
    if (f[0] == 0 && f[1] == 0 && f[3] == 0 && f[4] == 0 && f[5] == 0) return;
    last_cam = (uint64_t)(uintptr_t)f - 0x38;
    { extern void vr_sync_note_camera(const float *); vr_sync_note_camera(f); }   // (vr_sync.c: the census)
    if (!vr_world_view()) {
        extern bool vr_quick_menu_open(void), framepace_quick_ui(void);
        if (vr_quick_menu_open() || framepace_quick_ui()) quick_held = true;
        c.out_ok = false, c.outs_n = 0;
        c.fp_now = false;   // (menus: Link's model as the game has it, the inventory's too)
        return;
    }
    pthread_mutex_lock(&lock);
    // (else: ours, the game's is kept). Not only the last one written: the game hands back the camera of another of its
    // calls - the other eye's, an earlier camera of the frame - and that, taken as the game's own, had the head's turn
    // put on it again: in third person, with the head turned, the view swung between the game's way plus the head's turn
    // and plus twice it, frame to frame and eye to eye ("the rotation is broken", 26.2's tester; first person takes its
    // place from Link and showed less)
    bool ours = c.out_ok && same_view(f, c.out);
    for (int i = 0; !ours && i < c.outs_n; i++) ours = same_view(f, c.outs[i]);
    if (!ours) memcpy(c.game, f, sizeof c.game);
    XrView v[2];
    if (!locate(v)) {
        pthread_mutex_unlock(&lock);
        return;
    }
    // Stereo: both eyes of a pair from one xrLocateViews, the left frame's - as an app drawing both eyes at once has
    // them. Located again in the right frame (a frame later), the head had turned between the eyes' poses: a runtime that
    // reprojects the pair as one head pose (WiVRn) showed the eyes apart while the head turned fast (2026-11-24).
    // SAVAGEWIND_VR_PAIR_ANCHOR=0: each frame its own, as before
    int eye = stereo_on() ? vr_pair_camera(&c.pair) : -1;
    {
        static int same = -1;
        if (same < 0) same = !(getenv("SAVAGEWIND_VR_PAIR_ANCHOR") && *getenv("SAVAGEWIND_VR_PAIR_ANCHOR") == '0');
        uint64_t id = eye >= 0 ? vr_pair_id(&c.pair) : 0;
        static XrTime pair_located;
        // (synchronous stereo / multiview: every frame is a left one, and the game sets its cameras more than once a
        // frame - each one located again gave one frame several head poses, so the right eye's camera made from one of
        // them missed the view the frame's constants were made with, and in a real headset - the head never still -
        // the right eye got the left eye's picture in about half the frames (2026-11-25). The frame's first location
        // for all of its cameras, as the right frame of a stereo pair does)
        if (eye == 0 && vr_sync_mode() && id && c.pair_views_id == id)
            memcpy(v, c.pair_views, sizeof c.pair_views), located_t = pair_located;
        else if (eye == 0) memcpy(c.pair_views, v, sizeof c.pair_views), c.pair_views_id = id, pair_located = located_t;
        else if (eye == 1 && same && id && c.pair_views_id == id)
            memcpy(v, c.pair_views, sizeof c.pair_views), located_t = pair_located;
    }
    // the head: halfway between the eyes, turned as the first one (the runtime turns both alike)
    vr_pose head = from_xr(v[0].pose);
    head.p = vr_v3_scale(vr_v3_add(head.p, from_xr(v[1].pose).p), 0.5f);
    bool recentred = !c.have_neutral || c.recenter;
    if (recentred) {
        c.neutral = head;
        c.have_neutral = true;
        c.recenter = false;
        LOG("[vr] recentred: head at %.2f %.2f %.2f, heading %.1f deg\n", head.p.x, head.p.y, head.p.z,
            vr_quat_heading(head.q) * 57.29578f);
    }
    bool first_cam = !c.cam_set;   // (this frame's first camera: the anchor the skeleton used is still c.anchor)
    vr_anchor skel_anchor = c.anchor;
    bool skel_anchor_ok = c.anchor_ok && c.fp_now;
    c.cam_set = true;
    trace("lookat", eye, c.cur.world);
    vr_pose view = eye >= 0 ? from_xr(v[eye].pose) : head;
    vr_pose tracked = view;   // (the eye as the headset has it: what the compositor is told the picture was drawn from)
    int world_percent = vr_opt(VR_OPT_WORLD_SCALE), ipd_percent = vr_opt(VR_OPT_IPD_SCALE);
    if (world_percent < 50 || world_percent > 150) world_percent = 100;
    if (ipd_percent < 50 || ipd_percent > 150) ipd_percent = 100;
    float world_scale = world_percent / 100.0f, ipd_scale = ipd_percent / 100.0f;
    if (eye >= 0 && ipd_scale != 1.0f)
        view.p = vr_v3_add(head.p, vr_v3_scale(vr_v3_sub(view.p, head.p), ipd_scale));
    static int last_world_scale = -1, last_ipd_scale = -1;
    if (world_percent != last_world_scale || ipd_percent != last_ipd_scale) {
        vr_vec3 l = from_xr(v[0].pose).p, r = from_xr(v[1].pose).p;
        float ipd_mm = vr_v3_len(vr_v3_sub(r, l)) * 1000.0f;
        LOG("[vr] tracking scale %d%%; eye-separation control %d%%; headset IPD %.1f mm, effective %.1f mm\n",
            world_percent, ipd_percent, ipd_mm, ipd_mm * world_scale * ipd_scale);
        last_world_scale = world_percent, last_ipd_scale = ipd_percent;
    }
    // the game's camera: where it is and the way it looks along the ground
    const float *gf = c.game;
    vr_vec3 geye = vr_v3(gf[0], gf[1], gf[2]);
    float yaw = vr_game_yaw_of(vr_v3(gf[3] - gf[0], gf[4] - gf[1], gf[5] - gf[2]));
    // first person: the neutral head goes to Link's eyes, a little ahead of his head, facing the game camera's way
    // an event: first person or its camera (event_now); a still one keeps the turn from before it
    bool ev_fp, ev_still, ev_hands, ev = event_now(&ev_fp, &ev_still, &ev_hands);
    if (ev && !c.in_event) {
        c.event_yaw = c.last_yaw;
        LOG("[vr] event %s: %s%s\n", framepace_event_name(), ev_fp ? "first person" : "its camera",
            ev_fp && ev_still ? ", the view kept still" : "");
    }
    c.in_event = ev;
    if (ev && ev_fp && ev_still) yaw = c.event_yaw;
    else c.last_yaw = yaw;
    bool fp = ev ? ev_fp : first_person();
    // first person in play: the game's camera swinging round fast by itself does not turn the view - the left trigger
    // (ZL) with nothing to lock on sets it behind Link, who may face anywhere (he walked back toward the view: a turn
    // of 180 degrees, 2026-11-20); a lock-on turns it to the target. Faster than 90 deg/s with the turn stick left
    // alone, its turn goes into c.turn against it. The game walks Link by the view drawn, so the stick still walks
    // where the player looks. While Magnesis is moved by the hand (vr_magnesis.c) every turn of it is kept from the
    // view, slow ones too: the game's camera follows the held object round, which turned the view and, with it, the
    // hand's aim in the world - the object ran away sideways. SAVAGEWIND_VR_KEEP_VIEW=0: the view turned with it
    {
        extern bool vr_magnesis_active(void);
        static int keep = -1;
        if (keep < 0) keep = !(getenv("SAVAGEWIND_VR_KEEP_VIEW") && *getenv("SAVAGEWIND_VR_KEEP_VIEW") == '0');
        extern bool vr_turn_stick_recent(void);
        uint64_t now = plat_now_ns();
        float d = remainderf(yaw - c.yaw_prev, 2 * VR_PI);
        float dt = c.yaw_prev_ns ? (float)(now - c.yaw_prev_ns) / 1e9f : 1.0f;
        bool mag = vr_magnesis_active();
        // A quick menu (runes, weapons): the right stick picks in it, and the game's camera turned with the push too -
        // its whole turn came on closing, and the view was left looking elsewhere (2026-11-24). While one is open and
        // for a moment after it closes, the camera's turns are kept from the view, as Magnesis'
        extern bool vr_quick_menu_open(void), framepace_quick_ui(void);
        static uint64_t quick_ns;
        // (the menu is drawn on the virtual screen, where this is not reached: quick_held says it was up)
        if (vr_quick_menu_open() || framepace_quick_ui() || quick_held) quick_ns = now, quick_held = false;
        // (the game does not update its camera while the menu is up: the whole turn comes in the first update after it
        // closes, long after the one before, so the gap between updates does not count here)
        bool quick = quick_ns && now - quick_ns < 500000000ull;
        if (quick && fabsf(d) > 0.01f && keep && fp && !ev && c.yaw_prev_ns) {
            static uint64_t said;
            if (now - said > 1000000000ull)
                said = now, LOG("[vr] quick menu: the camera's turn of %.0f deg kept from the view\n", d * 57.29578f);
        }
        if (keep && fp && !ev && c.yaw_prev_ns && (quick || dt < 0.25f) &&
            (mag || quick || (fabsf(d) > 1.5708f * (dt > 0.004f ? dt : 0.004f) && !vr_turn_stick_recent()))) {
            c.turn = remainderf(c.turn - d, 2 * VR_PI);
            c.kept_turns++;
        }
        c.yaw_prev = yaw, c.yaw_prev_ns = now;
    }
    // The heading the anchor is made with. Link's arms, hands and what they hold are placed (vr_tracking_to_world) before
    // this frame's camera, with the anchor the last camera left: a left frame that took a fresh heading drew them turned
    // against its own view by one game frame of the turn stick's turn - in that eye only, the right one drawing with the
    // left's anchor: near things crossed the eyes while turning smoothly (2026-11-24, WiVRn). A pair's left frame keeps
    // the heading the skeleton used (the right frame's, a game frame old); the right frame takes the fresh one, for the
    // next pair. SAVAGEWIND_VR_PAIR_ANCHOR=0: fresh each frame, as before
    float yaw_used = yaw + c.turn;
    {
        static int same = -1;
        if (same < 0) same = !(getenv("SAVAGEWIND_VR_PAIR_ANCHOR") && *getenv("SAVAGEWIND_VR_PAIR_ANCHOR") == '0');
        // (synchronous stereo / multiview: every frame is a pair's left one and there is no right frame to take the
        // fresh heading - kept, the view stood still and then jumped 0.35 rad at a time while turning, 2026-11-25)
        // (first person only: in third person the game's camera orbits Link, its place this frame's - with the last
        // frame's heading the view looked a game frame's orbit off its own place, 1-2 degrees in fake_xr and more at a
        // headset's game rate, and the turn juddered: "the camera rotation is broken" in third person, 25.7's tester)
        if (same && eye == 0 && fp && !vr_sync_mode() && c.anchor_ok && c.anchor_yaw_ok && fp == c.anchor_fp_was &&
            fabsf(remainderf(yaw_used - c.anchor_turn_yaw, 2 * VR_PI)) < 0.35f)   // (a cut, a lock-on snap: fresh)
            yaw_used = c.anchor_turn_yaw;
    }
    double lp[3];
    c.fp_now = false;
    if (fp && control_cell_get(&control_cell_player_pos, lp)) {
        // Seated: the centred head at Link's eyes. Standing (BetterVR's): the centred head's real height above the floor
        // (LOCAL's origin above STAGE's, plus the head's height in LOCAL), so a shorter or crouching player sees lower
        float h = c.eye_height, ly;
        extern bool vr_local_height(float *y);
        if (vr_opt(VR_OPT_PLAY_POSITION) == 1 && vr_local_height(&ly)) {
            h = c.neutral.p.y + ly;
            h = h < 0.4f ? 0.4f : h > 2.4f ? 2.4f : h;
        }
        h += head_follow();
        c.eye_used = h;
        // (the eye ahead of his head's centre the way the head looks, not the game's camera: looking back, it was
        // behind him and saw his back)
        float look = yaw_used + remainderf(vr_quat_heading(head.q) - vr_quat_heading(c.neutral.q), 2 * VR_PI);
        geye = vr_v3((float)lp[0] + sinf(look) * c.forward,
                     (float)lp[1] + h + vr_opt(VR_OPT_HEIGHT) / 100.0f,   // (the Options page's height)
                     (float)lp[2] + cosf(look) * c.forward);
        // climbing a wall (BetterVR's climbing camera, hook_CaptureClimbWallSurface): he faces the wall, so the eye
        // ahead of his head was in the rock; it goes back from the wall instead (0.15 m by default, set by
        // SAVAGEWIND_VR_CLIMB_BACK), smoothed in and out over a fifth of a second. SAVAGEWIND_VR_CLIMB_EYE=0: as elsewhere
        {
            static int on = -1;
            static float w;
            static uint64_t last;
            static float back = 0.15f;
            if (on < 0) {
                on = !(getenv("SAVAGEWIND_VR_CLIMB_EYE") && *getenv("SAVAGEWIND_VR_CLIMB_EYE") == '0');
                back = env_f("SAVAGEWIND_VR_CLIMB_BACK", 0.12f, 0.05f, 0.5f);
                LOG("[vr] climb camera: at Link's eyes, %.2f m back from the wall (SAVAGEWIND_VR_CLIMB_BACK)\n", back);
            }
            extern bool vr_player_flags(uint32_t *f);
            extern bool vr_skel_facing(float *yaw);
            uint32_t mf;
            float face;
            bool climbing = on && vr_player_flags(&mf) && (mf & (1u << 7)) && vr_skel_facing(&face);
            uint64_t now = plat_now_ns();
            float dt = last ? (float)(now - last) / 1e9f : 0;
            last = now;
            w += ((climbing ? 1.0f : 0.0f) - w) * (1 - expf(-(dt > 0.5f ? 0.5f : dt) / 0.2f));
            if (w > 0.001f && vr_skel_facing(&face)) {
                // (from his head bone as animated, not from his feet at standing height: climbing he hangs with his
                // head lower and nearer the wall, and the eye sat 0.38 m over it and 0.32 m back - "the head very far
                // from the body", 2026-11-26; now at his eyes, `back` off the wall. Without the bone, as before)
                extern bool vr_skel_head_world(float out[3]);
                float hw[3];
                vr_vec3 behind = vr_skel_head_world(hw)
                    ? vr_v3(hw[0] - sinf(face) * back, hw[1] + 0.08f + vr_opt(VR_OPT_HEIGHT) / 100.0f, hw[2] - cosf(face) * back)
                    : vr_v3((float)lp[0] - sinf(face) * back, geye.y, (float)lp[2] - cosf(face) * back);
                geye = vr_v3_add(geye, vr_v3_scale(vr_v3_sub(behind, geye), w));
                c.climb_eye = w;
            } else {
                c.climb_eye = 0;
            }
        }
        c.fp_now = true;
    } else if (!ev && control_cell_get(&control_cell_player_pos, lp)) {
        // third person: the game's camera moved toward Link's head or away from it (BetterVR's ThirdPlayerDistance;
        // the Options page's Third-person distance, % of the game's own), along the line between them
        int k = vr_opt(VR_OPT_THIRD_DISTANCE);
        if (k > 0 && k != 100) {
            vr_vec3 head = vr_v3((float)lp[0], (float)lp[1] + 1.5f, (float)lp[2]);
            geye = vr_v3_add(head, vr_v3_scale(vr_v3_sub(geye, head), k / 100.0f));
        }
    }
    // roomscale (BetterVR's): Link walked toward the head since the last camera: the neutral head follows him, so the
    // eye stays where the player's head is in the world while his body catches up. Only his step toward the head, and
    // no farther than it: a slope, a current or a push moving him elsewhere moves the eye with him (the eye pinned in
    // the world while he slid away left the view off his body, and the stick kept walking him)
    if (c.fp_now && c.rs.driving && c.rs.have_lp && c.anchor_ok) {
        float dx = (float)(lp[0] - c.rs.lp[0]), dz = (float)(lp[2] - c.rs.lp[2]);
        float d = sqrtf(c.rs.gx * c.rs.gx + c.rs.gz * c.rs.gz);
        if (d > 1e-3f && dx * dx + dz * dz < 1.0f) {
            float s = (dx * c.rs.gx + dz * c.rs.gz) / d;
            s = s < 0 ? 0 : s > d ? d : s;
            vr_vec3 dl = vr_v3(c.rs.gx / d * s, 0, c.rs.gz / d * s);
            c.neutral.p = vr_v3_add(c.neutral.p,
                                    vr_v3_scale(vr_quat_rotate(vr_quat_conj(vr_anchor_rotation(c.anchor)), dl),
                                                1.0f / c.anchor.scale));
        }
    }
    // the anchor that puts the neutral head at the game's eye, looking the game's way; the eye's pose from there
    vr_anchor a = vr_anchor_recenter(c.neutral, geye, yaw_used, world_scale);
    c.anchor = a, c.anchor_ok = true;
    c.anchor_turn_yaw = yaw_used, c.anchor_yaw_ok = true, c.anchor_fp_was = fp;
    c.anchor_lp_ok = c.fp_now;
    if (c.fp_now) memcpy(c.anchor_lp, lp, sizeof c.anchor_lp);
    // Stereo: the two eyes of a pair are drawn in two game frames, half a step apart, and the anchor moves between them
    // (the turn stick's turn, the game camera's own swing, Link's step, the eye's place ahead of his head). The headset
    // only knows each eye's tracked pose, so a right eye drawn from a moved anchor was shown as if from the left one's
    // place: while turning or moving the eyes disagreed (one eye "desynchronised"), and a step forward between them read
    // as nearer things - crossed eyes (2026-11-24, Steam Frame report). The right frame draws with the left frame's
    // anchor: both eyes see the world from one place, as a real pair of eyes. What moves in the world itself (Link
    // included) still shows its half step. The anchor the rest uses (c.anchor: the hands, roomscale) stays the fresh one.
    // SAVAGEWIND_VR_PAIR_ANCHOR=0: each eye its own anchor, as before
    {
        static int same = -1;
        if (same < 0) {
            same = !(getenv("SAVAGEWIND_VR_PAIR_ANCHOR") && *getenv("SAVAGEWIND_VR_PAIR_ANCHOR") == '0');
            if (!same) LOG("[vr] stereo: each eye drawn with its own frame's anchor (SAVAGEWIND_VR_PAIR_ANCHOR=0)\n");
        }
        uint64_t id = eye >= 0 ? vr_pair_id(&c.pair) : 0;
        if (eye == 0) {
            c.pair_anchor = a, c.pair_anchor_id = id, c.pair_anchor_fp = c.fp_now;
            c.pair_anchor_lp_ok = c.fp_now;
            if (c.fp_now) memcpy(c.pair_anchor_lp, lp, sizeof c.pair_anchor_lp);
        } else if (eye == 1 && same && !recentred && id && c.pair_anchor_id == id && c.pair_anchor_fp == c.fp_now) {
            a = c.pair_anchor;
            if (c.pair_anchor_counted != id) c.pair_anchor_counted = id, c.pair_anchors++;
        }
    }
    // (the `vr` command: frames whose view turned against the anchor their skeleton was placed with - the hands placed
    // in this frame before its camera, with the last frame's anchor; placed after it they take this one)
    if (first_cam && skel_anchor_ok && c.fp_now && eye >= 0) {
        c.anchor_frames++;
        if (c.skel_before_cam && fabsf(remainderf(a.yaw - skel_anchor.yaw, 2 * VR_PI)) > 1e-4f) c.anchor_mismatch++;
        if (c.skel_before_cam) c.skel_first_frames++;
    }
    vr_lookat_from_view(a, view, f, f + 3, f + 6);
    roomscale(c.anchor, head, geye, ev, lp, f);
    memcpy(c.view_out, f, sizeof c.view_out);
    // (debugging the arm rig) SAVAGEWIND_VR_DEBUG_CAM="<back> <right> <up>" (m): the picture drawn from there, looking at
    // the eye, while the head, the hands and Link's arms stay as in first person - the whole arm seen from outside
    {
        static int dbg = -1;
        static float db, dr, du;
        if (dbg < 0) dbg = getenv("SAVAGEWIND_VR_DEBUG_CAM") && sscanf(getenv("SAVAGEWIND_VR_DEBUG_CAM"), "%f %f %f", &db, &dr, &du) == 3;
        if (dbg && c.fp_now) {
            vr_vec3 e = vr_v3(f[0], f[1], f[2]), fw = vr_v3(f[3] - f[0], 0, f[5] - f[2]);
            float fl = vr_v3_len(fw);
            if (fl > 1e-4f) {
                fw = vr_v3_scale(fw, 1 / fl);
                vr_vec3 rt = vr_v3_cross(fw, vr_v3(0, 1, 0));   // (the side; its sign as the game's frame has it)
                vr_vec3 p = vr_v3_add(vr_v3_add(vr_v3_sub(e, vr_v3_scale(fw, db)), vr_v3_scale(rt, dr)), vr_v3(0, du, 0));
                f[0] = p.x, f[1] = p.y, f[2] = p.z;
                f[3] = e.x, f[4] = e.y - 0.25f, f[5] = e.z;
                f[6] = 0, f[7] = 1, f[8] = 0;
            }
        }
    }
    memcpy(c.out, f, sizeof c.out);
    c.out_ok = true;
    memmove(c.outs[1], c.outs[0], sizeof c.outs - sizeof c.outs[0]);
    memcpy(c.outs[0], f, sizeof c.outs[0]);
    if (c.outs_n < 8) c.outs_n++;
    { extern void vr_sync_note_out(const float *); vr_sync_note_out(f); }
    // the frustum. In stereo, the eye's own field, off-centre as it is (narrower on the nose's side: SAVAGEWIND_VR_
    // ASYMMETRIC=0 for the symmetric one), widened to the window's shape about its centre: the game's projection gets
    // its half-size and its centre as an offset (vr_persp, vr_persp_offset). In mono (one picture for both eyes): both
    // fields together, made symmetric.
    static int asym = -1;
    if (asym < 0) asym = !(getenv("SAVAGEWIND_VR_ASYMMETRIC") && *getenv("SAVAGEWIND_VR_ASYMMETRIC") == '0');
    float tx = 0, ty = 0, cx = 0, cy = 0;
    // Synchronous stereo / multiview: one game frame draws both eyes, and the game culls with its own camera - the left
    // eye's. The right eye's outer side, beyond the left eye's field, lost what the game left out there (holes in trees,
    // the sky through rock along the right eye's right edge, 2026-11-25). Both eyes take the field of both together
    // (the left eye's left edge, the right eye's right edge, the higher top, the lower bottom), submitted as drawn: the
    // game culls for both. SAVAGEWIND_VR_SYNC_UNION_FOV=0: each eye its own field
    static int union_fov = -1;
    if (union_fov < 0) union_fov = !(getenv("SAVAGEWIND_VR_SYNC_UNION_FOV") && *getenv("SAVAGEWIND_VR_SYNC_UNION_FOV") == '0');
    bool uni = union_fov && eye == 0 && vr_sync_mode();
    XrFovf fov_u = v[0].fov;
    if (uni) {
        fov_u.angleLeft = fminf(v[0].fov.angleLeft, v[1].fov.angleLeft);
        fov_u.angleRight = fmaxf(v[0].fov.angleRight, v[1].fov.angleRight);
        fov_u.angleUp = fmaxf(v[0].fov.angleUp, v[1].fov.angleUp);
        fov_u.angleDown = fminf(v[0].fov.angleDown, v[1].fov.angleDown);
    }
    if (eye >= 0 && asym && !vr_rune_aiming()) {   // (aiming a rune: symmetric, its axis the picture's centre)
        const XrFovf *fe = uni ? &fov_u : &v[eye].fov;
        float l = tanf(fe->angleLeft), r = tanf(fe->angleRight);
        float u = tanf(fe->angleUp), d = tanf(fe->angleDown);
        tx = (r - l) / 2, ty = (u - d) / 2, cx = (r + l) / 2, cy = (u + d) / 2;
    } else {
        for (int i = 0; i < 2; i++) {
            if (eye >= 0 && i != eye) continue;
            tx = fmaxf(tx, fmaxf(tanf(v[i].fov.angleRight), tanf(-v[i].fov.angleLeft)));
            ty = fmaxf(ty, fmaxf(tanf(v[i].fov.angleUp), tanf(-v[i].fov.angleDown)));
        }
    }
    if (tx > 0.05f && ty > 0.05f) {
        float wa = c.window_aspect;
        if (tx / ty < wa) tx = ty * wa;
        else ty = tx / wa;
        c.tan_x = tx, c.tan_y = ty, c.off_x = cx, c.off_y = cy;
        c.cur = (vr_view){.world = true,
                          .eye = eye,
                          .pair_id = eye >= 0 ? vr_pair_id(&c.pair) : 0,
                          .pose = to_xr(tracked),
                          .located = located_t,
                          .fov = {atanf(cx - tx), atanf(cx + tx), atanf(cy + ty), atanf(cy - ty)}};
        // synchronous stereo: the right eye of this same frame - its view from the same anchor (and the same moves
        // after it: roomscale, the debug camera), its own field - for the translator's second pass (vr_sync.c)
        if (eye == 0 && vr_sync_mode() && asym && !vr_rune_aiming()) {
            vr_pose view1 = from_xr(v[1].pose);
            if (ipd_scale != 1.0f) view1.p = vr_v3_add(head.p, vr_v3_scale(vr_v3_sub(view1.p, head.p), ipd_scale));
            float g0[9], g1[9], f1[9];
            vr_lookat_from_view(a, view, g0, g0 + 3, g0 + 6);
            vr_lookat_from_view(a, view1, g1, g1 + 3, g1 + 6);
            for (int i = 0; i < 6; i++) f1[i] = g1[i] + (f[i] - g0[i]);
            for (int i = 6; i < 9; i++) f1[i] = g1[i];
            const XrFovf *f1v = uni ? &fov_u : &v[1].fov;
            float l = tanf(f1v->angleLeft), r = tanf(f1v->angleRight);
            float u = tanf(f1v->angleUp), d = tanf(f1v->angleDown);
            float tx1 = (r - l) / 2, ty1 = (u - d) / 2, cx1 = (r + l) / 2, cy1 = (u + d) / 2;
            float wa = c.window_aspect;
            if (tx1 / ty1 < wa) tx1 = ty1 * wa;
            else ty1 = tx1 / wa;
            c.sync_cur1 = (vr_view){.world = true, .eye = 1, .pair_id = c.cur.pair_id, .pose = v[1].pose,
                                    .located = located_t,
                                    .fov = {atanf(cx1 - tx1), atanf(cx1 + tx1), atanf(cy1 + ty1), atanf(cy1 - ty1)}};
            c.sync_cur1_ok = true;
            extern void vr_sync_frame(uint64_t pair_id, const float *f0, const float *f1, const float t0[4], const float t1[4]);
            float t0[4] = {tx, ty, cx, cy}, t1[4] = {tx1, ty1, cx1, cy1};
            vr_sync_frame(c.cur.pair_id, f, f1, t0, t1);
        }
    }
    c.updates++;
    // (debugging) SAVAGEWIND_VR_LOG_CAMERA=<n>: every n-th update, the eye, the game's camera and the view made
    static int log_every = -1;
    if (log_every < 0) log_every = getenv("SAVAGEWIND_VR_LOG_CAMERA") ? atoi(getenv("SAVAGEWIND_VR_LOG_CAMERA")) : 0;
    if (log_every > 0 && c.updates % (uint64_t)log_every == 0) {
        vr_vec3 od = vr_v3(f[3] - f[0], f[4] - f[1], f[5] - f[2]);
        LOG("[vr-cam] eye %d head %.2f %.2f %.2f heading %.1f | game eye %.1f %.1f %.1f yaw %.1f | view yaw %.1f pitch "
            "%.1f eye %.2f %.2f %.2f\n", eye, head.p.x, head.p.y, head.p.z, vr_quat_heading(head.q) * 57.29578f, geye.x,
            geye.y, geye.z, yaw * 57.29578f, vr_game_yaw_of(od) * 57.29578f, vr_game_pitch_of(od) * 57.29578f, f[0],
            f[1], f[2]);
    }
    pthread_mutex_unlock(&lock);
}

// rt_persp, the world camera (far plane over 10000): its tan(fovy / 2) and aspect in play
// output_scale.c rt_persp_offset, the world camera's: its projection moved so that its centre is the frustum's
// (vr_camera_lookat), as fractions of the image's width and height (NDC is 2 wide). SAVAGEWIND_VR_OFFSET_SIGN=-1 flips
// them (a check of the sign).
bool vr_persp_offset(float *x, float *y) {
    if (!vr_world_view()) return false;
    static float sign = 0;
    if (!sign) sign = getenv("SAVAGEWIND_VR_OFFSET_SIGN") && atoi(getenv("SAVAGEWIND_VR_OFFSET_SIGN")) < 0 ? -1.0f : 1.0f;
    pthread_mutex_lock(&lock);
    bool ok = c.cur.world && c.tan_x > 0 && c.tan_y > 0;
    if (ok) *x += sign * c.off_x / (2 * c.tan_x), *y += sign * c.off_y / (2 * c.tan_y);
    pthread_mutex_unlock(&lock);
    return ok;
}

// The UI's world-to-screen (runtime/hle/arrow.c rt_ui_project): name labels, markers and gauges are placed for the
// eye's own picture (xy: -1..1 across this frame's frustum), but the headset shows the HUD on a smaller panel ahead (1.6
// m wide at 1.4 m: about 30 degrees each side, the eye's field 45 to 50), so a marker over an enemy 30 degrees off
// showed at 17. It goes where the eye's line to the spot crosses the panel, as BetterVR's hook_PlaceScreenMarker
// (a head-locked panel: its centre 5 cm under the line of sight). SAVAGEWIND_VR_UI_PROJECT=0: as the game has it
//
// The stamina wheel (its caller, 1.9.0 main+0xe1ad80, asks for a spot 1.8 m above Link's feet): in first person that is
// in his head, by the eye, and the wheel went anywhere. It stays under the hearts there, as BetterVR's
// hook_FixStaminaGaugeScreenPosition (layout -482, +170)
#define VR_UI_STAMINA 0x7000e1ad80ull
void vr_ui_project(float *xy, uint64_t caller, const float *half) {
    static int on = -1;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_UI_PROJECT") && *getenv("SAVAGEWIND_VR_UI_PROJECT") == '0');
    if (!on || !vr_world_view()) return;
    if (caller == VR_UI_STAMINA) {
        pthread_mutex_lock(&lock);
        bool fp = c.fp_now;
        if (fp) c.ui_stamina++;
        pthread_mutex_unlock(&lock);
        if (fp && half && half[0] > 1 && half[1] > 1) xy[0] = -482.0f / half[0], xy[1] = 170.0f / half[1];
        return;
    }
    extern bool vr_hud_panel(float *dist, float *w, float *aspect);
    float dist, w, aspect;
    if (!vr_hud_panel(&dist, &w, &aspect)) return;
    pthread_mutex_lock(&lock);
    bool ok = c.cur.world && c.tan_x > 0 && c.tan_y > 0;
    float tx = c.off_x + xy[0] * c.tan_x, ty = c.off_y + xy[1] * c.tan_y;
    c.ui_projected++;
    pthread_mutex_unlock(&lock);
    if (!ok) return;
    float x = dist * tx / (w / 2), y = (dist * ty + 0.05f) / (w * aspect / 2);
    if (isfinite(x) && isfinite(y)) xy[0] = x, xy[1] = y;
}
uint64_t vr_ui_projected(void) { return c.ui_projected; }
uint64_t vr_ui_stamina(void) { return c.ui_stamina; }

bool vr_persp(float *tan_fovy, float *aspect) {
    if (!vr_world_view()) return false;
    pthread_mutex_lock(&lock);
    bool ok = c.cur.world;
    trace("persp", c.cur.eye, ok);
    if (ok) *tan_fovy = c.tan_y, *aspect = c.tan_x / c.tan_y;
    pthread_mutex_unlock(&lock);
    return ok;
}

// nvnQueuePresentTexture (the game's thread): this frame is done, as what it was drawn
void vr_present_mark(void) {
    if (!vr_enabled()) return;
    pthread_mutex_lock(&lock);
    if (c.q_count == QUEUE) {  // (never in practice: the queue thread is a frame or two behind)
        c.q_head = (c.q_head + 1) % QUEUE;
        c.q_count--;
    }
    vr_view v = c.cur;
    v.world = v.world && c.cam_set && vr_world_view();  // (a frame without a camera of its own: the screen)
    if (stereo_on() && vr_sync_mode() && c.q_count >= QUEUE - 1) {   // (no room for a whole pair: the oldest pair goes)
        int drop = c.q_count >= 2 && c.q[c.q_head].eye == 0 && c.q[(c.q_head + 1) % QUEUE].eye == 1 ? 2 : 1;
        c.q_head = (c.q_head + drop) % QUEUE;
        c.q_count -= drop;
    }
    bool sync_pair = stereo_on() && vr_sync_mode() && v.world && v.eye == 0 && c.sync_cur1_ok &&
                     c.sync_cur1.pair_id == v.pair_id;
    if (stereo_on() && vr_sync_mode() && v.world && !sync_pair) v.eye = -1;   // (no right eye made: one picture for both)
    c.q[(c.q_head + c.q_count++) % QUEUE] = v;
    trace("present", v.eye, v.world);
    if (sync_pair) {   // (synchronous stereo: the right eye of the same frame, the translator's replay presents it)
        if (c.q_count == QUEUE) c.q_head = (c.q_head + 1) % QUEUE, c.q_count--;
        c.q[(c.q_head + c.q_count++) % QUEUE] = c.sync_cur1;
    }
    c.sync_cur1_ok = false;
    // the next frame: the other eye after a left one, else the left
    if (stereo_on()) vr_pair_present(&c.pair, v.world);
    if (sync_pair) {   // (the pair is whole: the next frame is a left one again)
        c.pair.open = true, c.pair.eye = 1;
        vr_pair_present(&c.pair, true);
    }
    c.cam_set = false;
    c.skel_before_cam = false;
    pthread_mutex_unlock(&lock);
}

static struct { bool world; int eye; } taken_last;   // (the last frame vr_present_take gave: vr_translating_eye)

// the present on the queue thread (device.c backend_present): what the frame being presented is
static uint64_t q_orphans;   // (one game frame's presents and its queue entries out of step: logged)
bool vr_present_take(vr_view *out) {
    pthread_mutex_lock(&lock);
    // Synchronous stereo / multiview: a game frame is one present of the game and two entries here (its left eye,
    // then its right eye, pushed together by vr_present_mark); the translator takes the right one only with the left
    // one's second present (vr_present_take_right). An entry of a right eye at the head is an orphan - its left one was
    // dropped (the queue full) or its second present never came - and taking it as a frame's first present put every
    // frame from then on one entry out of step: the left eye's swapchain got the right eye's picture now and then and
    // the eyes swapped poses (a Quest 3 player: the left eye "full of movement", 2026-11-25). Dropped instead
    while (c.q_count > 0 && vr_sync_mode() && c.q[c.q_head].world && c.q[c.q_head].eye == 1) {
        c.q_head = (c.q_head + 1) % QUEUE;
        c.q_count--;
        if (q_orphans++ < 20) LOG("[vr] present queue: a right eye without its left one dropped (%llu so far)\n",
                                  (unsigned long long)q_orphans);
    }
    bool ok = c.q_count > 0;
    if (ok) {
        *out = c.q[c.q_head];
        c.q_head = (c.q_head + 1) % QUEUE;
        c.q_count--;
        taken_last.world = out->world, taken_last.eye = out->eye;
    }
    pthread_mutex_unlock(&lock);
    if (!ok) memset(out, 0, sizeof *out);
    return ok;
}

// The second present of a game frame (synchronous stereo / multiview): its right eye's entry, only when the head is
// exactly that - the right eye of the same pair; else nothing is taken and there is no second present
bool vr_present_take_right(vr_view *out, uint64_t pair_id) {
    pthread_mutex_lock(&lock);
    bool ok = c.q_count > 0 && c.q[c.q_head].world && c.q[c.q_head].eye == 1 && pair_id && c.q[c.q_head].pair_id == pair_id;
    if (ok) {
        *out = c.q[c.q_head];
        c.q_head = (c.q_head + 1) % QUEUE;
        c.q_count--;
        taken_last.world = out->world, taken_last.eye = out->eye;
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

// The eye of the frame the translating thread is on (nvn/vulkan: its passes and composite). The game's thread runs
// ahead of it, so vr_frame_eye - the frame the game is setting its cameras for - was often the next frame's there: the
// right eye's shadow passes left out (SAVAGEWIND_VR_SHADOWS_ONCE) were now and then the left one's, which flickered.
// The frames are marked in order at their present (vr_present_mark) and taken in order once translated
// (vr_present_take): the one being translated is the oldest not taken. -1: not known yet (or not in stereo play).
// Not marked yet (the translation of a frame can reach its composite before the game presents it): the eye after the
// last one taken - the right after a left, else a pair's left (vr_pair_present's order).
int vr_translating_eye(void) {
    if (!stereo_on() || !vr_world_view()) return -1;
    pthread_mutex_lock(&lock);
    int eye = c.q_count > 0 ? (c.q[c.q_head].world ? c.q[c.q_head].eye : -1) : taken_last.world && taken_last.eye == 0 ? 1 : 0;
    pthread_mutex_unlock(&lock);
    return eye;
}

uint64_t vr_camera_updates(void) { return c.updates; }
uint64_t vr_stereo_pairs(void) { return c.pair.pairs; }
uint64_t vr_pair_anchors(void) { return c.pair_anchors; }
void vr_anchor_checks(uint64_t *frames, uint64_t *mismatch) { *frames = c.anchor_frames, *mismatch = c.anchor_mismatch; }
uint64_t vr_skel_first_frames(void) { return c.skel_first_frames; }

// vr_skel.c: the last camera was set in first person (the eye in Link's head)
bool vr_first_person_now(void) { return c.fp_now; }
float vr_eye_height_used(void) { return c.eye_used; }
// the `vr` command: the world eye last written (the view's place)
bool vr_eye_world(float out[3]) {
    if (!c.out_ok) return false;
    out[0] = c.view_out[0], out[1] = c.view_out[1], out[2] = c.view_out[2];
    return true;
}
float vr_climb_eye(void) { return c.climb_eye; }

// vr_skel.c: a tracking-space pose in the game's world, with the anchor the last camera used (false before one)
// vr_skel.c: the way the head looks in the game's world, flat (radians, the game's yaw: x = sin, z = cos), from the
// last camera - first person only
uint64_t vr_kept_turns(void) { return c.kept_turns; }

bool vr_view_heading(float *yaw) {
    pthread_mutex_lock(&lock);
    bool ok = c.out_ok && c.fp_now;
    if (ok) {
        float dx = c.view_out[3] - c.view_out[0], dz = c.view_out[5] - c.view_out[2];
        ok = dx * dx + dz * dz > 1e-8f;
        if (ok) *yaw = atan2f(dx, dz);
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

// The anchor is the last camera's; Link's skeleton and the actors are calculated before this frame's camera, after
// his move, so on the move his arms (and what they hold) were placed one step behind the eye - at a run, on a horse,
// the hands slid away from where the controllers are. The anchor is carried along by how far Link has moved since
// that camera (BetterVR's GetFreshCameraReferenceMtx). SAVAGEWIND_VR_FRESH_ANCHOR=0: the last camera's as it was
bool vr_tracking_to_world(const vr_pose *in, vr_pose *out) {
    static int fresh = -1;
    if (fresh < 0) fresh = !(getenv("SAVAGEWIND_VR_FRESH_ANCHOR") && *getenv("SAVAGEWIND_VR_FRESH_ANCHOR") == '0');
    double lp[3];
    bool now = fresh && control_cell_get(&control_cell_player_pos, lp);
    static int same = -1;
    if (same < 0) same = !(getenv("SAVAGEWIND_VR_PAIR_ANCHOR") && *getenv("SAVAGEWIND_VR_PAIR_ANCHOR") == '0');
    pthread_mutex_lock(&lock);
    bool ok = c.anchor_ok && c.fp_now;
    if (ok && !c.cam_set) c.skel_before_cam = true;
    if (ok) {
        // A pair's right frame is drawn with its left frame's anchor (vr_camera_set): its skeleton too. The game can
        // calculate Link's skeleton after the frame's camera, when c.anchor is already the right frame's fresh one -
        // turning with the stick, his hands and arms were drawn in the right eye turned by a game frame of the turn
        // against its view (fake_xr: 2 degrees, the hands at 0.7 m instead of 0.54 in depth; a tester saw his hands
        // and body cross-eyed, 2026-11-25)
        int eye = c.pair.open ? c.pair.eye : c.pair.next;
        bool pair = same && eye == 1 && c.pair.id && c.pair_anchor_id == c.pair.id && c.pair_anchor_fp;
        const vr_anchor *an = pair ? &c.pair_anchor : &c.anchor;
        const double *alp = pair ? c.pair_anchor_lp : c.anchor_lp;
        bool alp_ok = pair ? c.pair_anchor_lp_ok : c.anchor_lp_ok;
        *out = vr_xr_to_game_pose(*an, *in);
        // (Link's step since the anchor, carried to the hands so they do not trail his body - not in a pair's right
        // frame: its view is drawn from the left frame's place, and carried, the hands went half a step ahead of it
        // there while walking, somewhere else in each eye: "crosseyed hands when moving", 26.2's tester;
        // test_vr_stereo_sync.py --walk, 10-12 samples apart before)
        if (now && alp_ok && !pair) {
            vr_vec3 d = vr_v3((float)(lp[0] - alp[0]), (float)(lp[1] - alp[1]), (float)(lp[2] - alp[2]));
            if (vr_v3_len(d) < 2.0f) out->p = vr_v3_add(out->p, d);   // (a warp or a reload: no carry)
        }
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

// xr_vulkan.c: the heading the head had when it was centred (the body's way, for a HUD that does not follow the gaze)
bool vr_neutral_heading(float *heading) {
    pthread_mutex_lock(&lock);
    bool ok = c.have_neutral;
    if (ok) *heading = vr_quat_heading(c.neutral.q);
    pthread_mutex_unlock(&lock);
    return ok;
}

// vr_skel.c (the game's thread, at a model's skeleton): the eye of the frame in progress and its pair's id,
// without choosing it (a frame's eye is chosen by its clock step, before its updates: vr_pair.h); -1 out of stereo play
int vr_frame_eye_peek(uint64_t *pair) {
    if (!vr_world_view() || !stereo_on()) return -1;
    pthread_mutex_lock(&lock);
    int eye = c.pair.open ? c.pair.eye : c.pair.next;
    *pair = c.pair.open || eye ? c.pair.id : c.pair.serial + 1;
    pthread_mutex_unlock(&lock);
    return eye;
}

// exec.c (the game's thread, translating the frame's commands): the eye the frame in progress is drawn for, in play in
// stereo (0 left, 1 right; -1 otherwise). Fixed by the frame's first camera (vr_pair_camera), before its passes.
int vr_frame_eye(void) {
    if (!vr_world_view() || !stereo_on()) return -1;
    pthread_mutex_lock(&lock);
    int eye = c.cam_set ? vr_pair_eye(&c.pair) : -1;
    pthread_mutex_unlock(&lock);
    return eye;
}

// exec.c (the game's thread, at the runes' target pick): the point of the frame in progress's picture the rune
// aims at, as the pick's coordinates (u 0..1 left to right, v 0..1 top to bottom). The game picks Stasis', Magnesis'
// and Cryonis' target by reading its G-buffer at the picture's centre (a 1x1 draw, its constants' first two floats
// 0.5 0.5). The point is the eye's own axis (BetterVR's patch_CTRL_FixScreenChecks); while a rune is aimed the eye's
// field is made symmetric (vr_rune_aiming), so that is the picture's centre - where the game's reticle is drawn.
//
// The runes are no longer aimed with the hand (2026-11-20): Magnesis also casts a ray of its own on the CPU through
// the picture's centre (checked against fake_xr: pointing the hand alone, or the head with the eye's off-centre field,
// it took nothing; with the head and a symmetric field it lifted the weapon), so only a point on that ray works, and
// the hand's ray drawn as yellow dots pointed elsewhere than the reticle. The option (vr_runes_by_hand) is ignored.
// false (the game's centre) out of the headset's view of play.
static _Atomic uint64_t rune_pick_ns;   // (the last pick: plat_now_ns)
bool vr_rune_hand_debug(void) {
    static int on = -1;
    if (on < 0) on = getenv("SAVAGEWIND_VR_RUNE_HAND_DEBUG") && *getenv("SAVAGEWIND_VR_RUNE_HAND_DEBUG") == '1';
    return on;
}
bool vr_rune_aim_uv(float *u, float *v) {
    if (!vr_world_view()) return false;
    pthread_mutex_lock(&lock);
    vr_view view = c.cur;
    pthread_mutex_unlock(&lock);
    if (!view.world) return false;
    atomic_store(&rune_pick_ns, plat_now_ns());
    vr_pose eye = from_xr(view.pose), from = eye;
    {   // (research) SAVAGEWIND_VR_RUNE_HAND_DEBUG=1: the main hand's ray instead, Link's arms kept on it
        extern bool vr_hand_pose(int hand, vr_pose *out);
        vr_pose hand;
        if (vr_rune_hand_debug() && vr_hand_pose(vr_opt(VR_OPT_LEFT_HANDED) == 1 ? 0 : 1, &hand)) from = hand;
    }
    float x, y;
    if (!vr_ray_picture_uv(eye, from.p, vr_quat_rotate(from.q, vr_v3(0, 0, -1)), 30.0f, view.fov.angleLeft,
                           view.fov.angleRight, view.fov.angleUp, view.fov.angleDown, &x, &y))
        return false;
    *u = x < 0.02f ? 0.02f : x > 0.98f ? 0.98f : x;
    *v = y < 0.02f ? 0.02f : y > 0.98f ? 0.98f : y;
    return true;
}

// a rune is being aimed: its target pick was drawn in the last 0.3 s. Also vr_skel.c: Link's arms are left to the
// game's animation then - Magnesis took nothing with his right hand (the Sheikah Slate's) moved to the controller
// pointing elsewhere than the view (fake_xr: the same head aim lifted the weapon with the arms not moved)
static bool vr_rune_aiming(void) {
    uint64_t last = atomic_load(&rune_pick_ns);
    return last && plat_now_ns() - last < 300000000ull;
}
bool vr_rune_aiming_now(void) { return vr_world_view() && vr_rune_aiming() && !vr_rune_hand_debug(); }

// output_scale.c (TAA's jitter): in play in stereo, frames in pairs
bool vr_stereo_now(void) { return vr_world_view() && stereo_on(); }
// xr_vulkan.c: stereo is the mode (whether or not this frame is play)
bool vr_stereo_mode(void) { return stereo_on(); }

// runtime/hle/actor_jobs.c: in stereo play an actor's job-push skip (counted in game frames) lasts twice the frames, as
// long as flat (SAVAGEWIND_VR_JOB_SKIP=0: the game's count)
bool vr_job_skip_doubled(void) {
    static int on = -1;
    if (on < 0) on = !(getenv("SAVAGEWIND_VR_JOB_SKIP") && *getenv("SAVAGEWIND_VR_JOB_SKIP") == '0');
    if (!on || !vr_stereo_now()) return false;
    static _Atomic uint64_t n;
    if (n++ == 0) LOG("[vr] stereo: actors' job-push skips doubled (two game frames a moment)\n");
    return true;
}

// exec.c (the game's thread, translating the frame's commands): the field of the eye being drawn, in tangents - its
// centre (cx, cy: the off-centre eye's) and half-size (tx, ty). false out of stereo play's world view, and in synchronous
// stereo / multiview (one game frame for both eyes)
bool vr_eye_field(float *cx, float *cy, float *tx, float *ty) {
    if (!vr_world_view() || !stereo_on() || vr_sync_mode()) return false;
    pthread_mutex_lock(&lock);
    bool ok = c.cur.world && c.tan_x > 0 && c.tan_y > 0;
    if (ok) *cx = c.off_x, *cy = c.off_y, *tx = c.tan_x, *ty = c.tan_y;
    pthread_mutex_unlock(&lock);
    return ok;
}

// mvec.c (TAA, the game's thread at the frame's composite): the offset vr_persp_offset gives the frame being drawn
// (fractions of the picture), to tell it from the jitter
bool vr_offset_now(float *x, float *y) {
    *x = *y = 0;
    if (!vr_world_view()) return false;
    pthread_mutex_lock(&lock);
    bool ok = c.cur.world && c.tan_x > 0 && c.tan_y > 0;
    if (ok) *x = c.off_x / (2 * c.tan_x), *y = c.off_y / (2 * c.tan_y);
    pthread_mutex_unlock(&lock);
    return ok;
}
