// What the files of runtime/vr/ share: the OpenXR instance and system vr.c keeps for the whole run.
#pragma once
#include <stdbool.h>
#include <openxr/openxr.h>

XrInstance vr_xr_instance(void);  // XR_NULL_HANDLE unless vr_enabled()
XrSystemId vr_xr_system(void);

// "[vr] what failed: XR_ERROR_..." through the runtime's own names; false when r failed
bool vr_xr_ok(XrResult r, const char *what);

// VR stops for the rest of the run (the session was lost, a call failed): the game goes on flat
void vr_disable(const char *why);
