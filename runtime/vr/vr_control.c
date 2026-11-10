// The `vr` control command (docs/CONTROL.md): what the VR mode is doing. Read only, so every player may run it.
#include <stdint.h>

#include "../control.h"
#include "vr.h"

static const char *state_name(int st) {
    static const char *const names[] = {"none", "idle", "ready", "synchronized", "visible", "focused", "stopping",
                                        "loss_pending", "exiting"};
    return st >= 0 && st < (int)(sizeof names / sizeof *names) ? names[st] : "unknown";
}

static void cmd_vr(int argc, char **argv, ControlReply *r) {
    (void)argv;
    if (argc != 1) {
        control_fail(r, "Usage: vr");
        return;
    }
    uint64_t frames = 0, rendered = 0;
    int st = 0;
    vr_stats(&frames, &rendered, &st);
    control_field_bool(r, "enabled", vr_enabled());
    control_field_str(r, "status", vr_status_text());
    control_field_str(r, "session", state_name(st));
    control_field_int(r, "frames", (int64_t)frames);
    control_field_int(r, "rendered", (int64_t)rendered);
    control_text(r, "VR %s: %s; session %s, %llu frames (%llu with an image)", vr_enabled() ? "on" : "off",
                 vr_status_text(), state_name(st), (unsigned long long)frames, (unsigned long long)rendered);
}

void vr_control_init(void) {
    static const ControlCmd cmds[] = {
        {"vr", "", "Show what the VR mode is doing", cmd_vr, CONTROL_PLAYER},
    };
    control_register(cmds, (int)(sizeof cmds / sizeof *cmds));
}
