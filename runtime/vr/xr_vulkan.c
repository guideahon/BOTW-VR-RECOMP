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
#include <math.h>
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
#include "vr_math.h"
#include "vr_options.h"

#define MAX_IMAGES 8
enum {
    SC_LEFT, SC_RIGHT, SC_SCREEN, SC_HUD, SC_VIGNETTE, SC_MARKER, SC_PERF, SC_LEFT2, SC_RIGHT2,
    SC_DEPTH_LEFT, SC_DEPTH_RIGHT, SC_COUNT
};
// (each eye has two swapchains: its picture comes in two formats, the scene's apart from the HUD and the window's;
// switching between them used to make the swapchain again, a GPU wait each time, a stutter in the headset)
static inline int eye_of(int i) { return i == SC_LEFT || i == SC_LEFT2 ? 0 : i == SC_RIGHT || i == SC_RIGHT2 ? 1 : -1; }
static inline int depth_eye_of(int i) { return i == SC_DEPTH_LEFT ? 0 : i == SC_DEPTH_RIGHT ? 1 : -1; }

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
    XrSpace stage;                // the floor (STAGE; none if the runtime has no such space)
    float local_y;                // LOCAL's origin above STAGE's (m), when known
    bool local_y_ok;
    XrSessionState state;
    bool running;
    swapchain sc[SC_COUNT];
    vr_view eye_view[2];          // what each eye's last released image is (a projection view needs one per eye)
    bool eye_ready[2];
    int eye_sc[2];                // ... and which of its two swapchains has it
    bool eye_depth_ready[2];
    int eye_depth_sc[2];
    VkExtent2D eye_depth_extent[2];
    float eye_depth_near[2], eye_depth_far[2];
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
bool vr_depth_wanted(void) { return vr_depth_enabled() && vr_world_view(); }

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
    bool perf;               // ... and the performance overlay
    float perf_aspect;
    int depth_eye;
    VkExtent2D depth_extent;
    float depth_near, depth_far;
} fr;
static uint64_t perf_frames;   // headset frames with the performance overlay
uint64_t vr_perf_frames(void) { return perf_frames; }
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
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;   // (the floor: Options > VR > Play position > Standing)
    if (XR_FAILED(xrCreateReferenceSpace(g.session, &rci, &g.stage))) g.stage = XR_NULL_HANDLE;
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
static void vig_dropped(void);
static void marker_dropped(void);

static void drop_swapchain(int i) {
    swapchain *s = &g.sc[i];
    if (s->h == XR_NULL_HANDLE) return;
    rec_drain();
    vkDeviceWaitIdle(g_vk.dev);  // (only on a resize or the end of the session: its images may still be in use)
    xrDestroySwapchain(s->h);
    memset(s, 0, sizeof *s);
    if (eye_of(i) >= 0 && g.eye_sc[eye_of(i)] == i) g.eye_ready[eye_of(i)] = false;
    if (depth_eye_of(i) >= 0 && g.eye_depth_sc[depth_eye_of(i)] == i)
        g.eye_depth_ready[depth_eye_of(i)] = false;
    if (i == SC_HUD) g.hud_ready = false;
    if (i == SC_VIGNETTE) vig_dropped();
    if (i == SC_MARKER) marker_dropped();
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
    static const char *const names[] = {"left eye", "right eye", "virtual screen", "HUD", "comfort vignette",
                                        "controller marker", "performance overlay", "left eye (2)", "right eye (2)"};
    LOG("[vr] %s swapchain %ux%u, %u images (format %d)\n", names[i], ext.width, ext.height, s->n, (int)src);
    return true;
}

static bool make_depth_swapchain(int i, VkExtent2D ext, VkFormat fmt) {
    swapchain *s = &g.sc[i];
    uint32_t n = 0;
    int64_t formats[64];
    if (!vr_xr_ok(xrEnumerateSwapchainFormats(g.session, 64, &n, formats), "xrEnumerateSwapchainFormats(depth)")) return false;
    bool offered = false;
    for (uint32_t k = 0; k < n; k++) offered |= formats[k] == fmt;
    if (!offered) {
        static bool said;
        if (!said) LOG("[vr] the runtime offers no depth swapchain in format %d; composition depth is skipped\n", (int)fmt);
        said = true;
        return false;
    }
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO, NULL, 0,
                                XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
                                fmt, 1, ext.width, ext.height, 1, 1, 1};
    if (!vr_xr_ok(xrCreateSwapchain(g.session, &ci, &s->h), "xrCreateSwapchain(depth)")) return false;
    XrSwapchainImageVulkanKHR imgs[MAX_IMAGES];
    for (int k = 0; k < MAX_IMAGES; k++) imgs[k] = (XrSwapchainImageVulkanKHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    if (!vr_xr_ok(xrEnumerateSwapchainImages(s->h, MAX_IMAGES, &s->n, (XrSwapchainImageBaseHeader *)imgs),
                  "xrEnumerateSwapchainImages(depth)")) {
        xrDestroySwapchain(s->h);
        s->h = XR_NULL_HANDLE;
        return false;
    }
    for (uint32_t k = 0; k < s->n; k++) s->images[k] = imgs[k].image;
    s->extent = ext;
    s->format = fmt;
    LOG("[vr] %s depth swapchain %ux%u, %u images (format %d)\n", i == SC_DEPTH_LEFT ? "left eye" : "right eye",
        ext.width, ext.height, s->n, (int)fmt);
    return true;
}

