// runtime/vr/vr.c against fake_xr through the OpenXR loader: VR stays off unless SAVAGEWIND_VR asks for it, finds
// the runtime when there is one, and falls back to flat (no crash, a status that says why) when there is none.
// vr_init runs once per process, so each case is its own run (make test-vr-loader):
//   test_vr_init off         SAVAGEWIND_VR unset or 0
//   test_vr_init runtime     SAVAGEWIND_VR=1, XR_RUNTIME_JSON = fake_xr's manifest
//   test_vr_init no-runtime  SAVAGEWIND_VR=1, XR_RUNTIME_JSON = a manifest that does not exist
#include <string.h>

#include "vr/vr.h"
#include "vr_test.h"

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "";
    VR_TEST(mode);
    vr_init();
    vr_init();  // a second call changes nothing
    if (!strcmp(mode, "off")) {
        CHECK(!vr_enabled());
        CHECK(!strcmp(vr_status_text(), "off"));
    } else if (!strcmp(mode, "runtime")) {
        CHECK(vr_enabled());
        CHECK(strstr(vr_status_text(), "runtime Savage Wind fake_xr") == vr_status_text());
    } else if (!strcmp(mode, "no-runtime")) {
        CHECK(!vr_enabled());
        CHECK(strstr(vr_status_text(), "no runtime") == vr_status_text());
    } else {
        fprintf(stderr, "usage: %s off | runtime | no-runtime\n", argv[0]);
        return 2;
    }
    return VR_TEST_END("test_vr_init");
}
