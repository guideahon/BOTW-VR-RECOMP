// The `vr` control command (docs/CONTROL.md): what the VR mode is doing. Read only, so every player may run it.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../control.h"
#include "vr.h"

static const char *state_name(int st) {
    static const char *const names[] = {"none", "idle", "ready", "synchronized", "visible", "focused", "stopping",
                                        "loss_pending", "exiting"};
    return st >= 0 && st < (int)(sizeof names / sizeof *names) ? names[st] : "unknown";
}

uint64_t vr_world_frames(void);  // runtime/vr/xr_vulkan.c
bool vr_input_state(uint64_t *buttons, int32_t sticks[4], uint64_t *syncs);  // runtime/vr/vr_input.c
uint64_t vr_rumble_pulses(void);  // runtime/vr/vr_input.c
uint64_t vr_arrows_aimed(void);   // runtime/vr/vr_skel.c
uint64_t vr_arc_frames(void);     // runtime/vr/xr_vulkan.c
uint64_t vr_weapons_moved(void);  // runtime/vr/vr_skel.c
uint64_t vr_swing_hits(void);     // runtime/vr/vr_skel.c
uint64_t vr_two_hand_frames(void); // runtime/vr/vr_skel.c
bool vr_two_hand_latched(void);    // runtime/vr/vr_skel.c
int vr_swing_sensor(void);         // runtime/vr/vr_skel.c
void vr_motion_state(uint32_t *attacks, uint32_t *slashes, uint32_t *stabs, float *power, bool *shield, bool *on);

static void cmd_vr(int argc, char **argv, ControlReply *r) {
    if (argc >= 3 && !strcmp(argv[1], "fnprof")) {   // (reverse engineering) vr fnprof start | stop <file>
        extern void fnprof_live_start(void);
        extern int fnprof_live_stop(const char *path);
        if (!strcmp(argv[2], "start")) { fnprof_live_start(); control_text(r, "fnprof on"); return; }
        if (argc == 4 && !strcmp(argv[2], "stop")) { control_text(r, "%d functions", fnprof_live_stop(argv[3])); return; }
        control_fail(r, "Usage: vr fnprof start | stop <file>");
        return;
    }
    if (argc == 4 && !strcmp(argv[1], "rumble")) {   // (a test: the game's rumble path, as vibration.c feeds it)
        extern void vr_rumble(int hand, float amplitude);
        vr_rumble(argv[2][0] == 'r', (float)atof(argv[3]));
        control_text(r, "rumble %s %s", argv[2], argv[3]);
        return;
    }
    if (argc == 2 && !strcmp(argv[1], "recenter")) {
        vr_recenter();
        control_text(r, "recentring at the next camera update");
        return;
    }
    if (argc != 1) {
        control_fail(r, "Usage: vr [recenter]");
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
    control_field_int(r, "world_frames", (int64_t)vr_world_frames());
    control_field_int(r, "camera_updates", (int64_t)vr_camera_updates());
    control_field_bool(r, "world_view", vr_world_view());
    {   // events (cutscenes): the one playing and whether the last camera was set in first person
        extern const char *framepace_event_name(void);
        extern bool vr_first_person_now(void);
        control_field_str(r, "event", framepace_event_name());
        control_field_bool(r, "first_person", vr_first_person_now());
    }
    control_field_int(r, "stereo_pairs", (int64_t)vr_stereo_pairs());
    uint64_t pad = 0, syncs = 0;
    int32_t sticks[4] = {0};
    control_field_bool(r, "controllers", vr_input_state(&pad, sticks, &syncs));
    control_field_int(r, "controller_buttons", (int64_t)pad);
    control_field_num(r, "controller_lx", sticks[0] / 32767.0);
    control_field_num(r, "controller_ly", sticks[1] / 32767.0);
    control_field_num(r, "controller_rx", sticks[2] / 32767.0);
    control_field_num(r, "controller_ry", sticks[3] / 32767.0);
    control_field_int(r, "controller_syncs", (int64_t)syncs);
    uint32_t attacks = 0, slashes = 0, stabs = 0;
    float power = 0;
    bool shield = false, motion = false;
    vr_motion_state(&attacks, &slashes, &stabs, &power, &shield, &motion);
    control_field_bool(r, "motion_combat", motion);
    control_field_int(r, "motion_attacks", attacks);
    control_field_int(r, "motion_slashes", slashes);
    control_field_int(r, "motion_stabs", stabs);
    control_field_num(r, "motion_last_power", power);
    control_field_bool(r, "motion_shield", shield);
    control_field_int(r, "rumble_pulses", (int64_t)vr_rumble_pulses());
    control_field_int(r, "arrows_aimed", (int64_t)vr_arrows_aimed());
    control_field_int(r, "arc_frames", (int64_t)vr_arc_frames());
    control_field_int(r, "weapons_moved", (int64_t)vr_weapons_moved());
    control_field_int(r, "swing_hits", (int64_t)vr_swing_hits());
    { extern uint64_t vk_vr_shadow_passes_skipped(void); control_field_int(r, "shadow_passes_skipped", (int64_t)vk_vr_shadow_passes_skipped()); }
    { extern uint64_t vr_window_left_frames(void); control_field_int(r, "window_left_frames", (int64_t)vr_window_left_frames()); }
    { extern uint64_t vr_vignette_frames(void); extern float vr_vignette_level(void); extern float vr_stick_motion(void);
      { extern uint64_t vr_marker_frames(void); control_field_int(r, "marker_frames", (int64_t)vr_marker_frames()); }
      control_field_int(r, "vignette_frames", (int64_t)vr_vignette_frames());
      control_field_num(r, "vignette_level", vr_vignette_level()); control_field_num(r, "stick_motion", vr_stick_motion()); }
    { extern uint64_t vr_roomscale_frames(void); control_field_int(r, "roomscale_frames", (int64_t)vr_roomscale_frames()); }
    control_field_int(r, "two_hand_frames", (int64_t)vr_two_hand_frames());
    control_field_int(r, "two_hands", vr_two_hand_latched() ? 1 : 0);
    control_field_int(r, "swing_sensor", (int64_t)vr_swing_sensor());
    control_text(r, "VR %s: %s; session %s, %llu frames (%llu with an image)", vr_enabled() ? "on" : "off",
                 vr_status_text(), state_name(st), (unsigned long long)frames, (unsigned long long)rendered);
}

void vr_control_init(void) {
    static const ControlCmd cmds[] = {
        {"vr", "[recenter]", "Show what the VR mode is doing, or recentre the view on the head", cmd_vr, CONTROL_PLAYER},
    };
    control_register(cmds, (int)(sizeof cmds / sizeof *cmds));
}
