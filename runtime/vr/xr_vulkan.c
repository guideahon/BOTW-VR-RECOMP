// VR mode: the OpenXR session on the game's Vulkan device, and what each of the game's frames shows in the headset.
// runtime/nvn/vulkan/device.c calls in here:
//   create_instance  -> vr_vk_create_instance   (XR_KHR_vulkan_enable2: the runtime adds what it needs)
//   pick_device      -> vr_vk_physical_device    (the headset's GPU)
//   create_device    -> vr_vk_create_device, then vr_vk_session_start
//   backend_present  -> vr_present_begin        (what the frame being presented was drawn as: vr_camera.c)
//   vk_frame_end     -> vr_frame_record (before the window image goes to present) and vr_frame_after_submit
//
// The window image (the game's frame) is copied into one of three swapchains: the virtual screen's (menus, events,
// loading: a quad layer, phase 1) or an eye's (in play: a projection layer, phases 2 and 3). In stereo the game draws
// left then right; the left frame only updates the left eye's image, the right frame updates the right one and makes
// the headset's frame with both (xrWaitFrame paces the pairs). In mono (SAVAGEWIND_VR_STEREO=0) each frame is a
// headset frame with one picture for both eyes.
//
// Order on the queue: OpenXR may use the session's queue (g_vk.queue) inside xrReleaseSwapchainImage and xrEndFrame,
// and wants the app's writes to an image submitted before the release. Both run through rec_custom, on the recorder
// thread, after the submission that copies into the image: in order with every other use of the queue. The next
// frame waits until they have run, so one release / headset frame is in flight at a time.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "../rt.h"
#include "../nvn/vulkan/vk.h"
#include "../nvn/vulkan/record.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "vr.h"
#include "vr_internal.h"

#define MAX_IMAGES 8
enum { SC_LEFT, SC_RIGHT, SC_SCREEN, SC_HUD, SC_COUNT };

static PFN_xrGetVulkanGraphicsRequirements2KHR GetRequirements;
static PFN_xrCreateVulkanInstanceKHR CreateInstance;
static PFN_xrGetVulkanGraphicsDevice2KHR GetDevice;
static PFN_xrCreateVulkanDeviceKHR CreateDevice;

typedef struct {
    XrSwapchain h;
    VkFormat format;
    VkExtent2D extent;
    uint32_t n;
    VkImage images[MAX_IMAGES];
} swapchain;

static struct {
    XrSession session;
    XrSpace local, view;          // the room (LOCAL) and the head (VIEW: the HUD goes with it)
    XrSessionState state;
    bool running;
    swapchain sc[SC_COUNT];
    vr_view eye_view[2];          // what each eye's last released image is (a projection view needs one per eye)
    bool eye_ready[2];
    VkPhysicalDevice phys;
    float screen_w, screen_dist;  // the virtual screen: its width and distance (m)
    bool hud_on;                  // phase 4: the HUD apart from the scene, in front of the eyes (SAVAGEWIND_VR_HUD)
    float hud_w, hud_dist;        // ... its width and distance (m)
    bool hud_ready;               // the HUD's swapchain has an image released
    uint64_t frames, rendered, world_frames, presents;
    XrTime last_display;          // the last frame's predicted display time and the display's period
    XrDuration period;
} g;

static vr_view pres;  // what the frame being presented was drawn as (vr_present_begin)

// what the recorder thread does after the frame's submission (end_frame)
static struct {
    bool release[SC_COUNT];  // the swapchains whose image to release
    vr_view view;            // what the frame shows
    bool active;             // xrBeginFrame done, xrEndFrame not yet
    XrTime display;
    float aspect;            // height / width of the window image
    float hud_aspect;        // ... of the HUD's
    int kind;                // the headset frame's layer: 0 none, 1 the screen (quad), 2 the eyes (projection)
    bool hud;                // ... and the HUD in front of them
} fr;
static pthread_mutex_t end_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t end_done = PTHREAD_COND_INITIALIZER;
static bool end_pending;

