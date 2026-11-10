// VR mode: SAVAGEWIND_VR, the OpenXR instance and the headset (system). The instance is kept for the whole run;
// runtime/vr/xr_vulkan.c makes the Vulkan device and the session on it (docs/VR.md).
#include <stdbool.h>
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
#include "vr_internal.h"

#define VULKAN_ENABLE2 "XR_KHR_vulkan_enable2"

static bool asked, enabled;
static char status[192] = "off";
static XrInstance g_inst = XR_NULL_HANDLE;
static XrSystemId g_system = XR_NULL_SYSTEM_ID;

bool vr_enabled(void) { return enabled; }
const char *vr_status_text(void) { return status; }
XrInstance vr_xr_instance(void) { return enabled ? g_inst : XR_NULL_HANDLE; }
XrSystemId vr_xr_system(void) { return g_system; }

bool vr_xr_ok(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return true;
    char name[XR_MAX_RESULT_STRING_SIZE] = "";
    if (g_inst == XR_NULL_HANDLE || XR_FAILED(xrResultToString(g_inst, r, name))) snprintf(name, sizeof name, "%d", (int)r);
    LOG("[vr] %s: %s\n", what, name);
    return false;
}

void vr_disable(const char *why) {
    if (!enabled) return;
    enabled = false;
    snprintf(status, sizeof status, "stopped: %s", why);
    LOG("[vr] VR stopped (%s): the game goes on flat\n", why);
}

static bool env_on(const char *name) {
    const char *v = getenv(name);
    return v && *v && strcmp(v, "0") != 0 && strcmp(v, "off") != 0;
}

static bool runtime_has(const char *ext) {
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(NULL, 0, &n, NULL)) || !n) return false;
    XrExtensionProperties *p = calloc(n, sizeof *p);
    for (uint32_t i = 0; i < n; i++) p[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    bool has = false;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(NULL, n, &n, p)))
        for (uint32_t i = 0; i < n; i++) has |= !strcmp(p[i].extensionName, ext);
    free(p);
    return has;
}

static void stay_flat(const char *fmt, const char *detail) {
    snprintf(status, sizeof status, fmt, detail);
    LOG("[vr] VR asked for, but %s: staying flat\n", status);
    if (g_inst != XR_NULL_HANDLE) xrDestroyInstance(g_inst);
    g_inst = XR_NULL_HANDLE;
}

void vr_init(void) {
    if (asked) return;
    asked = true;
    if (!env_on("SAVAGEWIND_VR")) return;

    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Savage Wind");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Savage Wind");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    bool vulkan = runtime_has(VULKAN_ENABLE2);
    const char *ext[] = {VULKAN_ENABLE2};
    ci.enabledExtensionCount = vulkan ? 1 : 0;
    ci.enabledExtensionNames = ext;
    XrResult r = xrCreateInstance(&ci, &g_inst);
    if (XR_FAILED(r)) {
        char code[32];
        snprintf(code, sizeof code, "%d", (int)r);
        g_inst = XR_NULL_HANDLE;
        stay_flat("no runtime (xrCreateInstance %s)", code);
        return;
    }
    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(g_inst, &ip);
    char runtime[160];
    snprintf(runtime, sizeof runtime, "%s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
             XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
    if (!vulkan) {
        stay_flat("runtime %s has no XR_KHR_vulkan_enable2", runtime);
        return;
    }
    XrSystemGetInfo gi = {XR_TYPE_SYSTEM_GET_INFO, NULL, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    r = xrGetSystem(g_inst, &gi, &g_system);
    if (XR_FAILED(r)) {  // (XR_ERROR_FORM_FACTOR_UNAVAILABLE: the headset is off or not connected)
        char why[200];
        snprintf(why, sizeof why, "%s found no headset (xrGetSystem %d)", runtime, (int)r);
        stay_flat("runtime %s", why);
        return;
    }
    XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
    xrGetSystemProperties(g_inst, g_system, &sp);
    snprintf(status, sizeof status, "runtime %s, headset %s", runtime, sp.systemName);
    enabled = true;
    LOG("[vr] OpenXR %s\n", status);
}
