// VR mode, phase 1: the OpenXR session on the game's Vulkan device, and the flat game on a virtual screen (a quad
// layer) in the headset. runtime/nvn/vulkan/device.c calls in here:
//   create_instance  -> vr_vk_create_instance   (XR_KHR_vulkan_enable2: the runtime adds what it needs)
//   pick_device      -> vr_vk_physical_device    (the headset's GPU)
//   create_device    -> vr_vk_create_device, then vr_vk_session_start
//   vk_frame_end     -> vr_frame_record (before the window image goes to present) and vr_frame_after_submit
//
// Order on the queue: OpenXR may use the session's queue (g_vk.queue) inside xrReleaseSwapchainImage and xrEndFrame,
// and wants the app's writes to an image submitted before the release. Both run through rec_custom, on the recorder
// thread, after the submission that copies into the image: in order with every other use of the queue. The next
// frame's xrWaitFrame waits until they have run, so one OpenXR frame is in flight at a time.
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

static PFN_xrGetVulkanGraphicsRequirements2KHR GetRequirements;
static PFN_xrCreateVulkanInstanceKHR CreateInstance;
static PFN_xrGetVulkanGraphicsDevice2KHR GetDevice;
static PFN_xrCreateVulkanDeviceKHR CreateDevice;

static struct {
    XrSession session;
    XrSpace local;
    XrSessionState state;
    bool running;
    XrSwapchain swapchain;
    VkFormat sc_format;
    VkExtent2D sc_extent;
    uint32_t n_images;
    VkImage images[MAX_IMAGES];
    VkPhysicalDevice phys;
    float screen_w, screen_dist;  // the virtual screen: its width and distance (m)
    uint64_t frames, rendered, world_frames;
    XrTime last_display;          // the last frame's predicted display time and the display's period
    XrDuration period;
} g;

// the frame between vr_frame_record and its end on the recorder thread
static struct {
    bool active;   // xrBeginFrame done, xrEndFrame not yet
    bool render;   // an image was acquired and copied into
    XrTime display;
    float aspect;  // height / width of what was copied
    bool world;    // a projection layer of the head's view (vr_camera.c), else the virtual screen
    XrPosef pose;
    XrFovf fov;
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
    // the virtual screen (SAVAGEWIND_VR_SCREEN_WIDTH / _DISTANCE, metres): 3 m wide, 2.5 m ahead, at eye height
    g.screen_w = env_float("SAVAGEWIND_VR_SCREEN_WIDTH", 3.0f, 0.2f, 50.0f);
    g.screen_dist = env_float("SAVAGEWIND_VR_SCREEN_DISTANCE", 2.5f, 0.3f, 50.0f);
    LOG("[vr] session on queue family %u index %u; virtual screen %.1f m wide at %.1f m\n", qfam, qindex, g.screen_w,
        g.screen_dist);
}

// ---- session states ----
static void drop_swapchain(void) {
    if (g.swapchain == XR_NULL_HANDLE) return;
    rec_drain();
    vkDeviceWaitIdle(g_vk.dev);  // (only on a resize or the end of the session: its images may still be in use)
    xrDestroySwapchain(g.swapchain);
    g.swapchain = XR_NULL_HANDLE;
    g.n_images = 0;
}

