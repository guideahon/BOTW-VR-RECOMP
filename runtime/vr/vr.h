// VR mode (OpenXR). Built only with `make VR=1` (SW_VR); a plain build has none of runtime/vr/ and behaves as before.
// Turned on at run time with SAVAGEWIND_VR=1. Without an OpenXR runtime, or when it fails, the game stays flat and the
// log says why ([vr] lines). The plan is in docs/VR.md.
#pragma once
#include <stdbool.h>

// Reads SAVAGEWIND_VR and, when it asks for VR, checks that an OpenXR runtime answers. Once, early in main().
void vr_init(void);

// VR was asked for and an OpenXR runtime answered (later phases: and a session runs).
bool vr_enabled(void);

// What the log and `report` say about VR: "off", "no runtime", "runtime <name>" ...
const char *vr_status_text(void);