// ---- session states ----
static void end_session(const char *why) {
    for (int i = 0; i < SC_COUNT; i++) drop_swapchain(i);
    if (g.local != XR_NULL_HANDLE) xrDestroySpace(g.local);
    if (g.view != XR_NULL_HANDLE) xrDestroySpace(g.view);
    if (g.stage != XR_NULL_HANDLE) xrDestroySpace(g.stage);
    g.stage = XR_NULL_HANDLE, g.local_y_ok = false;
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

static void barrier_aspect(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, VkImageLayout from,
                           VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = src_stage,
                               .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access,
                               .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                               .subresourceRange = {aspect, 0, 1, 0, 1}};
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

// ---- the bow's arc (vr_skel.c vr_bow_arc): dots copied onto an eye's image, each the size of a tile from a small
// buffer of one colour, at where the eye's pose and frustum see the arc's points (over the scene: no depth) ----
#define ARC_MAX 48
#define ARC_TILE 16
static struct {
    float pts[ARC_MAX][3];
    int n;               // this frame's points (0: no arc)
    VkBuffer buf;        // two tiles: R G B A, then B G R A
    VmaAllocation mem;
    bool failed;
    uint64_t frames;     // eye images the arc was drawn on
} arc;

int vr_bow_arc(float (*pts)[3], int max);  // vr_skel.c
uint64_t vr_arc_frames(void) { return arc.frames; }

static bool arc_buffer(void) {
    if (arc.buf) return true;
    if (arc.failed) return false;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 2 * ARC_TILE * ARC_TILE * 4,
                             .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
    VmaAllocationInfo info;
    if (vmaCreateBuffer(g_vk.vma, &bi, &ai, &arc.buf, &arc.mem, &info) != VK_SUCCESS || !info.pMappedData) {
        arc.failed = true;
        arc.buf = VK_NULL_HANDLE;
        return false;
    }
    uint8_t *p = info.pMappedData;
    for (int k = 0; k < ARC_TILE * ARC_TILE; k++) {   // a warm yellow, as sRGB bytes
        uint8_t *a = p + 4 * k, *b = p + 4 * (ARC_TILE * ARC_TILE + k);
        a[0] = 255, a[1] = 214, a[2] = 72, a[3] = 255;
        b[0] = 72, b[1] = 214, b[2] = 255, b[3] = 255;
    }
    vmaFlushAllocation(g_vk.vma, arc.mem, 0, VK_WHOLE_SIZE);
    return true;
}

// onto dst (an eye's swapchain image of the format src_fmt, in TRANSFER_DST_OPTIMAL), seen as the view v
static void arc_draw(VkCommandBuffer cb, VkImage dst, VkExtent2D ext, VkFormat src_fmt, const vr_view *v) {
    VkDeviceSize tile;
    if (src_fmt == VK_FORMAT_R8G8B8A8_UNORM || src_fmt == VK_FORMAT_R8G8B8A8_SRGB) tile = 0;
    else if (src_fmt == VK_FORMAT_B8G8R8A8_UNORM || src_fmt == VK_FORMAT_B8G8R8A8_SRGB) tile = ARC_TILE * ARC_TILE * 4;
    else return;
    if (!arc.n || !v->world || !arc_buffer()) return;
    vr_quat qi = {-v->pose.orientation.x, -v->pose.orientation.y, -v->pose.orientation.z, v->pose.orientation.w};
    vr_vec3 eye = vr_v3(v->pose.position.x, v->pose.position.y, v->pose.position.z);
    float tl = tanf(v->fov.angleLeft), tr = tanf(v->fov.angleRight), tu = tanf(v->fov.angleUp), td = tanf(v->fov.angleDown);
    VkBufferImageCopy rg[ARC_MAX];
    uint32_t n = 0;
    for (int i = 0; i < arc.n; i++) {
        vr_vec3 p = vr_quat_rotate(qi, vr_v3_sub(vr_v3(arc.pts[i][0], arc.pts[i][1], arc.pts[i][2]), eye));
        if (p.z > -0.05f) continue;   // behind the eye
        float x = p.x / -p.z, y = p.y / -p.z;
        float u = (x - tl) / (tr - tl), w = (tu - y) / (tu - td);
        // a dot about 2.5 cm across where it is, at least 3 pixels, at most a tile
        float px = 0.025f / -p.z / (tr - tl) * (float)ext.width;
        int r = (int)(px * 0.5f + 0.5f);
        r = r < 2 ? 2 : r > ARC_TILE / 2 ? ARC_TILE / 2 : r;
        int cx = (int)(u * (float)ext.width), cy = (int)(w * (float)ext.height);
        int x0 = cx - r, y0 = cy - r, x1 = cx + r, y1 = cy + r;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > (int)ext.width) x1 = (int)ext.width;
        if (y1 > (int)ext.height) y1 = (int)ext.height;
        if (x1 <= x0 || y1 <= y0) continue;
        rg[n++] = (VkBufferImageCopy){.bufferOffset = tile, .bufferRowLength = ARC_TILE, .bufferImageHeight = ARC_TILE,
                                      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                      .imageOffset = {x0, y0, 0},
                                      .imageExtent = {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0), 1}};
    }
    if (!n) return;
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    rec_CmdCopyBufferToImage(cb, arc.buf, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, rg);
    arc.frames++;
}

