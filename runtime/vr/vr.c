// VR mode, phase 0: SAVAGEWIND_VR and a check that an OpenXR runtime answers (an instance created and dropped again).
// The session, the swapchains and the frame loop come in phase 1 (docs/VR.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openxr/openxr.h>

#ifdef VR_TEST_BUILD  // (tests/vr/test_vr_init.c builds this file alone, without the rest of the runtime)
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#include "../rt.h"
#endif
#include "vr.h"

static bool asked, enabled;
static char status[160] = "off";

bool vr_enabled(void) { return enabled; }
const char *vr_status_text(void) { return status; }

static bool env_on(const char *name) {
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0 && strcmp(v, "off") != 0;
}

void vr_init(void) {
    if (asked) return;
    asked = true;
    if (!env_on("SAVAGEWIND_VR")) return;

    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Savage Wind");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Savage Wind");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    XrInstance inst = XR_NULL_HANDLE;
    XrResult r = xrCreateInstance(&ci, &inst);
    if (XR_FAILED(r)) {
        snprintf(status, sizeof status, "no runtime (xrCreateInstance %d)", (int)r);
        LOG("[vr] VR asked for, but no OpenXR runtime answered (xrCreateInstance: %d): staying flat\n", (int)r);
        return;
    }
    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(inst, &ip);
    snprintf(status, sizeof status, "runtime %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
             XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    xrDestroyInstance(inst);
    enabled = true;
    LOG("[vr] OpenXR %s (phase 0: the game is still drawn flat)\n", status);
}