static void end_session(const char *why) {
    drop_swapchain();
    if (g.local != XR_NULL_HANDLE) xrDestroySpace(g.local);
    if (g.session != XR_NULL_HANDLE) xrDestroySession(g.session);
    g.local = XR_NULL_HANDLE;
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

// ---- the swapchain: the window image's size, in the sRGB twin of its format (its bytes are already gamma-encoded,
// so they are copied as they are and the runtime reads them as sRGB) ----
static VkFormat srgb_twin(VkFormat f) {
    switch (f) {
    case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
    case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB;
    default: return f;
    }
}

static bool make_swapchain(VkExtent2D ext, VkFormat src) {
    VkFormat want = srgb_twin(src);
    uint32_t n = 0;
    int64_t formats[64];
    if (!vr_xr_ok(xrEnumerateSwapchainFormats(g.session, 64, &n, formats), "xrEnumerateSwapchainFormats")) return false;
    bool offered = false;
    for (uint32_t i = 0; i < n; i++) offered |= formats[i] == want;
    if (!offered) {
        static bool said;
        if (!said) LOG("[vr] the runtime offers no swapchain in format %d: no virtual screen\n", (int)want);
        said = true;
        return false;
    }
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO, NULL, 0,
                                XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT, want, 1,
                                ext.width, ext.height, 1, 1, 1};
    if (!vr_xr_ok(xrCreateSwapchain(g.session, &ci, &g.swapchain), "xrCreateSwapchain")) return false;
    XrSwapchainImageVulkanKHR imgs[MAX_IMAGES];
    for (int i = 0; i < MAX_IMAGES; i++) imgs[i] = (XrSwapchainImageVulkanKHR){XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
    if (!vr_xr_ok(xrEnumerateSwapchainImages(g.swapchain, MAX_IMAGES, &g.n_images, (XrSwapchainImageBaseHeader *)imgs),
                  "xrEnumerateSwapchainImages")) {
        xrDestroySwapchain(g.swapchain);
        g.swapchain = XR_NULL_HANDLE;
        return false;
    }
    for (uint32_t i = 0; i < g.n_images; i++) g.images[i] = imgs[i].image;
    g.sc_extent = ext;
    g.sc_format = src;
    LOG("[vr] virtual screen swapchain %ux%u, %u images\n", ext.width, ext.height, g.n_images);
    return true;
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
static void wait_previous_end(void) {
    pthread_mutex_lock(&end_lock);
    while (end_pending) pthread_cond_wait(&end_done, &end_lock);
    pthread_mutex_unlock(&end_lock);
}

void vr_frame_record(VkCommandBuffer cb, VkImage src, VkExtent2D ext, VkFormat fmt) {
    if (g.session == XR_NULL_HANDLE) return;
    wait_previous_end();
    poll_events();
    if (!g.running || g.session == XR_NULL_HANDLE) return;
    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    if (!vr_xr_ok(xrWaitFrame(g.session, NULL, &fs), "xrWaitFrame")) return;
    XrResult r = xrBeginFrame(g.session, NULL);
    if (XR_FAILED(r)) {
        vr_xr_ok(r, "xrBeginFrame");
        return;
    }
    fr.active = true;
    fr.render = false;
    fr.display = fs.predictedDisplayTime;
    g.last_display = fs.predictedDisplayTime;
    g.period = fs.predictedDisplayPeriod;
    g.frames++;
    if (!fs.shouldRender) return;
    if (g.swapchain != XR_NULL_HANDLE && (g.sc_extent.width != ext.width || g.sc_extent.height != ext.height ||
                                          g.sc_format != fmt))
        drop_swapchain();
    if (g.swapchain == XR_NULL_HANDLE && !make_swapchain(ext, fmt)) return;
    uint32_t idx = 0;
    if (!vr_xr_ok(xrAcquireSwapchainImage(g.swapchain, NULL, &idx), "xrAcquireSwapchainImage")) return;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, NULL, XR_INFINITE_DURATION};
    if (!vr_xr_ok(xrWaitSwapchainImage(g.swapchain, &wi), "xrWaitSwapchainImage")) return;
    VkImage dst = g.images[idx];
    // window image: drawn as a colour attachment, read here, then drawn on again (the menu) or presented
    barrier(cb, src, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {ext.width, ext.height, 1}};
    rec_CmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rg);
    // the runtime takes its images back as colour attachments (XR_KHR_vulkan_enable2)
    barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    barrier(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fr.render = true;
    fr.aspect = (float)ext.height / (float)ext.width;
    vr_window_aspect((float)ext.width / (float)ext.height);
    // in play: the image is the head's view; shown with the pose and frustum the camera was given
    fr.world = vr_world_view() && vr_camera_view(&fr.pose, &fr.fov);
    if (fr.world) g.world_frames++;
    g.rendered++;
}

static void end_frame(void *arg) {
    (void)arg;
    XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjection proj = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView views[2];
    const XrCompositionLayerBaseHeader *layers[1] = {(const XrCompositionLayerBaseHeader *)&quad};
    uint32_t n = 0;
    bool released = fr.render && vr_xr_ok(xrReleaseSwapchainImage(g.swapchain, NULL), "xrReleaseSwapchainImage");
    if (released && fr.world) {
        // phase 2: one picture for both eyes, at the head's pose and with the frustum it was drawn with
        for (int i = 0; i < 2; i++) {
            views[i] = (XrCompositionLayerProjectionView){XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            views[i].pose = fr.pose;
            views[i].fov = fr.fov;
            views[i].subImage.swapchain = g.swapchain;
            views[i].subImage.imageRect = (XrRect2Di){{0, 0}, {(int32_t)g.sc_extent.width, (int32_t)g.sc_extent.height}};
        }
        proj.space = g.local;
        proj.viewCount = 2;
        proj.views = views;
        layers[0] = (const XrCompositionLayerBaseHeader *)&proj;
        n = 1;
    } else if (released) {
        quad.space = g.local;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = g.swapchain;
        quad.subImage.imageRect = (XrRect2Di){{0, 0}, {(int32_t)g.sc_extent.width, (int32_t)g.sc_extent.height}};
        quad.pose = (XrPosef){{0, 0, 0, 1}, {0, 0, -g.screen_dist}};
        quad.size = (XrExtent2Df){g.screen_w, g.screen_w * fr.aspect};
        n = 1;
    }
    XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO, NULL, fr.display, XR_ENVIRONMENT_BLEND_MODE_OPAQUE, n, layers};
    vr_xr_ok(xrEndFrame(g.session, &ei), "xrEndFrame");
    fr.active = false;
    pthread_mutex_lock(&end_lock);
    end_pending = false;
    pthread_cond_broadcast(&end_done);
    pthread_mutex_unlock(&end_lock);
}

void vr_frame_after_submit(void) {
    if (!fr.active) return;
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
