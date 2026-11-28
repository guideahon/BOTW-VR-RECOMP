// The VR mode's options (runtime/settings.h VR_OPTIONS): settings.ini's vr_* keys, set on the game's Options screen
// (VR page, runtime/ui/game_options.mm), unless a SAVAGEWIND_VR_* variable gives one (the variable wins, for tests
// and launch scripts). Read each time they are used: a change on the Options screen applies at once.
#include <stdlib.h>
#include <string.h>

#include "../settings.h"
#include "vr_options.h"
#include "../rt.h"

// a variable's text as the option's number (-1: not this option's text)
static int from_env(int id, const char *e) {
    switch (id) {
    case VR_OPT_VIEW: return !strcmp(e, "third");
    case VR_OPT_EVENTS: return !strcmp(e, "first") ? 1 : !strcmp(e, "third") ? 2 : !strcmp(e, "screen") ? 3 : 0;
    case VR_OPT_TURN: return !strcmp(e, "snap") ? 1 : !strcmp(e, "head") ? 2 : 0;
    case VR_OPT_SNAP_DEGREES: return atoi(e) >= 5 && atoi(e) <= 180 ? atoi(e) : 0;
    case VR_OPT_TURN_SPEED: return atof(e) >= 0.2 && atof(e) <= 3 ? (int)(atof(e) * 100 + 0.5) : 0;
    case VR_OPT_WALK: return !strcmp(e, "controller");
    case VR_OPT_LEFT_HANDED: return *e == '1';
    case VR_OPT_SWING_ATTACK: return *e == '1';   // (off by default)
    case VR_OPT_RUMBLE_GAIN: return atof(e) > 0 && atof(e) <= 4 ? (int)(atof(e) * 100 + 0.5) : 0;
    case VR_OPT_SWING: return !strcmp(e, "easy");
    case VR_OPT_HEIGHT: return atoi(e) >= -50 && atoi(e) <= 50 ? atoi(e) : 0;             // (cm)
    case VR_OPT_HUD_DISTANCE: return atof(e) >= 0.2 && atof(e) <= 20 ? (int)(atof(e) * 100 + 0.5) : 0;  // (m)
    case VR_OPT_THIRD_DISTANCE: return atoi(e) >= 25 && atoi(e) <= 300 ? atoi(e) : 0;      // (percent)
    case VR_OPT_HUD_SIZE: return atoi(e) >= 25 && atoi(e) <= 300 ? atoi(e) : 0;           // (percent)
    case VR_OPT_BOW_AIM: return !strcmp(e, "head");
    case VR_OPT_RUNE_AIM: return !strcmp(e, "head");
    case VR_OPT_EVENT_BARS: return *e == '1';
    case VR_OPT_PLAY_POSITION: return !strcmp(e, "standing");
    case VR_OPT_EYE_RES: return atoi(e) >= 360 && atoi(e) <= 4096 ? atoi(e) : 0;   // (lines)
    case VR_OPT_WORLD_SCALE:
    case VR_OPT_IPD_SCALE: return atoi(e) >= 50 && atoi(e) <= 150 ? atoi(e) : 0; // (%)
    case VR_OPT_SLOT_GUIDE: return !strcmp(e, "off") || !strcmp(e, "0") ? 1 : !strcmp(e, "always") ? 2 : 0;
    case VR_OPT_DEADZONE: return atoi(e) >= 1 && atoi(e) <= 60 ? atoi(e) : 0;        // (percent)
    case VR_OPT_EXPERIMENTAL: return *e == '1';
    case VR_OPT_RENDER: return !strcmp(e, "stereo") || !strcmp(e, "compat") ? 1 : !strcmp(e, "multiview") ? 2 :
                               !strcmp(e, "mono") ? 3 : 0;
    case VR_OPT_ANGVEL_FIX: return !strcmp(e, "on") || !strcmp(e, "1") ? 1 : !strcmp(e, "off") || !strcmp(e, "0") ? 2 : 0;
    case VR_OPT_BOW_ARC: return !strcmp(e, "0") || !strcmp(e, "off") ? 1 : !strcmp(e, "dot") ? 2 : !strcmp(e, "faint") ? 3 : 0;
    case VR_OPT_VIGNETTE: return !strcmp(e, "low") ? 1 : !strcmp(e, "medium") ? 2 : !strcmp(e, "high") ? 3 :
                                 *e >= '0' && *e <= '3' && !e[1] ? *e - '0' : 0;
    default: return *e == '0';   // (the toggles on by default: "0" turns them off)
    }
}