static bool load_fns(XrInstance inst) {
#define LOAD(var, name) \
    if (XR_FAILED(xrGetInstanceProcAddr(inst, name, (PFN_xrVoidFunction *)&var)) || !var) return false;
    LOAD(GetRequirements, "xrGetVulkanGraphicsRequirements2KHR")
    LOAD(CreateInstance, "xrCreateVulkanInstanceKHR")
    LOAD(GetDevice, "xrGetVulkanGraphicsDevice2KHR")
    LOAD(CreateDevice, "xrCreateVulkanDeviceKHR")
#undef LOAD
    return true;
}

VkResult vr_vk_create_instance(const VkInstanceCreateInfo *ci, VkInstance *out) {
    XrInstance inst = vr_xr_instance();
    if (inst == XR_NULL_HANDLE) return vkCreateInstance(ci, NULL, out);
    if (!load_fns(inst)) {
        vr_disable("the runtime gave no XR_KHR_vulkan_enable2 functions");
        return vkCreateInstance(ci, NULL, out);
    }
    XrGraphicsRequirementsVulkanKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    if (!vr_xr_ok(GetRequirements(inst, vr_xr_system(), &req), "xrGetVulkanGraphicsRequirements2KHR")) {
        vr_disable("no Vulkan requirements");
        return vkCreateInstance(ci, NULL, out);
    }
    LOG("[vr] runtime wants Vulkan %u.%u to %u.%u (the game asks for 1.3)\n", XR_VERSION_MAJOR(req.minApiVersionSupported),
        XR_VERSION_MINOR(req.minApiVersionSupported), XR_VERSION_MAJOR(req.maxApiVersionSupported),
        XR_VERSION_MINOR(req.maxApiVersionSupported));
    XrVulkanInstanceCreateInfoKHR xci = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR, NULL, vr_xr_system(), 0,
                                         vkGetInstanceProcAddr, ci, NULL};
    VkResult vr = VK_ERROR_INITIALIZATION_FAILED;
    if (!vr_xr_ok(CreateInstance(inst, &xci, out, &vr), "xrCreateVulkanInstanceKHR")) {
        vr_disable("the runtime could not make the Vulkan instance");
        return vkCreateInstance(ci, NULL, out);
    }
    return vr;
}

VkPhysicalDevice vr_vk_physical_device(VkInstance vk) {
    XrInstance inst = vr_xr_instance();
    if (inst == XR_NULL_HANDLE) return VK_NULL_HANDLE;
    XrVulkanGraphicsDeviceGetInfoKHR gi = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR, NULL, vr_xr_system(), vk};
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    if (!vr_xr_ok(GetDevice(inst, &gi, &pd), "xrGetVulkanGraphicsDevice2KHR")) {
        vr_disable("the runtime named no GPU");
        return VK_NULL_HANDLE;
    }
    g.phys = pd;
    return pd;
}

VkResult vr_vk_create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, VkDevice *out) {
    XrInstance inst = vr_xr_instance();
    if (inst == XR_NULL_HANDLE || pd != g.phys) {
        if (inst != XR_NULL_HANDLE) vr_disable("the game's GPU is not the headset's");
        return vkCreateDevice(pd, ci, NULL, out);
    }
    XrVulkanDeviceCreateInfoKHR xdi = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR, NULL, vr_xr_system(), 0,
                                       vkGetInstanceProcAddr, pd, ci, NULL};
    VkResult vr = VK_ERROR_INITIALIZATION_FAILED;
    if (!vr_xr_ok(CreateDevice(inst, &xdi, out, &vr), "xrCreateVulkanDeviceKHR")) {
        vr_disable("the runtime could not make the Vulkan device");
        return vkCreateDevice(pd, ci, NULL, out);
    }
    return vr;
}

static float env_float(const char *name, float def, float lo, float hi) {
    const char *v = getenv(name);
    float f = v && *v ? (float)atof(v) : def;
    return f >= lo && f <= hi ? f : def;
}

