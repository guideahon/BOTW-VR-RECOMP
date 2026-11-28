// The `vr` control command (docs/CONTROL.md): what the VR mode is doing. Read only, so every player may run it.
#include <math.h>
#include <stdio.h>
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
void vr_hand_tracking_state(bool active[2], bool valid[2], bool tracked[2]);  // runtime/vr/vr_input.c
uint64_t vr_rumble_pulses(void);  // runtime/vr/vr_input.c
uint64_t vr_arrows_aimed(void);   // runtime/vr/vr_skel.c
uint64_t vr_side_arrows_aimed(void);   // runtime/vr/vr_skel.c
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
    if (argc == 2 && !strcmp(argv[1], "lead")) {   // the pose's lead (xr_vulkan.c pose_lead_mark), then reset
        extern void vr_pose_lead(double *avg_ms, double *max_ms, uint64_t *n);
        extern void vr_pose_lead_reset(void);
        double avg, mx;
        uint64_t n;
        vr_pose_lead(&avg, &mx, &n);
        control_field_int(r, "frames", (int64_t)n);
        control_field_num(r, "pose_lead_ms", avg);
        control_field_num(r, "pose_lead_max_ms", mx);
        vr_pose_lead_reset();
        return;
    }
    if (argc == 2 && !strcmp(argv[1], "recenter")) {
        vr_recenter();
        control_text(r, "recentring at the next camera update");
        return;
    }
    if ((argc == 2 || argc == 3) && !strcmp(argv[1], "frametimes")) {   // the headset's frames (pairs), as `frames`
        extern int vr_frame_times(double seconds, double out[6], int *late);
        double secs = argc == 3 ? atof(argv[2]) : 5, v[6];
        if (secs <= 0 || secs > 60) secs = 5;
        int late = 0, n = vr_frame_times(secs, v, &late);
        control_field_int(r, "count", n);
        if (n) {
            control_field_num(r, "frame_ms_avg", v[0]);
            control_field_num(r, "frame_ms_p50", v[1]);
            control_field_num(r, "frame_ms_p95", v[2]);
            control_field_num(r, "frame_ms_p99", v[3]);
            control_field_num(r, "frame_ms_max", v[4]);
            control_field_num(r, "runtime_period_ms", v[5]);
            control_field_int(r, "over_1_5_periods", late);
        }
        return;
    }
    if (argc != 1) {
        control_fail(r, "Usage: vr [recenter | lead | frametimes [seconds]]");
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
    { extern uint64_t vr_pair_hands_kept(void); control_field_int(r, "pair_hands_kept", (int64_t)vr_pair_hands_kept()); }
    { extern uint64_t vk_vr_rays_fixed(void); control_field_int(r, "rays_fixed", (int64_t)vk_vr_rays_fixed()); }
    { extern uint64_t vr_pair_poses_kept(void); control_field_int(r, "pair_poses_kept", (int64_t)vr_pair_poses_kept()); }
    control_field_int(r, "pair_anchors", (int64_t)vr_pair_anchors());
    {
        extern void vr_anchor_checks(uint64_t *frames, uint64_t *mismatch);
        uint64_t fr, mm;
        vr_anchor_checks(&fr, &mm);
        control_field_int(r, "anchor_frames", (int64_t)fr);
        control_field_int(r, "anchor_mismatch", (int64_t)mm);
        extern uint64_t vr_skel_first_frames(void);
        control_field_int(r, "skel_first_frames", (int64_t)vr_skel_first_frames());
    }
    uint64_t pad = 0, syncs = 0;
    int32_t sticks[4] = {0};
    control_field_bool(r, "controllers", vr_input_state(&pad, sticks, &syncs));
    control_field_int(r, "controller_buttons", (int64_t)pad);
    control_field_num(r, "controller_lx", sticks[0] / 32767.0);
    control_field_num(r, "controller_ly", sticks[1] / 32767.0);
    control_field_num(r, "controller_rx", sticks[2] / 32767.0);
    { extern bool vr_quick_menu_open(void); control_field_bool(r, "quick_menu", vr_quick_menu_open()); }
    control_field_num(r, "controller_ry", sticks[3] / 32767.0);
    control_field_int(r, "controller_syncs", (int64_t)syncs);
    bool hand_active[2], hand_valid[2], hand_tracked[2];
    vr_hand_tracking_state(hand_active, hand_valid, hand_tracked);
    control_field_bool(r, "left_hand_active", hand_active[0]);
    control_field_bool(r, "left_hand_valid", hand_valid[0]);
    control_field_bool(r, "left_hand_tracked", hand_tracked[0]);
    control_field_bool(r, "right_hand_active", hand_active[1]);
    control_field_bool(r, "right_hand_valid", hand_valid[1]);
    control_field_bool(r, "right_hand_tracked", hand_tracked[1]);
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
    control_field_int(r, "side_arrows_aimed", (int64_t)vr_side_arrows_aimed());
    {
        extern void vr_skel_armor_off(float *, float *, uint64_t *, uint64_t *);
        float last, max;
        uint64_t checks, off;
        vr_skel_armor_off(&last, &max, &checks, &off);
        control_field_num(r, "armor_wrist_off", last), control_field_num(r, "armor_wrist_off_max", max);
        control_field_int(r, "armor_checks", (int64_t)checks), control_field_int(r, "armor_off_frames", (int64_t)off);
    }
    { extern uint64_t vk_rune_aims(void); control_field_int(r, "rune_aims", (int64_t)vk_rune_aims()); }  // (exec.c: rune picks set by VR, the hand or the eye's axis)
    control_field_int(r, "arc_frames", (int64_t)vr_arc_frames());
    control_field_int(r, "weapons_moved", (int64_t)vr_weapons_moved());
    control_field_int(r, "swing_hits", (int64_t)vr_swing_hits());
    { extern uint64_t vr_kept_turns(void); control_field_int(r, "kept_turns", (int64_t)vr_kept_turns()); }
    { extern uint32_t vr_swings_ignored(void); control_field_int(r, "swings_ignored", (int64_t)vr_swings_ignored()); }
    { extern uint32_t vr_swing_attacks(void); control_field_int(r, "swing_attacks", (int64_t)vr_swing_attacks()); }
    { extern uint32_t vr_no_shake(void); control_field_int(r, "no_shake", (int64_t)vr_no_shake()); }
    { extern uint64_t vr_guide_frames(void); control_field_int(r, "slot_guide_frames", (int64_t)vr_guide_frames()); }
    { extern uint32_t vr_hand_throws(void); control_field_int(r, "hand_throws", (int64_t)vr_hand_throws()); }
    { extern uint64_t vr_bow_string_frames(void); control_field_int(r, "bow_string_frames", (int64_t)vr_bow_string_frames()); }
    { extern uint64_t vr_carried_frames(void); control_field_int(r, "carried_frames", (int64_t)vr_carried_frames()); }
    { extern float vr_climb_eye(void); control_field_num(r, "climb_eye", vr_climb_eye()); }
    {   // how far the eye is from Link's head bone: up (+) / down, and along the ground (a climbing report: "the head very
        // far from the body", 2026-11-26)
        extern bool vr_skel_head_world(float out[3]), vr_eye_world(float out[3]);
        float h[3], e[3];
        if (vr_skel_head_world(h) && vr_eye_world(e)) {
            control_field_num(r, "eye_above_head", e[1] - h[1]);
            control_field_num(r, "eye_from_head_flat", sqrtf((e[0] - h[0]) * (e[0] - h[0]) + (e[2] - h[2]) * (e[2] - h[2])));
        }
    }
    { extern bool vr_player_carrying(void); control_field_int(r, "carrying", vr_player_carrying()); }
    { extern uint64_t vk_vr_shadow_passes_skipped(void); control_field_int(r, "shadow_passes_skipped", (int64_t)vk_vr_shadow_passes_skipped()); }
    { extern uint64_t vk_vr_hud_draws_skipped(void); control_field_int(r, "hud_draws_skipped", (int64_t)vk_vr_hud_draws_skipped()); }
    { extern float vr_eye_height_used(void); control_field_num(r, "eye_height", vr_eye_height_used()); }
    {   // (the player's movement flags: vr_skel.c vr_player_flags; -1 not there)
        extern bool vr_player_flags(uint32_t *f);
        uint32_t mf;
        control_field_num(r, "move_flags", vr_player_flags(&mf) ? (double)mf : -1);
    }
    { extern uint64_t vr_perf_frames(void); control_field_int(r, "perf_frames", (int64_t)vr_perf_frames()); }
    { extern uint64_t vr_lookat_camera(void); char b[32]; snprintf(b, sizeof b, "0x%llx", (unsigned long long)vr_lookat_camera());
      control_field_str(r, "lookat_camera", b); }
    { extern uint64_t vr_window_left_frames(void); control_field_int(r, "window_left_frames", (int64_t)vr_window_left_frames()); }
    { extern uint64_t vr_vignette_frames(void); extern float vr_vignette_level(void); extern float vr_stick_motion(void);
      { extern uint64_t vr_marker_frames(void); control_field_int(r, "marker_frames", (int64_t)vr_marker_frames()); }
      control_field_int(r, "vignette_frames", (int64_t)vr_vignette_frames());
      control_field_num(r, "vignette_level", vr_vignette_level()); control_field_num(r, "stick_motion", vr_stick_motion()); }
    { extern uint64_t vr_roomscale_frames(void); control_field_int(r, "roomscale_frames", (int64_t)vr_roomscale_frames()); }
    { extern uint64_t vr_ladder_frames(void); control_field_int(r, "ladder_frames", (int64_t)vr_ladder_frames()); }
    { extern bool vr_skel_facing(float *yaw); float f; if (vr_skel_facing(&f)) control_field_num(r, "link_facing", f * 57.29578f); }
    { extern uint64_t vr_ui_projected(void); control_field_int(r, "ui_projected", (int64_t)vr_ui_projected()); }
    { extern uint64_t vr_ui_stamina(void); control_field_int(r, "ui_stamina", (int64_t)vr_ui_stamina()); }
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
