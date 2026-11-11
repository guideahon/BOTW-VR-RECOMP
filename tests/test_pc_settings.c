// Run twice with the same temporary file to verify persistence across separate boots.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/settings.c"
#include "../runtime/hle/bindings.c"

const TextLanguage g_text_languages[] = {{"Auto", NULL, NULL}};
const VoiceLanguage g_voice_languages[] = {{"Auto", NULL}};
const int g_ntext_languages = 1, g_nvoice_languages = 1;
int input_pad_family(void) { return 0; }
bool fsr3_upscaler_enabled(void) { return false; }
float winsize_refresh_rate(void) { return 0; }

int main(int argc, char **argv) {
    assert(argc == 6);
    setenv("SAVAGEWIND_SETTINGS", argv[1], 1);
    settings_load();
    assert(g_settings.pc_cursor == (atoi(argv[2]) != 0));
    assert(g_settings.rumble == (atoi(argv[3]) != 0));
    assert(g_settings.sparse_heap == atoi(argv[4]));
    assert(g_settings.tex_lod_bias == atoi(argv[5]));
    assert(g_settings.audio_output == atoi(argv[4]));
    g_settings.rumble = !g_settings.rumble;
    // UI changes must remain possible even when startup debug overrides were supplied.
    g_settings.pc_cursor = !g_settings.pc_cursor;
    g_settings.sparse_heap = (g_settings.sparse_heap + 1) % SPARSE_HEAP_COUNT;
    g_settings.tex_lod_bias = (g_settings.tex_lod_bias + 1) % TEX_LOD_COUNT;
    g_settings.audio_output = (g_settings.audio_output + 1) % AUDIO_OUT_COUNT;
    // The VR options (settings.h VR_OPTIONS): the first boot finds the defaults (all 0) and sets some; the next ones
    // find them as set. A toggle on by default is written the player's way round: off as 0.
    if (g_settings.vr[VR_OPT_SNAP_DEGREES] == 0) {
        for (int i = 0; i < VR_OPT_COUNT; i++) assert(g_settings.vr[i] == 0);
        g_settings.vr[VR_OPT_VIEW] = 1, g_settings.vr[VR_OPT_EVENTS] = 3, g_settings.vr[VR_OPT_SNAP_DEGREES] = 60;
        g_settings.vr[VR_OPT_RUMBLE] = 1;   // (off)
    } else {
        assert(g_settings.vr[VR_OPT_VIEW] == 1 && g_settings.vr[VR_OPT_EVENTS] == 3);
        assert(g_settings.vr[VR_OPT_SNAP_DEGREES] == 60 && g_settings.vr[VR_OPT_RUMBLE] == 1);
        assert(g_settings.vr[VR_OPT_MOTION] == 0 && g_settings.vr[VR_OPT_TWO_HANDS] == 0);
    }
    settings_save();
    {
        FILE *f = fopen(argv[1], "r");
        char text[16384] = {0};
        assert(f);
        fread(text, 1, sizeof text - 1, f);
        fclose(f);
        assert(strstr(text, "vr_view = 1\n") && strstr(text, "vr_rumble = 0\n") && strstr(text, "vr_snap_degrees = 60\n"));
        assert(strstr(text, "vr_motion_combat = 1\n") && strstr(text, "vr_events = 3\n"));
    }
    printf("PASS: load cursor=%s, toggle and save for next boot\n", argv[2]);
}
