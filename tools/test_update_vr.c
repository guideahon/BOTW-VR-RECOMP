// tools/test_update_appimage.py builds this with SW_VR: BOTW VR's Update as Savage Wind Configuration drives it
// (runtime/post_update.c, runtime/ui/launcher.mm), without the game. It checks for a newer release, presses Update (and
// everything else Update and "Go back to the previous version" can call), then finishes setting up.
// usage: test_update_vr <cache folder> <current version>; prints key=value lines.
#include "../runtime/post_update.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *g_cache, *g_version;
void vk_cache_path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", g_cache, name); }
const char *rt_version(void) { return g_version; }
// (runtime/platform/linux.c's, without the rest of the game: $APPDIR is this program's folder in the test)
bool plat_exe_path(char *buf, size_t size) {
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n <= 0) return false;
    buf[n] = 0;
    return true;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    g_cache = argv[1], g_version = argv[2];
    char version[64] = "", message[512] = "", file[256] = "", title[200] = "", what[900] = "";
    const char *page = "", *self = "";
    update_check_start();
    UpdateState st = UPDATE_IDLE;
    for (int i = 0; i < 1000 && (st = update_check_state(version, sizeof version, message, sizeof message)) == UPDATE_CHECKING; i++) {
        struct timespec t = {0, 100000000};
        nanosleep(&t, NULL);
    }
    static const char *const states[] = {"idle", "checking", "available", "current", "error"};
    printf("state=%s\nversion=%s\nmessage=%s\n", states[st], version, message);
    bool by_hand = update_by_hand(file, sizeof file, &page, &self);  // (Update: the launcher asks this first)
    printf("by_hand=%d\nfile=%s\npage=%s\nself=%s\n", by_hand, file, page, self);
    printf("applies_here=%d\n", update_applies_here());
    update_apply_here(title, sizeof title, what, sizeof what);  // (the launcher's update thread, were it started)
    printf("apply_title=%s\napply_progress=%g\n", title, update_apply_progress());
    printf("apply_started=%d\n", update_apply_start());
    printf("can_rollback=%d\n", post_update_can_rollback());
    printf("rollback_started=%d\n", update_rollback_start());
    post_update_finish(g_version);
    post_update_tidy(g_version);
    return 0;
}