// ---- the virtual screen shows the game's picture where it is in the window (device.c: the 16:9 box on the VR
// window, the headset eyes' shape), not the bars around it ----
static int screen_box[4];
void vr_screen_box(int x, int y, int w, int h) { screen_box[0] = x, screen_box[1] = y, screen_box[2] = w, screen_box[3] = h; }

// ---- the comfort vignette (Options > VR > Comfort vignette): while the sticks walk or turn Link (vr_input.c
// vr_stick_motion), the edges of the view go dark: a quad in the head's space over the eyes' pictures. Its image is
// black with an alpha ring; VIG_LEVELS of them (the ring closing in) are made once in a buffer, and the swapchain gets
// another only when the level changes ----
#define VIG_SIZE 128
#define VIG_LEVELS 12
#define VIG_SPAN 2.0f     // the quad's half-width, in tangents (1 m away, 4 m across: past a Quest's field)
static struct {
    VkBuffer buf;          // VIG_LEVELS images, RGBA
    VmaAllocation mem;
    bool failed;
    float level;           // smoothed, 0..1
    int shown;             // the level whose image the swapchain has (0, -1: none)
    bool ready;            // ... released
    uint64_t frames;       // headset frames it was shown in
} vig = {.shown = -1};

float vr_stick_motion(void);   // vr_input.c
uint64_t vr_vignette_frames(void) { return vig.frames; }
float vr_vignette_level(void) { return vig.level; }

static void vig_dropped(void) { vig.shown = -1, vig.ready = false; }

static bool vig_buffer(void) {
    if (vig.buf) return true;
    if (vig.failed) return false;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = (VkDeviceSize)VIG_LEVELS * VIG_SIZE * VIG_SIZE * 4,
                             .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
    VmaAllocationInfo info;
    if (vmaCreateBuffer(g_vk.vma, &bi, &ai, &vig.buf, &vig.mem, &info) != VK_SUCCESS || !info.pMappedData) {
        vig.failed = true;
        vig.buf = VK_NULL_HANDLE;
        return false;
    }
    uint8_t *p = info.pMappedData;
    for (int l = 0; l < VIG_LEVELS; l++) {
        // level l + 1: clear within `clear` (tangents: 1.0 is 45 degrees off the line of sight), dark from clear + 0.5;
        // the strongest leaves about 30 degrees around the gaze clear
        float s = (float)(l + 1) / VIG_LEVELS, clear = 1.9f - 1.35f * s;
        for (int y = 0; y < VIG_SIZE; y++)
            for (int x = 0; x < VIG_SIZE; x++) {
                float tx = ((x + 0.5f) / VIG_SIZE * 2 - 1) * VIG_SPAN, ty = ((y + 0.5f) / VIG_SIZE * 2 - 1) * VIG_SPAN;
                float r = sqrtf(tx * tx + ty * ty), t = (r - clear) / 0.5f;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                uint8_t *q = p + 4 * ((size_t)l * VIG_SIZE * VIG_SIZE + (size_t)y * VIG_SIZE + x);
                q[0] = q[1] = q[2] = 0;
                q[3] = (uint8_t)(255 * t * t * (3 - 2 * t) + 0.5f);
            }
    }
    vmaFlushAllocation(g_vk.vma, vig.mem, 0, VK_WHOLE_SIZE);
    return true;
}

