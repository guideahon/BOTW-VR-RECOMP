// What the files of runtime/vr/ share: the OpenXR instance and system vr.c keeps for the whole run, the session
// xr_vulkan.c runs, and the view vr_camera.c gives the game.
#pragma once
#include <stdbool.h>
#include <openxr/openxr.h>

XrInstance vr_xr_instance(void);  // XR_NULL_HANDLE unless vr_enabled()
XrSystemId vr_xr_system(void);

// "[vr] what failed: XR_ERROR_..." through the runtime's own names; false when r failed
bool vr_xr_ok(XrResult r, const char *what);

// VR stops for the rest of the run (the session was lost, a call failed): the game goes on flat
void vr_disable(const char *why);

// xr_vulkan.c: the session, its LOCAL space, and when the frame being made now should be shown (0 before frames)
bool vr_session_running(void);
XrSession vr_xr_session(void);
XrSpace vr_xr_local(void);
XrTime vr_next_display_time(void);

// vr_camera.c: the head pose and frustum the world camera was last given (false: none yet)
bool vr_camera_view(XrPosef *pose, XrFovf *fov);
void vr_window_aspect(float width_over_height);
