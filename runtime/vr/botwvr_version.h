// BOTW VR's own version, apart from Savage Wind's (the recomp it is built on, runtime/ui/launcher.mm
// SAVAGEWIND_VERSION): <phase>-<release>, e.g. "alpha-2026.11.26.2".
//   phase    alpha until the project's owner calls every serious bug fixed; then beta. Changed only when they say so.
//   release  the release's date and its number that day, as its tag: vr-<release>. Set in the commit the tag is made
//            on (.github/workflows/vr-release.yml refuses a tag that does not match it).
// It names the release files (BOTW-VR-Recomp-<version>-...), shows on the start screen and the title screen, and starts every
// log; a new one also resets the drivers' shader caches (runtime/post_update.c, device.c's application version).
#pragma once
#define BOTWVR_PHASE "alpha"
#define BOTWVR_RELEASE "2026.11.28.1"
#define BOTWVR_VERSION BOTWVR_PHASE "-" BOTWVR_RELEASE
