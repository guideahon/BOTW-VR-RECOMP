// VR mode, phase 2: the headset moves the game's camera, and the game draws the headset's field of view.
//
// In play (not paused, no event, no title or loading screen) the world's view cameras (runtime/hle/freecam.c
// rt_lookat: sead::LookAtCamera eye / at / up) take the head's pose on top of the game's own camera: the game's camera
// keeps its position and heading (it still follows Link and turns with the stick), the head adds its turn and its
// movement since the last recentring, and gives the pitch and roll itself (a view that tilts without the head
// tilting makes people sick). The world camera's projection (runtime/nvn/output_scale.c rt_persp) becomes a symmetric
// frustum that holds the field of view of both eyes, in the window's shape; the window is sized to the headset's shape
// at start (vr_init), and the frame goes to the headset as a projection layer with that frustum and the pose the
// camera was given (runtime/vr/xr_vulkan.c). Elsewhere the game is left alone and shown on the virtual screen.
//
// One picture for both eyes (mono) until phase 3.
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "../framepace.h"
#include "../game_sites.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

#include "vr.h"
#include "vr_internal.h"
#include "vr_math.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    bool have_neutral;     // the head's pose at the last recentring
    vr_pose neutral;
    bool recenter;         // asked for (vr_recenter): taken at the next camera update
    vr_pose head;          // the head (VIEW in LOCAL) the cameras last got, and when it is shown
    XrTime head_time;
    bool head_ok;
    float tan_x, tan_y;    // the frustum the world camera last got (half-tangents)
    bool fov_ok;
    float window_aspect;   // the window image's width / height
    float out[9];          // the last eye / at / up written
    bool out_ok;
    float game[9];         // the game's own eye / at / up, as it last set them: a camera that holds still is not set
                           // again, and what rt_lookat then sees is what was written here last (so is the camera's copy)
    uint64_t updates;
} c = {.window_aspect = 1};

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

void vr_window_aspect(float a) {
    if (a > 0.1f && a < 10) c.window_aspect = a;
}

static vr_pose from_xr(XrPosef p) {
    vr_pose r = {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                 {p.position.x, p.position.y, p.position.z}};
    return r;
}

// the head and both eyes' fields of view at the time the frame being made should be shown
static bool locate(vr_pose *head, XrTime *when, float *tx, float *ty) {
    XrTime t = vr_next_display_time();
    XrSession s = vr_xr_session();
    if (!t || s == XR_NULL_HANDLE) return false;
    XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO, NULL, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, t, vr_xr_local()};
    XrViewState vs = {XR_TYPE_VIEW_STATE};
    XrView v[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t n = 0;
    if (XR_FAILED(xrLocateViews(s, &li, &vs, 2, &n, v)) || n != 2) return false;
    if (!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return false;
    // the head: halfway between the eyes, turned as the first one (the runtime turns both alike)
    vr_pose l = from_xr(v[0].pose), r = from_xr(v[1].pose);
    *head = l;
    head->p = vr_v3_scale(vr_v3_add(l.p, r.p), 0.5f);
    *when = t;
    float x = 0, y = 0;
    for (int i = 0; i < 2; i++) {
        x = fmaxf(x, fmaxf(tanf(v[i].fov.angleRight), tanf(-v[i].fov.angleLeft)));
        y = fmaxf(y, fmaxf(tanf(v[i].fov.angleUp), tanf(-v[i].fov.angleDown)));
    }
    *tx = x, *ty = y;
    return x > 0.05f && y > 0.05f;
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
    vr_pose head;
    XrTime when;
    float tx, ty;
    if (!locate(&head, &when, &tx, &ty)) {
        pthread_mutex_unlock(&lock);
        return;
    }
    if (!c.have_neutral || c.recenter) {
        c.neutral = head;
        c.have_neutral = true;
        c.recenter = false;
        LOG("[vr] recentred: head at %.2f %.2f %.2f, heading %.1f deg\n", head.p.x, head.p.y, head.p.z,
            vr_quat_heading(head.q) * 57.29578f);
    }
    // the game's camera: where it is and the way it looks along the ground
    const float *gf = c.game;
    vr_vec3 eye = vr_v3(gf[0], gf[1], gf[2]);
    vr_vec3 dir = vr_v3(gf[3] - gf[0], gf[4] - gf[1], gf[5] - gf[2]);
    float yaw = vr_game_yaw_of(dir);
    // the anchor that puts the neutral head at the game's eye, looking the game's way; the head's pose from there
    vr_anchor a = vr_anchor_recenter(c.neutral, eye, yaw, 1.0f);
    vr_lookat_from_view(a, head, f, f + 3, f + 6);
    memcpy(c.out, f, sizeof c.out);
    c.out_ok = true;
    c.head = head;
    c.head_time = when;
    c.head_ok = true;
    // the frustum: both eyes' fields, widened to the window's shape
    float wa = c.window_aspect;
    if (tx / ty < wa) tx = ty * wa;
    else ty = tx / wa;
    c.tan_x = tx, c.tan_y = ty;
    c.fov_ok = true;
    c.updates++;
    // (debugging) SAVAGEWIND_VR_LOG_CAMERA=<n>: every n-th update, the head, the game's camera and the view made
    static int log_every = -1;
    if (log_every < 0) log_every = getenv("SAVAGEWIND_VR_LOG_CAMERA") ? atoi(getenv("SAVAGEWIND_VR_LOG_CAMERA")) : 0;
    if (log_every > 0 && c.updates % (uint64_t)log_every == 0) {
        vr_vec3 od = vr_v3(f[3] - f[0], f[4] - f[1], f[5] - f[2]);
        LOG("[vr-cam] head %.2f %.2f %.2f heading %.1f | game eye %.1f %.1f %.1f yaw %.1f | view yaw %.1f pitch %.1f "
            "eye %.2f %.2f %.2f | t %lld\n", head.p.x, head.p.y, head.p.z, vr_quat_heading(head.q) * 57.29578f, eye.x,
            eye.y, eye.z, yaw * 57.29578f, vr_game_yaw_of(od) * 57.29578f, vr_game_pitch_of(od) * 57.29578f, f[0], f[1],
            f[2], (long long)when);
    }
    pthread_mutex_unlock(&lock);
}

// rt_persp, the world camera (far plane over 10000): its tan(fovy / 2) and aspect in play
bool vr_persp(float *tan_fovy, float *aspect) {
    if (!vr_world_view()) return false;
    pthread_mutex_lock(&lock);
    bool ok = c.fov_ok;
    if (ok) *tan_fovy = c.tan_y, *aspect = c.tan_x / c.tan_y;
    pthread_mutex_unlock(&lock);
    return ok;
}

// for the frame being ended (xr_vulkan.c): the pose and frustum the cameras were last given
bool vr_camera_view(XrPosef *pose, XrFovf *fov) {
    pthread_mutex_lock(&lock);
    bool ok = c.head_ok && c.fov_ok;
    if (ok) {
        *pose = (XrPosef){{c.head.q.x, c.head.q.y, c.head.q.z, c.head.q.w}, {c.head.p.x, c.head.p.y, c.head.p.z}};
        fov->angleLeft = -atanf(c.tan_x), fov->angleRight = atanf(c.tan_x);
        fov->angleUp = atanf(c.tan_y), fov->angleDown = -atanf(c.tan_y);
    }
    pthread_mutex_unlock(&lock);
    return ok;
}

uint64_t vr_camera_updates(void) { return c.updates; }
