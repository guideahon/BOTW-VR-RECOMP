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
// the first and nearly stops in the second, as it does for the free camera: both eyes see the same moment, and the
// world keeps its speed. The headset's frame is made once a pair (xr_vulkan.c), so at 90 Hz the game runs at 180 fps.
//
// Which eye, pose and frustum a frame was drawn with travels with it: nvnQueuePresentTexture (the game's thread, at the
// end of its frame: vr_present_mark) queues it, the present on the queue thread (vr_present_take) takes it in the same
// order, and the headset is told exactly what each picture is.
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "../framepace.h"
#include "../game_sites.h"
#include "../control_cells.h"

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
    float window_aspect;   // the window image's width / height
    float out[9];          // the last eye / at / up written
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
    bool fp_now;           // the last camera was set in first person
} c = {.stereo = -1, .window_aspect = 1, .first = -1};

static float env_f(const char *name, float def, float lo, float hi) {
    const char *e = getenv(name);
    float v = e ? (float)atof(e) : def;
    return v >= lo && v <= hi ? v : def;
}

// SAVAGEWIND_VR_VIEW=first (the default): the eye is in Link's head, as in BetterVR; third: on the game's own camera
// behind him. SAVAGEWIND_VR_EYE_HEIGHT (1.55 m above his feet) and SAVAGEWIND_VR_EYE_FORWARD (0.18 m ahead, out of his
// head's model) place it.
static bool first_person(void) {
    if (c.first < 0) {
        const char *e = getenv("SAVAGEWIND_VR_VIEW");
        c.first = !(e && !strcmp(e, "third"));
        c.eye_height = env_f("SAVAGEWIND_VR_EYE_HEIGHT", 1.55f, 0.3f, 3.0f);
        c.forward = env_f("SAVAGEWIND_VR_EYE_FORWARD", 0.18f, 0.0f, 1.0f);
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
        LOG("[vr-trace] %s eye %d world %d\n", what, eye, world);
    }
}

static bool stereo_on(void) {
    if (c.stereo < 0) {
        const char *e = getenv("SAVAGEWIND_VR_STEREO");
        c.stereo = !(e && *e == '0');
        LOG("[vr] %s\n", c.stereo ? "stereo: a frame per eye, the game's clock once a pair"
                                  : "mono: one picture for both eyes (SAVAGEWIND_VR_STEREO=0)");
    }
    return c.stereo;
}

bool vr_world_view(void) {
    if (!vr_enabled() || !vr_session_running()) return false;
    if (framepace_in_event() || framepace_title_ui() || framepace_loading_ui() || framepace_map_ui()) return false;
    if (g_site.pause_state && LD32(g_site.pause_state) != 0) return false;
    return true;
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
    float step = vr_pair_step(&c.pair, ratio);  // (the pair's time in its first frame, nearly none in its second)
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
static bool locate(XrView v[2]) {
    XrTime t = vr_next_display_time();
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

static bool near9(const float *a, const float *b) {
    for (int i = 0; i < 9; i++)
        if (fabsf(a[i] - b[i]) > 1e-4f) return false;
    return true;
}

// rt_lookat, for a view camera (eye off the origin, up near +y): f = eye[3], at[3], up[3]
void vr_camera_lookat(float *f) {
    if (!vr_world_view()) {
        c.out_ok = false;
        return;
    }
    pthread_mutex_lock(&lock);
    if (!(c.out_ok && near9(f, c.out))) memcpy(c.game, f, sizeof c.game);  // (else: ours, the game's is kept)
    XrView v[2];
    if (!locate(v)) {
        pthread_mutex_unlock(&lock);
        return;
    }
    // the head: halfway between the eyes, turned as the first one (the runtime turns both alike)
    vr_pose head = from_xr(v[0].pose);
    head.p = vr_v3_scale(vr_v3_add(head.p, from_xr(v[1].pose).p), 0.5f);
    if (!c.have_neutral || c.recenter) {
        c.neutral = head;
        c.have_neutral = true;
        c.recenter = false;
        LOG("[vr] recentred: head at %.2f %.2f %.2f, heading %.1f deg\n", head.p.x, head.p.y, head.p.z,
            vr_quat_heading(head.q) * 57.29578f);
    }
    int eye = stereo_on() ? vr_pair_camera(&c.pair) : -1;
    c.cam_set = true;
    trace("lookat", eye, c.cur.world);
    vr_pose view = eye >= 0 ? from_xr(v[eye].pose) : head;
    // the game's camera: where it is and the way it looks along the ground
    const float *gf = c.game;
    vr_vec3 geye = vr_v3(gf[0], gf[1], gf[2]);
    float yaw = vr_game_yaw_of(vr_v3(gf[3] - gf[0], gf[4] - gf[1], gf[5] - gf[2]));
    // first person: the neutral head goes to Link's eyes, a little ahead of his head, facing the game camera's way
    double lp[3];
    c.fp_now = false;
    if (first_person() && control_cell_get(&control_cell_player_pos, lp)) {
        geye = vr_v3((float)lp[0] + sinf(yaw + c.turn) * c.forward, (float)lp[1] + c.eye_height,
                     (float)lp[2] + cosf(yaw + c.turn) * c.forward);
        c.fp_now = true;
    }
    // the anchor that puts the neutral head at the game's eye, looking the game's way; the eye's pose from there
    vr_anchor a = vr_anchor_recenter(c.neutral, geye, yaw + c.turn, 1.0f);
    vr_lookat_from_view(a, view, f, f + 3, f + 6);
    memcpy(c.out, f, sizeof c.out);
    c.out_ok = true;
    // the frustum: the eye's field (both, in mono) made symmetric, widened to the window's shape
    float tx = 0, ty = 0;
    for (int i = 0; i < 2; i++) {
        if (eye >= 0 && i != eye) continue;
        tx = fmaxf(tx, fmaxf(tanf(v[i].fov.angleRight), tanf(-v[i].fov.angleLeft)));
        ty = fmaxf(ty, fmaxf(tanf(v[i].fov.angleUp), tanf(-v[i].fov.angleDown)));
    }
    if (tx > 0.05f && ty > 0.05f) {
        float wa = c.window_aspect;
        if (tx / ty < wa) tx = ty * wa;
        else ty = tx / wa;
        c.tan_x = tx, c.tan_y = ty;
        c.cur = (vr_view){true, eye, to_xr(view), {-atanf(tx), atanf(tx), atanf(ty), -atanf(ty)}};
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
    c.q[(c.q_head + c.q_count++) % QUEUE] = v;
    trace("present", v.eye, v.world);
    // the next frame: the other eye after a left one, else the left
    if (stereo_on()) vr_pair_present(&c.pair, v.world);
    c.cam_set = false;
    pthread_mutex_unlock(&lock);
}

// the present on the queue thread (device.c backend_present): what the frame being presented is
bool vr_present_take(vr_view *out) {
    pthread_mutex_lock(&lock);
    bool ok = c.q_count > 0;
    if (ok) {
        *out = c.q[c.q_head];
        c.q_head = (c.q_head + 1) % QUEUE;
        c.q_count--;
    }
    pthread_mutex_unlock(&lock);
    if (!ok) memset(out, 0, sizeof *out);
    return ok;
}

uint64_t vr_camera_updates(void) { return c.updates; }
uint64_t vr_stereo_pairs(void) { return c.pair.pairs; }