static const char *const env_names[VR_OPT_COUNT] = {
    [VR_OPT_VIEW] = "SAVAGEWIND_VR_VIEW", [VR_OPT_EVENTS] = "SAVAGEWIND_VR_EVENTS", [VR_OPT_TURN] = "SAVAGEWIND_VR_TURN",
    [VR_OPT_SNAP_DEGREES] = "SAVAGEWIND_VR_SNAP_DEGREES", [VR_OPT_TURN_SPEED] = "SAVAGEWIND_VR_TURN_SPEED",
    [VR_OPT_WALK] = "SAVAGEWIND_VR_WALK", [VR_OPT_LEFT_HANDED] = "SAVAGEWIND_VR_LEFT_HANDED",
    [VR_OPT_RUMBLE] = "SAVAGEWIND_VR_RUMBLE", [VR_OPT_RUMBLE_GAIN] = "SAVAGEWIND_VR_RUMBLE_GAIN",
    [VR_OPT_MOTION] = "SAVAGEWIND_VR_MOTION", [VR_OPT_SWING] = "SAVAGEWIND_VR_SWING",
    [VR_OPT_SWING_ATTACK] = "SAVAGEWIND_VR_SWING_ATTACK", [VR_OPT_SWING_HITS] = "SAVAGEWIND_VR_SWING_HITS",
    [VR_OPT_WEAPON_HITS] = "SAVAGEWIND_VR_WEAPON_HITS", [VR_OPT_SHIELD] = "SAVAGEWIND_VR_SHIELD",
    [VR_OPT_TWO_HANDS] = "SAVAGEWIND_VR_TWO_HANDS", [VR_OPT_DROP] = "SAVAGEWIND_VR_DROP",
    [VR_OPT_BOW_AIM] = "SAVAGEWIND_VR_BOW_AIM", [VR_OPT_BOW_ARC] = "SAVAGEWIND_VR_BOW_ARC",
    [VR_OPT_ROOMSCALE] = "SAVAGEWIND_VR_ROOMSCALE", [VR_OPT_HEIGHT] = "SAVAGEWIND_VR_HEIGHT_OFFSET",
    [VR_OPT_HUD_DISTANCE] = "SAVAGEWIND_VR_HUD_DISTANCE", [VR_OPT_HUD_SIZE] = "SAVAGEWIND_VR_HUD_SIZE",
    [VR_OPT_HUD_GAZE] = "SAVAGEWIND_VR_HUD_FOLLOWS_GAZE", [VR_OPT_ONE_HIT] = "SAVAGEWIND_VR_ONE_HIT",
    [VR_OPT_THIRD_DISTANCE] = "SAVAGEWIND_VR_THIRD_DISTANCE", [VR_OPT_EVENT_BARS] = "SAVAGEWIND_VR_CUTSCENE_BARS",
    [VR_OPT_VIGNETTE] = "SAVAGEWIND_VR_VIGNETTE", [VR_OPT_HAND_MARKERS] = "SAVAGEWIND_VR_SHOW_CONTROLLERS",
    [VR_OPT_PLAY_POSITION] = "SAVAGEWIND_VR_PLAY_POSITION", [VR_OPT_RUNE_AIM] = "SAVAGEWIND_VR_RUNE_AIM",
    [VR_OPT_EYE_RES] = "SAVAGEWIND_VR_HEIGHT", [VR_OPT_DEADZONE] = "SAVAGEWIND_VR_DEADZONE",
    [VR_OPT_ANGVEL_FIX] = "SAVAGEWIND_VR_ANGULAR_FIX", [VR_OPT_SLOT_GUIDE] = "SAVAGEWIND_VR_SLOT_GUIDE",
    [VR_OPT_WORLD_SCALE] = "SAVAGEWIND_VR_WORLD_SCALE", [VR_OPT_IPD_SCALE] = "SAVAGEWIND_VR_IPD_SCALE",
    [VR_OPT_RENDER] = "SAVAGEWIND_VR_RENDER", [VR_OPT_EXPERIMENTAL] = "SAVAGEWIND_VR_EXPERIMENTAL",
    [VR_OPT_HIDE_WINDOW] = "SAVAGEWIND_VR_HIDE_WINDOW", [VR_OPT_PERFORMANCE] = "SAVAGEWIND_VR_PERFORMANCE",
};

int vr_opt(int id) {
    if (id < 0 || id >= VR_OPT_COUNT) return 0;
    static int env[VR_OPT_COUNT], read;   // (the variables, read once: -1 none)
    if (!read) {
        for (int i = 0; i < VR_OPT_COUNT; i++) {
            const char *e = env_names[i] ? getenv(env_names[i]) : NULL;
            env[i] = e && *e ? from_env(i, e) : -1;
        }
        read = 1;
    }
    return env[id] >= 0 ? env[id] : g_settings.vr[id];
}

bool vr_opt_on(int id) { return vr_opt(id) == 0; }

// Options > VR > Performance (on by default): in VR, Video's Draw distance is taken as Potato and its Anti-aliasing as
// SMAA, without changing them (flat play keeps them). The eyes' cost is the render thread's draws, paid twice a pair in
// Stereo: Potato's 0.6 of the game's distances is +11% headset fps, SMAA instead of TAA +6%, both +29% in Stereo and
// +20% in Multiview (fake_xr, RTX 3090, the Great Plateau, 2026-11-26; tools/vr/bench/quick.py); up close the picture is
// the same, far off fewer actors. Grass off (+7% alone, nothing on top of these) and Potato shadows (0%) were measured
// and left out. Off: Video's own settings in VR too
extern bool vr_enabled(void);

bool vr_performance(void) {
    bool on = vr_enabled() && vr_opt_on(VR_OPT_PERFORMANCE);
    static int said = -1;
    if (vr_enabled() && said != on)
        said = on, rt_log("[vr] performance: %s\n", on ? "on (Draw distance Potato, SMAA in place of TAA, in VR)"
                                                        : "off (Video's Draw distance and Anti-aliasing)");
    return on;
}