void vr_vk_session_start(VkInstance vk, VkPhysicalDevice pd, VkDevice dev, uint32_t qfam, uint32_t qindex) {
    XrInstance inst = vr_xr_instance();
    if (inst == XR_NULL_HANDLE) return;
    XrGraphicsBindingVulkanKHR bind = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR, NULL, vk, pd, dev, qfam, qindex};
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO, &bind, 0, vr_xr_system()};
    if (!vr_xr_ok(xrCreateSession(inst, &sci, &g.session), "xrCreateSession")) {
        vr_disable("no session");
        return;
    }
    XrReferenceSpaceCreateInfo rci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO, NULL, XR_REFERENCE_SPACE_TYPE_LOCAL,
                                      {{0, 0, 0, 1}, {0, 0, 0}}};
    if (!vr_xr_ok(xrCreateReferenceSpace(g.session, &rci, &g.local), "xrCreateReferenceSpace")) {
        vr_disable("no LOCAL space");
        return;
    }
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!vr_xr_ok(xrCreateReferenceSpace(g.session, &rci, &g.view), "xrCreateReferenceSpace (VIEW)")) {
        vr_disable("no VIEW space");
        return;
    }
    // the virtual screen (SAVAGEWIND_VR_SCREEN_WIDTH / _DISTANCE, metres): 3 m wide, 2.5 m ahead, at eye height
    g.screen_w = env_float("SAVAGEWIND_VR_SCREEN_WIDTH", 3.0f, 0.2f, 50.0f);
    g.screen_dist = env_float("SAVAGEWIND_VR_SCREEN_DISTANCE", 2.5f, 0.3f, 50.0f);
    // the HUD in play (SAVAGEWIND_VR_HUD=0: left in the eyes' pictures; _WIDTH / _DISTANCE): 1.6 m wide, 1.4 m ahead
    const char *hud = getenv("SAVAGEWIND_VR_HUD");
    g.hud_on = !(hud && *hud == '0');
    g.hud_w = env_float("SAVAGEWIND_VR_HUD_WIDTH", 1.6f, 0.1f, 20.0f);
    g.hud_dist = env_float("SAVAGEWIND_VR_HUD_DISTANCE", 1.4f, 0.2f, 20.0f);
    LOG("[vr] session on queue family %u index %u; virtual screen %.1f m wide at %.1f m; HUD %s\n", qfam, qindex,
        g.screen_w, g.screen_dist, g.hud_on ? "apart, in front of the eyes" : "in the eyes' pictures");
    vr_input_setup(g.session);
}

// ---- swapchains: the window image's size, in the sRGB twin of its format (its bytes are already gamma-encoded, so
// they are copied as they are and the runtime reads them as sRGB) ----
static void drop_swapchain(int i) {
    swapchain *s = &g.sc[i];
    if (s->h == XR_NULL_HANDLE) return;
    rec_drain();
    vkDeviceWaitIdle(g_vk.dev);  // (only on a resize or the end of the session: its images may still be in use)
    xrDestroySwapchain(s->h);
    memset(s, 0, sizeof *s);
    if (i < 2) g.eye_ready[i] = false;
    if (i == SC_HUD) g.hud_ready = false;
}

static VkFormat srgb_twin(VkFormat f) {
    switch (f) {
    case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
    case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB;
    default: return f;
    }
}

static bool make_swapchain(int i, VkExtent2D ext, VkFormat src) {
    swapchain *s = &g.sc[i];
    VkFormat want = srgb_twin(src);
    uint32_t n = 0;
    int64_t formats[64];
    if (!vr_xr_ok(xrEnumerateSwapchainFormats(g.session, 64, &n, formats), "xrEnumerateSwapchainFormats")) return false;
    bool offered = false;
    for (uint32_t k = 0; k < n; k++) offered |= formats[k] == want;
    if (!offered) {
        static bool said;
        if (!said) LOG("[vr] the runtime offers no swapchain in format %d: nothing for the headset\n", (int)want);
        said = true;
        return false;
    }
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO, NULL, 0,
                                XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT, want, 1,
                                ext.width, ext.height, 1, 1, 1};
    if (!vr_xr_ok(xrCreateSwapchain(g.session, &ci, &s->h), "xrCreateSwapchain")) return false;
    XrSwapchainImageVulkanKHR imgs[MAX_IMAGES];
    for (int k = 0; k < MAX_IMAGES; k++) imgs[k] = (XrSwapchainImageVulkanKHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    if (!vr_xr_ok(xrEnumerateSwapchainImages(s->h, MAX_IMAGES, &s->n, (XrSwapchainImageBaseHeader *)imgs),
                  "xrEnumerateSwapchainImages")) {
        xrDestroySwapchain(s->h);
        s->h = XR_NULL_HANDLE;
        return false;
    }
    for (uint32_t k = 0; k < s->n; k++) s->images[k] = imgs[k].image;
    s->extent = ext;
    s->format = src;
    static const char *const names[] = {"left eye", "right eye", "virtual screen", "HUD"};
    LOG("[vr] %s swapchain %ux%u, %u images\n", names[i], ext.width, ext.height, s->n);
    return true;
}