// in a headset frame in play: the level now (from the option and the sticks), and its image into the swapchain when
// it changed
static void vignette(VkCommandBuffer cb) {
    int opt = vr_opt(VR_OPT_VIGNETTE);
    static const float strength[] = {0, 0.55f, 0.8f, 1.0f};
    float want = opt > 0 && opt <= 3 ? strength[opt] * vr_stick_motion() : 0;
    // (in: a time constant of 0.15 s; out: 0.3 s, nearly gone in a second)
    float dt = g.period > 0 ? (float)g.period / 1e9f : 1 / 90.0f, k = want > vig.level ? dt / 0.15f : dt / 0.3f;
    vig.level += (want - vig.level) * (k > 1 ? 1 : k);
    if (vig.level < 0.01f) vig.level = 0;
    int l = (int)(vig.level * VIG_LEVELS + 0.5f);   // (0: none)
    if (l > VIG_LEVELS) l = VIG_LEVELS;
    if (!l) {
        if (vig.shown > 0) vig.shown = 0;   // (not shown; its image is made again next time)
        return;
    }
    if (l == vig.shown || !vig_buffer()) return;
    swapchain *s = &g.sc[SC_VIGNETTE];
    if (s->h == XR_NULL_HANDLE && !make_swapchain(SC_VIGNETTE, (VkExtent2D){VIG_SIZE, VIG_SIZE}, VK_FORMAT_R8G8B8A8_UNORM) &&
        !make_swapchain(SC_VIGNETTE, (VkExtent2D){VIG_SIZE, VIG_SIZE}, VK_FORMAT_B8G8R8A8_UNORM))   // (black: either order)
        return;
    uint32_t idx = 0;
    if (!vr_xr_ok(xrAcquireSwapchainImage(s->h, NULL, &idx), "xrAcquireSwapchainImage")) return;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
    if (!vr_xr_ok(xrWaitSwapchainImage(s->h, &wi), "xrWaitSwapchainImage")) return;
    VkImage dst = s->images[idx];
    barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy rg = {.bufferOffset = (VkDeviceSize)(l - 1) * VIG_SIZE * VIG_SIZE * 4,
                            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {VIG_SIZE, VIG_SIZE, 1}};
    rec_CmdCopyBufferToImage(cb, vig.buf, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    fr.release[SC_VIGNETTE] = true;
    vig.shown = l;
}

// ---- the controllers shown where Link's arms are not the player's hands (menus, the virtual screen, loading, third
// person; Options > VR > Show the controllers): a small disc in each hand's grip space, a quad over the other layers.
// Its image (a pale blue disc with a white rim) is made once into a static swapchain ----
#define MARKER_SIZE 64
#define MARKER_M 0.06f    // its width (m)
static struct {
    VkBuffer buf;
    VmaAllocation mem;
    bool failed, made, ready;
    uint64_t frames;      // headset frames they were in
} mk;

XrSpace vr_hand_space(int hand);   // vr_input.c
bool vr_first_person_now(void);    // vr_camera.c
uint64_t vr_marker_frames(void) { return mk.frames; }

static void marker_dropped(void) { mk.made = mk.ready = false; }

static bool markers_wanted(void) {
    if (!vr_opt_on(VR_OPT_HAND_MARKERS) || !mk.ready || g.sc[SC_MARKER].h == XR_NULL_HANDLE) return false;
    return !fr.view.world || !vr_first_person_now();   // (in first person play his arms follow the controllers)
}

static void marker_image(VkCommandBuffer cb) {
    if (mk.made || mk.failed || !vr_opt_on(VR_OPT_HAND_MARKERS)) return;
    if (!mk.buf) {
        VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = MARKER_SIZE * MARKER_SIZE * 4,
                                 .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO,
                                      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT};
        VmaAllocationInfo info;
        if (vmaCreateBuffer(g_vk.vma, &bi, &ai, &mk.buf, &mk.mem, &info) != VK_SUCCESS || !info.pMappedData) {
            mk.failed = true;
            mk.buf = VK_NULL_HANDLE;
            return;
        }
        uint8_t *p = info.pMappedData;
        for (int y = 0; y < MARKER_SIZE; y++)
            for (int x = 0; x < MARKER_SIZE; x++) {
                float dx = (x + 0.5f) / MARKER_SIZE * 2 - 1, dy = (y + 0.5f) / MARKER_SIZE * 2 - 1, r = sqrtf(dx * dx + dy * dy);
                uint8_t *q = p + 4 * (y * MARKER_SIZE + x);
                float edge = (1 - r) * MARKER_SIZE / 2;   // pixels inside the rim (anti-aliased by one)
                float a = edge < 0 ? 0 : edge > 1 ? 1 : edge;
                bool rim = r > 0.78f;
                // (the same bytes in R G B A and B G R A: pale blue fill is (150, 200, 255), so the order is set below)
                q[0] = rim ? 255 : 150, q[1] = rim ? 255 : 200, q[2] = 255;
                q[3] = (uint8_t)(a * (rim ? 255 : 170) + 0.5f);
            }
        vmaFlushAllocation(g_vk.vma, mk.mem, 0, VK_WHOLE_SIZE);
    }
    swapchain *s = &g.sc[SC_MARKER];
    if (s->h == XR_NULL_HANDLE && !make_swapchain(SC_MARKER, (VkExtent2D){MARKER_SIZE, MARKER_SIZE}, VK_FORMAT_R8G8B8A8_UNORM)) {
        // (only B G R A offered: the fill's red and blue swapped into place)
        if (!make_swapchain(SC_MARKER, (VkExtent2D){MARKER_SIZE, MARKER_SIZE}, VK_FORMAT_B8G8R8A8_UNORM)) {
            mk.failed = true;
            return;
        }
        uint8_t *p = NULL;
        VmaAllocationInfo info;
        vmaGetAllocationInfo(g_vk.vma, mk.mem, &info);
        p = info.pMappedData;
        for (int k = 0; k < MARKER_SIZE * MARKER_SIZE; k++) {
            uint8_t t = p[4 * k];
            p[4 * k] = p[4 * k + 2], p[4 * k + 2] = t;
        }
        vmaFlushAllocation(g_vk.vma, mk.mem, 0, VK_WHOLE_SIZE);
    }
    uint32_t idx = 0;
    if (!vr_xr_ok(xrAcquireSwapchainImage(s->h, NULL, &idx), "xrAcquireSwapchainImage")) return;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
    if (!vr_xr_ok(xrWaitSwapchainImage(s->h, &wi), "xrWaitSwapchainImage")) return;
    VkImage dst = s->images[idx];
    barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy rg = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {MARKER_SIZE, MARKER_SIZE, 1}};
    rec_CmdCopyBufferToImage(cb, mk.buf, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    fr.release[SC_MARKER] = true;
    mk.made = true;
}

