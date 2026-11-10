// VR mode (OpenXR). Built only with `make VR=1` (SW_VR); a plain build has none of runtime/vr/ and behaves as before.
// Turned on at run time with SAVAGEWIND_VR=1. Without an OpenXR runtime, or when it fails, the game stays flat and the
// log says why ([vr] lines). The plan is in docs/VR-PLAN.md, the state in docs/VR.md.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Reads SAVAGEWIND_VR and, when it asks for VR, opens the OpenXR runtime and finds the headset. Once, early in main().
void vr_init(void);

// VR was asked for, the runtime and the headset answered, and nothing has failed since.
bool vr_enabled(void);

// What the log and `report` say about VR: "off", "no runtime ...", "runtime <name>, headset <name>" ...
const char *vr_status_text(void);

// Frames begun / frames with an image for the headset, and the session's XrSessionState (0 before one)
void vr_stats(uint64_t *frames, uint64_t *rendered, int *state);

#ifdef VK_VERSION_1_0
// runtime/nvn/vulkan/device.c (runtime/vr/xr_vulkan.c): each falls back to the plain Vulkan call when VR is off
VkResult vr_vk_create_instance(const VkInstanceCreateInfo *ci, VkInstance *out);
VkPhysicalDevice vr_vk_physical_device(VkInstance inst);  // the headset's GPU, or VK_NULL_HANDLE
VkResult vr_vk_create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, VkDevice *out);
void vr_vk_session_start(VkInstance inst, VkPhysicalDevice pd, VkDevice dev, uint32_t qfam, uint32_t qindex);
// vk_frame_end: copy the window image (a colour attachment, left one) to the headset's virtual screen, recorded in cb
void vr_frame_record(VkCommandBuffer cb, VkImage img, VkExtent2D ext, VkFormat fmt);
// right after the submission of cb: the OpenXR frame ends, in order with the queue
void vr_frame_after_submit(void);
#endif