// ---- session states ----
static void end_session(const char *why) {
    for (int i = 0; i < SC_COUNT; i++) drop_swapchain(i);
    if (g.local != XR_NULL_HANDLE) xrDestroySpace(g.local);
    if (g.view != XR_NULL_HANDLE) xrDestroySpace(g.view);
    if (g.session != XR_NULL_HANDLE) xrDestroySession(g.session);
    g.local = g.view = XR_NULL_HANDLE;
    g.session = XR_NULL_HANDLE;
    g.running = false;
    vr_disable(why);
}

static void poll_events(void) {
    XrInstance inst = vr_xr_instance();
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (g.session != XR_NULL_HANDLE && xrPollEvent(inst, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrSessionState st = ((XrEventDataSessionStateChanged *)&ev)->state;
            g.state = st;
            LOG("[vr] session state %d\n", (int)st);
            if (st == XR_SESSION_STATE_READY && !g.running) {
                XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO, NULL, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
                if (vr_xr_ok(xrBeginSession(g.session, &bi), "xrBeginSession")) g.running = true;
            } else if (st == XR_SESSION_STATE_STOPPING && g.running) {
                vr_xr_ok(xrEndSession(g.session), "xrEndSession");
                g.running = false;
            } else if (st == XR_SESSION_STATE_EXITING) {
                end_session("the runtime ended the session");
            } else if (st == XR_SESSION_STATE_LOSS_PENDING) {
                end_session("the session was lost");
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            end_session("the runtime is going away");
        }
        ev = (XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
}

static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                    VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = src_stage,
                               .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access,
                               .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
                           .pImageMemoryBarriers = &b};
    rec_CmdPipelineBarrier2(cb, &di);
}

// ---- frames ----
void vr_present_begin(void) {
    vr_present_take(&pres);
    g.presents++;
}

static void wait_previous_end(void) {
    pthread_mutex_lock(&end_lock);
    while (end_pending) pthread_cond_wait(&end_done, &end_lock);
    pthread_mutex_unlock(&end_lock);
}

// the headset's frame: xrWaitFrame (which paces it) and xrBeginFrame; false when there is none to make
static bool begin_headset_frame(void) {
    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    if (!vr_xr_ok(xrWaitFrame(g.session, NULL, &fs), "xrWaitFrame")) return false;
    XrResult r = xrBeginFrame(g.session, NULL);
    if (XR_FAILED(r)) {
        vr_xr_ok(r, "xrBeginFrame");
        return false;
    }
    fr.active = true;
    fr.kind = 0;
    fr.display = fs.predictedDisplayTime;
    g.last_display = fs.predictedDisplayTime;
    g.period = fs.predictedDisplayPeriod;
    g.frames++;
    return fs.shouldRender;
}

// src (in layout src_layout: COLOR_ATTACHMENT_OPTIMAL for the window image, GENERAL for exec.c's images) into
// swapchain i, made at its size in the sRGB twin of its format: acquired and waited here, released after the submission
static bool put_into(int i, VkCommandBuffer cb, VkImage src, VkImageLayout src_layout, VkExtent2D ext, VkFormat fmt) {
    swapchain *s = &g.sc[i];
    if (s->h != XR_NULL_HANDLE && (s->extent.width != ext.width || s->extent.height != ext.height || s->format != fmt))
        drop_swapchain(i);
    if (s->h == XR_NULL_HANDLE && !make_swapchain(i, ext, fmt)) return false;
    uint32_t idx = 0;
    if (!vr_xr_ok(xrAcquireSwapchainImage(s->h, NULL, &idx), "xrAcquireSwapchainImage")) return false;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
    if (!vr_xr_ok(xrWaitSwapchainImage(s->h, &wi), "xrWaitSwapchainImage")) return false;
    VkImage dst = s->images[idx];
    bool window = src_layout != VK_IMAGE_LAYOUT_GENERAL;
    VkImageLayout read = window ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    // the window image: drawn as a colour attachment, read here, then drawn on again (the menu) or presented; exec.c's
    // images stay in GENERAL (written by its passes, a full barrier after them)
    if (window)
        barrier(cb, src, src_layout, read, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {ext.width, ext.height, 1}};
    rec_CmdCopyImage(cb, src, read, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    // the runtime takes its images back as colour attachments (XR_KHR_vulkan_enable2)
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    if (window)
        barrier(cb, src, read, src_layout, VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fr.release[i] = true;
    g.rendered++;
    return true;
}

// exec.c (vr_hud_prepare): this frame's 3D scene without the HUD, and the HUD alone (false: not made this frame)
bool vk_vr_images(VkImage *scene, VkFormat *scene_fmt, VkImage *hud, VkFormat *hud_fmt, VkExtent2D *ext);

bool vr_hud_wanted(void) { return g.hud_on && vr_world_view(); }

void vr_frame_record(VkCommandBuffer cb, VkImage src, VkExtent2D ext, VkFormat fmt) {
    if (g.session == XR_NULL_HANDLE) return;
    wait_previous_end();
    poll_events();
    if (!g.running || g.session == XR_NULL_HANDLE) return;
    vr_window_aspect((float)ext.width / (float)ext.height);
    memset(fr.release, 0, sizeof fr.release);
    fr.view = pres;
    fr.aspect = (float)ext.height / (float)ext.width;
    fr.hud = false;
    {   // (debugging) SAVAGEWIND_VR_TRACE=<n>: the first n frames recorded in play, as what
        static int left = -1;
        if (left < 0) left = getenv("SAVAGEWIND_VR_TRACE") ? atoi(getenv("SAVAGEWIND_VR_TRACE")) : 0;
        if (left > 0 && pres.world) {
            left--;
            LOG("[vr-trace] record eye %d world %d presents %llu\n", pres.eye, pres.world, (unsigned long long)g.presents);
        }
    }
    // in play, with the HUD apart: the eyes get the scene without it, the HUD its own image (exec.c vr_hud_prepare)
    VkImage scene = src, hud = VK_NULL_HANDLE;
    VkFormat scene_fmt = fmt, hud_fmt = fmt;
    VkExtent2D sext = ext;
    VkImageLayout sl = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bool split = pres.world && g.hud_on && vk_vr_images(&scene, &scene_fmt, &hud, &hud_fmt, &sext);
    if (split) sl = VK_IMAGE_LAYOUT_GENERAL;
    else scene = src, scene_fmt = fmt, sext = ext;
    if (pres.world && pres.eye == 0) {  // stereo, the left frame: its eye's image only, the headset's frame comes next
        put_into(SC_LEFT, cb, scene, sl, sext, scene_fmt);
        g.world_frames++;
        return;
    }
    if (!begin_headset_frame()) return;
    if (pres.world) {  // the right frame (stereo) or the only one (mono: both eyes see the left swapchain's picture)
        if (put_into(pres.eye == 1 ? SC_RIGHT : SC_LEFT, cb, scene, sl, sext, scene_fmt)) fr.kind = 2;
        if (split && put_into(SC_HUD, cb, hud, VK_IMAGE_LAYOUT_GENERAL, sext, hud_fmt)) {
            fr.hud = true;
            fr.hud_aspect = (float)sext.height / (float)sext.width;
        }
        g.world_frames++;
    } else if (put_into(SC_SCREEN, cb, src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, ext, fmt)) {
        fr.kind = 1;
    }
}

static void end_frame(void *arg) {
    (void)arg;
    for (int i = 0; i < SC_COUNT; i++) {
        if (!fr.release[i]) continue;
        fr.release[i] = false;
        if (!vr_xr_ok(xrReleaseSwapchainImage(g.sc[i].h, NULL), "xrReleaseSwapchainImage")) continue;
        if (i < 2) {
            g.eye_view[i] = fr.view;
            g.eye_ready[i] = true;
        }
        if (i == SC_HUD) g.hud_ready = true;
    }
    if (fr.active) {
        XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD}, hudq = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView views[2];
        const XrCompositionLayerBaseHeader *layers[2];
        uint32_t n = 0;
        bool mono = fr.view.world && fr.view.eye < 0;
        if (fr.kind == 2 && (mono ? g.eye_ready[0] : g.eye_ready[0] && g.eye_ready[1])) {
            for (int i = 0; i < 2; i++) {
                int sc = mono ? 0 : i;  // (mono: the left swapchain's picture, at the head's pose, for both)
                views[i] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                views[i].pose = g.eye_view[sc].pose;
                views[i].fov = g.eye_view[sc].fov;
                views[i].subImage.swapchain = g.sc[sc].h;
                views[i].subImage.imageRect =
                    (XrRect2Di){{0, 0}, {(int32_t)g.sc[sc].extent.width, (int32_t)g.sc[sc].extent.height}};
            }
            proj.space = g.local;
            proj.viewCount = 2;
            proj.views = views;
            layers[n++] = (const XrCompositionLayerBaseHeader *)&proj;
            if (fr.hud && g.hud_ready) {  // the HUD, with the head, a little below the line of sight
                hudq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                  XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                hudq.space = g.view;
                hudq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                hudq.subImage.swapchain = g.sc[SC_HUD].h;
                hudq.subImage.imageRect =
                    (XrRect2Di){{0, 0}, {(int32_t)g.sc[SC_HUD].extent.width, (int32_t)g.sc[SC_HUD].extent.height}};
                hudq.pose = (XrPosef){{0, 0, 0, 1}, {0, -0.05f, -g.hud_dist}};
                hudq.size = (XrExtent2Df){g.hud_w, g.hud_w * fr.hud_aspect};
                layers[n++] = (const XrCompositionLayerBaseHeader *)&hudq;
            }
        } else if (fr.kind == 1) {
            quad.space = g.local;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = g.sc[SC_SCREEN].h;
            quad.subImage.imageRect = (XrRect2Di){
                {0, 0}, {(int32_t)g.sc[SC_SCREEN].extent.width, (int32_t)g.sc[SC_SCREEN].extent.height}};
            quad.pose = (XrPosef){{0, 0, 0, 1}, {0, 0, -g.screen_dist}};
            quad.size = (XrExtent2Df){g.screen_w, g.screen_w * fr.aspect};
            layers[n++] = (const XrCompositionLayerBaseHeader *)&quad;
        }
        XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO, NULL, fr.display, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, n, layers};
        vr_xr_ok(xrEndFrame(g.session, &ei), "xrEndFrame");
        fr.active = false;
    }
    pthread_mutex_lock(&end_lock);
    end_pending = false;
    pthread_cond_broadcast(&end_done);
    pthread_mutex_unlock(&end_lock);
}

void vr_frame_after_submit(void) {
    bool any = fr.active;
    for (int i = 0; i < SC_COUNT; i++) any |= fr.release[i];
    if (!any) return;
    pthread_mutex_lock(&end_lock);
    end_pending = true;
    pthread_mutex_unlock(&end_lock);
    rec_custom(end_frame, NULL);
}

bool vr_session_running(void) { return g.running && g.session != XR_NULL_HANDLE; }
XrSession vr_xr_session(void) { return g.session; }
XrSpace vr_xr_local(void) { return g.local; }

// when the frame the game makes now should be shown: two periods after the last one begun (one being drawn, one shown)
XrTime vr_next_display_time(void) { return g.last_display ? g.last_display + 2 * g.period : 0; }

uint64_t vr_world_frames(void) { return g.world_frames; }

void vr_stats(uint64_t *frames, uint64_t *rendered, int *state) {
    *frames = g.frames;
    *rendered = g.rendered;
    *state = (int)g.state;
}