// src (in layout src_layout: COLOR_ATTACHMENT_OPTIMAL for the window image, GENERAL for exec.c's images) into
// swapchain i, made at its size in the sRGB twin of its format: acquired and waited here, released after the submission
static bool put_region(int i, VkCommandBuffer cb, VkImage src, VkImageLayout src_layout, VkOffset2D off, VkExtent2D ext,
                       VkFormat fmt);
static bool put_into(int i, VkCommandBuffer cb, VkImage src, VkImageLayout src_layout, VkExtent2D ext, VkFormat fmt) {
    return put_region(i, cb, src, src_layout, (VkOffset2D){0, 0}, ext, fmt);
}

// (the same, the part of src at off, of the size ext)
static bool put_region(int i, VkCommandBuffer cb, VkImage src, VkImageLayout src_layout, VkOffset2D off, VkExtent2D ext,
                       VkFormat fmt) {
    if (eye_of(i) >= 0) {   // an eye: whichever of its two swapchains is in this format at this size, else the other one
        int a = eye_of(i) == 0 ? SC_LEFT : SC_RIGHT, b = eye_of(i) == 0 ? SC_LEFT2 : SC_RIGHT2;
#define FITS(k) (g.sc[k].h != XR_NULL_HANDLE && g.sc[k].format == fmt && g.sc[k].extent.width == ext.width && \
                 g.sc[k].extent.height == ext.height)
        i = FITS(a) ? a : FITS(b) ? b : g.sc[a].h == XR_NULL_HANDLE ? a : g.sc[b].h == XR_NULL_HANDLE ? b :
            g.eye_sc[eye_of(a)] == a ? b : a;   // (neither: the one not shown now is made again)
#undef FITS
    }
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
    VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffset = {off.x, off.y, 0},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {ext.width, ext.height, 1}};
    rec_CmdCopyImage(cb, src, read, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    if (eye_of(i) >= 0) arc_draw(cb, dst, ext, fmt, &pres);
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

static bool put_depth_into(int i, VkCommandBuffer cb, VkImage src, VkExtent2D ext, VkFormat fmt, float near_z,
                           float far_z) {
    if (!vr_depth_enabled() || !src || !ext.width || !ext.height || !(near_z > 0) || !(far_z > near_z)) return false;
    swapchain *s = &g.sc[i];
    if (s->h != XR_NULL_HANDLE &&
        (s->extent.width != ext.width || s->extent.height != ext.height || s->format != fmt))
        drop_swapchain(i);
    if (s->h == XR_NULL_HANDLE && !make_depth_swapchain(i, ext, fmt)) return false;
    uint32_t idx = 0;
    if (!vr_xr_ok(xrAcquireSwapchainImage(s->h, NULL, &idx), "xrAcquireSwapchainImage(depth)")) return false;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
    if (!vr_xr_ok(xrWaitSwapchainImage(s->h, &wi), "xrWaitSwapchainImage(depth)")) return false;
    VkImage dst = s->images[idx];
    barrier_aspect(cb, src, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    barrier_aspect(cb, dst, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                      .dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                      .extent = {ext.width, ext.height, 1}};
    rec_CmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    barrier_aspect(cb, dst, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    barrier_aspect(cb, src, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    fr.release[i] = true;
    fr.depth_eye = depth_eye_of(i);
    fr.depth_extent = ext;
    fr.depth_near = near_z;
    fr.depth_far = far_z;
    return true;
}

// exec.c (vr_hud_prepare): this frame's 3D scene without the HUD, and the HUD alone (false: not made this frame)
bool vk_vr_images(VkImage *scene, VkFormat *scene_fmt, VkImage *hud, VkFormat *hud_fmt, VkExtent2D *ext);
bool vk_vr_depth(VkImage *depth, VkFormat *depth_fmt, VkExtent2D *ext, float *near_z, float *far_z);

bool vr_hud_wanted(void) { return g.hud_on && vr_world_view(); }

static void record(VkCommandBuffer cb, VkImage src, VkExtent2D ext, VkFormat fmt) {
    if (g.session == XR_NULL_HANDLE) return;
    wait_previous_end();
    poll_events();
    if (!g.running || g.session == XR_NULL_HANDLE) return;
    vr_window_aspect((float)ext.width / (float)ext.height);
    memset(fr.release, 0, sizeof fr.release);
    fr.view = pres;
    fr.aspect = (float)ext.height / (float)ext.width;
    fr.hud = false;
    fr.depth_eye = -1;
    arc.n = pres.world ? vr_bow_arc(arc.pts, ARC_MAX) : 0;
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
    if (pres.world) {
        int de = pres.eye == 1 ? 1 : 0;
        g.eye_depth_ready[de] = false;
        VkImage depth = VK_NULL_HANDLE;
        VkFormat depth_fmt = VK_FORMAT_UNDEFINED;
        VkExtent2D depth_ext = {0, 0};
        float near_z = 0, far_z = 0;
        if (vr_depth_enabled() && vk_vr_depth(&depth, &depth_fmt, &depth_ext, &near_z, &far_z)) {
            float scene_ar = (float)sext.width / (float)sext.height;
            float depth_ar = (float)depth_ext.width / (float)depth_ext.height;
            if (fabsf(scene_ar - depth_ar) <= 0.01f * fmaxf(scene_ar, depth_ar)) {
                int ds = de ? SC_DEPTH_RIGHT : SC_DEPTH_LEFT;
                put_depth_into(ds, cb, depth, depth_ext, depth_fmt, near_z, far_z);
            } else {
                static bool said;
                if (!said) LOG("[vr] composition depth skipped: scene %ux%u and depth %ux%u have different aspect ratios\n",
                               sext.width, sext.height, depth_ext.width, depth_ext.height);
                said = true;
            }
        }
    }
    if (pres.world && pres.eye == 0) {  // stereo, the left frame: its eye's image only, the headset's frame comes next
        put_into(SC_LEFT, cb, scene, sl, sext, scene_fmt);
        g.world_frames++;
        return;
    }
    if (!begin_headset_frame()) return;
    marker_image(cb);
    if (pres.world) {  // the right frame (stereo) or the only one (mono: both eyes see the left swapchain's picture)
        if (put_into(pres.eye == 1 ? SC_RIGHT : SC_LEFT, cb, scene, sl, sext, scene_fmt)) fr.kind = 2;
        vignette(cb);
        if (split && put_into(SC_HUD, cb, hud, VK_IMAGE_LAYOUT_GENERAL, sext, hud_fmt)) {
            fr.hud = true;
            fr.hud_aspect = (float)sext.height / (float)sext.width;
        }
        // the performance overlay (drawn on the window's picture, which the eyes do not get): that part of it, apart
        extern bool menu_perf_rect(int r[4]);
        int pr[4];
        fr.perf = false;
        bool have_perf = menu_perf_rect(pr);
        if (have_perf) {   // (rounded up, kept at its right edge: its width moves with its digits, the swapchain not)
            int right = pr[0] + pr[2];
            pr[2] = (pr[2] + 63) / 64 * 64, pr[3] = (pr[3] + 31) / 32 * 32;
            pr[0] = right - pr[2];
        }
        if (have_perf && pr[0] >= 0 && pr[1] >= 0 && pr[0] + pr[2] <= (int)ext.width && pr[1] + pr[3] <= (int)ext.height &&
            put_region(SC_PERF, cb, src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, (VkOffset2D){pr[0], pr[1]},
                       (VkExtent2D){(uint32_t)pr[2], (uint32_t)pr[3]}, fmt)) {
            fr.perf = true;
            fr.perf_aspect = (float)pr[3] / (float)pr[2];
            perf_frames++;
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
        if (eye_of(i) >= 0) {
            g.eye_view[eye_of(i)] = fr.view;
            g.eye_ready[eye_of(i)] = true;
            g.eye_sc[eye_of(i)] = i;
        }
        if (depth_eye_of(i) >= 0 && fr.depth_eye == depth_eye_of(i)) {
            int e = depth_eye_of(i);
            g.eye_depth_ready[e] = true;
            g.eye_depth_sc[e] = i;
            g.eye_depth_extent[e] = fr.depth_extent;
            g.eye_depth_near[e] = fr.depth_near;
            g.eye_depth_far[e] = fr.depth_far;
        }
        if (i == SC_HUD) g.hud_ready = true;
        if (i == SC_VIGNETTE) vig.ready = true;
        if (i == SC_MARKER) mk.ready = true;
    }
    if (fr.active) {
        XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD}, hudq = {XR_TYPE_COMPOSITION_LAYER_QUAD},
                               vigq = {XR_TYPE_COMPOSITION_LAYER_QUAD}, mkq[2], perfq = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView views[2];
        XrCompositionLayerDepthInfoKHR depths[2];
        const XrCompositionLayerBaseHeader *layers[6];
        uint32_t n = 0;
        bool mono = fr.view.world && fr.view.eye < 0;
        if (fr.kind == 2 && (mono ? g.eye_ready[0] : g.eye_ready[0] && g.eye_ready[1])) {
            bool with_depth = vr_depth_enabled() &&
                              (mono ? g.eye_depth_ready[0] : g.eye_depth_ready[0] && g.eye_depth_ready[1]);
            static bool depth_reported;
            if (with_depth && !depth_reported) {
                LOG("[vr] composition depth attached to both projection views\n");
                depth_reported = true;
            }
            for (int i = 0; i < 2; i++) {
                int sc = mono ? 0 : i;  // (mono: the left swapchain's picture, at the head's pose, for both)
                views[i] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                views[i].pose = g.eye_view[sc].pose;
                views[i].fov = g.eye_view[sc].fov;
                int k = g.eye_sc[sc];
                views[i].subImage.swapchain = g.sc[k].h;
                views[i].subImage.imageRect =
                    (XrRect2Di){{0, 0}, {(int32_t)g.sc[k].extent.width, (int32_t)g.sc[k].extent.height}};
                if (with_depth) {
                    depths[i] = (XrCompositionLayerDepthInfoKHR){XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR};
                    depths[i].subImage.swapchain = g.sc[g.eye_depth_sc[sc]].h;
                    depths[i].subImage.imageRect =
                        (XrRect2Di){{0, 0}, {(int32_t)g.eye_depth_extent[sc].width, (int32_t)g.eye_depth_extent[sc].height}};
                    depths[i].minDepth = 0.0f;
                    depths[i].maxDepth = 1.0f;
                    depths[i].nearZ = g.eye_depth_near[sc];
                    depths[i].farZ = g.eye_depth_far[sc];
                    views[i].next = &depths[i];
                }
            }
            proj.space = g.local;
            proj.viewCount = 2;
            proj.views = views;
            layers[n++] = (const XrCompositionLayerBaseHeader *)&proj;
            if (vig.ready && vig.shown > 0) {   // the comfort vignette: over the scene, under the HUD
                vigq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                  XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                vigq.space = g.view;
                vigq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                vigq.pose = (XrPosef){{0, 0, 0, 1}, {0, 0, -1}};
                vigq.size = (XrExtent2Df){2 * VIG_SPAN, 2 * VIG_SPAN};
                vigq.subImage.swapchain = g.sc[SC_VIGNETTE].h;
                vigq.subImage.imageRect = (XrRect2Di){{0, 0}, {VIG_SIZE, VIG_SIZE}};
                layers[n++] = (const XrCompositionLayerBaseHeader *)&vigq;
                vig.frames++;
            }
            if (fr.hud && g.hud_ready) {  // the HUD, with the head, a little below the line of sight
                hudq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                  XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                // its size and distance (the Options page, else SAVAGEWIND_VR_HUD_WIDTH / _DISTANCE); with the gaze
                // (the head's space) or ahead of the body (the room's, at the head, turned the way it was centred)
                int hd = vr_opt(VR_OPT_HUD_DISTANCE), hs = vr_opt(VR_OPT_HUD_SIZE);
                float dist = hd ? hd / 100.0f : g.hud_dist, w = g.hud_w * (hs ? hs / 100.0f : 1.0f);
                float heading;
                extern bool vr_neutral_heading(float *heading);
                if (vr_opt_on(VR_OPT_HUD_GAZE) || !vr_neutral_heading(&heading)) {
                    hudq.space = g.view;
                    hudq.pose = (XrPosef){{0, 0, 0, 1}, {0, -0.05f, -dist}};
                } else {
                    const XrVector3f *a = &views[0].pose.position, *b = &views[1].pose.position;
                    vr_quat q = vr_quat_yaw(heading);
                    vr_vec3 ahead = vr_quat_rotate(q, vr_v3(0, -0.05f - 0.15f, -dist));   // (a little lower: below the gaze)
                    hudq.space = g.local;
                    hudq.pose = (XrPosef){{q.x, q.y, q.z, q.w}, {(a->x + b->x) / 2 + ahead.x, (a->y + b->y) / 2 + ahead.y,
                                                                 (a->z + b->z) / 2 + ahead.z}};
                }
                hudq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                hudq.subImage.swapchain = g.sc[SC_HUD].h;
                hudq.subImage.imageRect =
                    (XrRect2Di){{0, 0}, {(int32_t)g.sc[SC_HUD].extent.width, (int32_t)g.sc[SC_HUD].extent.height}};
                hudq.size = (XrExtent2Df){w, w * fr.hud_aspect};
                layers[n++] = (const XrCompositionLayerBaseHeader *)&hudq;
            }
        } else if (fr.kind == 1) {
            quad.space = g.local;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = g.sc[SC_SCREEN].h;
            int32_t W = (int32_t)g.sc[SC_SCREEN].extent.width, H = (int32_t)g.sc[SC_SCREEN].extent.height;
            int32_t bx = screen_box[0], by = screen_box[1], bw = screen_box[2], bh = screen_box[3];
            if (bw < 16 || bh < 16 || bx < 0 || by < 0 || bx + bw > W || by + bh > H) bx = 0, by = 0, bw = W, bh = H;
            quad.subImage.imageRect = (XrRect2Di){{bx, by}, {bw, bh}};
            quad.pose = (XrPosef){{0, 0, 0, 1}, {0, 0, -g.screen_dist}};
            quad.size = (XrExtent2Df){g.screen_w, g.screen_w * (float)bh / (float)bw};
            layers[n++] = (const XrCompositionLayerBaseHeader *)&quad;
        }
        if (fr.kind == 2 && fr.perf) {   // the performance overlay: with the head, up and to the right, 1 m away
            perfq.space = g.view;
            perfq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            perfq.pose = (XrPosef){{0, 0, 0, 1}, {0.42f, 0.30f, -1.0f}};
            perfq.size = (XrExtent2Df){0.36f, 0.36f * fr.perf_aspect};
            perfq.subImage.swapchain = g.sc[SC_PERF].h;
            perfq.subImage.imageRect = (XrRect2Di){{0, 0}, {(int32_t)g.sc[SC_PERF].extent.width, (int32_t)g.sc[SC_PERF].extent.height}};
            layers[n++] = (const XrCompositionLayerBaseHeader *)&perfq;
        }
        if (n && markers_wanted()) {   // the controllers, over everything
            for (int h = 0; h < 2; h++) {
                XrSpace sp = vr_hand_space(h);
                if (sp == XR_NULL_HANDLE) continue;
                mkq[h] = (XrCompositionLayerQuad){XR_TYPE_COMPOSITION_LAYER_QUAD};
                mkq[h].layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT |
                                    XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                mkq[h].space = sp;
                mkq[h].eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                // (facing back along the controller, toward the player; a little ahead of the grip, over the knuckles)
                mkq[h].pose = (XrPosef){{0, 0, 0, 1}, {0, 0, -0.03f}};
                mkq[h].size = (XrExtent2Df){MARKER_M, MARKER_M};
                mkq[h].subImage.swapchain = g.sc[SC_MARKER].h;
                mkq[h].subImage.imageRect = (XrRect2Di){{0, 0}, {MARKER_SIZE, MARKER_SIZE}};
                layers[n++] = (const XrCompositionLayerBaseHeader *)&mkq[h];
            }
            mk.frames++;
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
XrSpace vr_xr_view(void) { return g.view; }

// how high LOCAL's origin is above the floor (STAGE), for Standing (vr_camera.c); false while the runtime does not
// know (no STAGE space, or not tracked yet). Asked again each time: the floor can be set up while playing
bool vr_local_height(float *y) {
    if (g.stage == XR_NULL_HANDLE || g.local == XR_NULL_HANDLE || !g.last_display) return g.local_y_ok && (*y = g.local_y, true);
    XrSpaceLocation l = {XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace(g.local, g.stage, g.last_display, &l)) && (l.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        g.local_y = l.pose.position.y, g.local_y_ok = true;
    if (g.local_y_ok) *y = g.local_y;
    return g.local_y_ok;
}

// when the frame the game makes now should be shown: two periods after the last one begun (one being drawn, one shown)
XrTime vr_next_display_time(void) { return g.last_display ? g.last_display + 2 * g.period : 0; }

uint64_t vr_world_frames(void) { return g.world_frames; }

void vr_stats(uint64_t *frames, uint64_t *rendered, int *state) {
    *frames = g.frames;
    *rendered = g.rendered;
    *state = (int)g.state;
}

// ---- the desktop window: one eye, not both in turn ----
// In stereo the game draws the left eye, then the right one, each into the window image that is also presented: the
// window showed them in turn (it flickered). The left frame's window image is kept (after its copy to the headset);
// the right frame's, once copied to the headset, is overwritten with it, so the window shows the left eye only.
// SAVAGEWIND_VR_WINDOW=both: as before.
static struct {
    VkImage img;
    VmaAllocation mem;
    VkExtent2D ext;
    VkFormat fmt;
    bool valid, written;   // a left frame is in it; it has been written once (its layout is TRANSFER_SRC then)
    uint64_t shown;        // right frames that showed it instead
} win;

uint64_t vr_window_left_frames(void) { return win.shown; }

static void window_eye(VkCommandBuffer cb, VkImage src, VkExtent2D ext, VkFormat fmt) {
    static int both = -1;
    if (both < 0) {
        const char *e = getenv("SAVAGEWIND_VR_WINDOW");
        both = e && !strcmp(e, "both");
    }
    if (both || !pres.world || pres.eye < 0) {
        win.valid = false;
        return;
    }
    if (win.img && (win.ext.width != ext.width || win.ext.height != ext.height || win.fmt != fmt)) {
        vk_defer_image(win.img, win.mem);
        win.img = VK_NULL_HANDLE, win.valid = win.written = false;
    }
    if (!win.img) {
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
                                .extent = {ext.width, ext.height, 1}, .mipLevels = 1, .arrayLayers = 1,
                                .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO};
        if (vmaCreateImage(g_vk.vma, &ci, &ai, &win.img, &win.mem, NULL) != VK_SUCCESS) {
            win.img = VK_NULL_HANDLE;
            return;
        }
        win.ext = ext, win.fmt = fmt, win.valid = win.written = false;
    }
    VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {ext.width, ext.height, 1}};
    if (pres.eye == 0) {   // the left frame: keep it
        barrier(cb, src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        barrier(cb, win.img, win.written ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        rec_CmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, win.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        barrier(cb, win.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        barrier(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        win.valid = win.written = true;
    } else if (win.valid) {   // the right frame, already copied to the headset: the window gets the left one
        barrier(cb, src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        rec_CmdCopyImage(cb, win.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, src, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
        barrier(cb, src, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        win.shown++;
    }
}

void vr_frame_record(VkCommandBuffer cb, VkImage src, VkExtent2D ext, VkFormat fmt) {
    record(cb, src, ext, fmt);
    if (g.session != XR_NULL_HANDLE && g.running) window_eye(cb, src, ext, fmt);
}
