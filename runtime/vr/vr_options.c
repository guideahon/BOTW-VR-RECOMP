// The VR mode's options (runtime/settings.h VR_OPTIONS): settings.ini's vr_* keys, set on the game's Options screen
// (VR page, runtime/ui/game_options.mm), unless a SAVAGEWIND_VR_* variable gives one (the variable wins, for tests
// and launch scripts). Read each time they are used: a change on the Options screen applies at once.
#include <stdlib.h>
#include <string.h>

#include "../settings.h"
#include "vr_options.h"

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
    case VR_OPT_RUMBLE_GAIN: return atof(e) > 0 && atof(e) <= 4 ? (int)(atof(e) * 100 + 0.5) : 0;
    case VR_OPT_SWING: return !strcmp(e, "easy");
    case VR_OPT_HEIGHT: return atoi(e) >= -50 && atoi(e) <= 50 ? atoi(e) : 0;             // (cm)
    case VR_OPT_HUD_DISTANCE: return atof(e) >= 0.2 && atof(e) <= 20 ? (int)(atof(e) * 100 + 0.5) : 0;  // (m)
    case VR_OPT_THIRD_DISTANCE: return atoi(e) >= 25 && atoi(e) <= 300 ? atoi(e) : 0;      // (percent)
    case VR_OPT_HUD_SIZE: return atoi(e) >= 25 && atoi(e) <= 300 ? atoi(e) : 0;           // (percent)
    case VR_OPT_BOW_AIM: return !strcmp(e, "head");
    case VR_OPT_EVENT_BARS: return *e == '1';
    case VR_OPT_PLAY_POSITION: return !strcmp(e, "standing");
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
    [VR_OPT_PLAY_POSITION] = "SAVAGEWIND_VR_PLAY_POSITION",
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
