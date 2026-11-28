#!/bin/bash
# tools/linux/test_catalog_window.sh <AppImage or AppDir> [vulkan: none|lavapipe|system]
# The AppImage catalog's test (https://github.com/AppImage/appimage.github.io, code/worker.sh), as near as this machine
# allows: started with no arguments on a bare X server (Xvfb, no window manager needed), an empty home (no game files,
# no settings), no network (firejail --net=none there, a network namespace here), no OpenXR runtime and, by default,
# no Vulkan driver at all (the catalog's runner has none). It passes when a visible window whose name xdotool can read
# appears within 30 seconds (the catalog's own check: xdotool search --onlyvisible --name '.'), listed by
# xwininfo -tree with a class. This machine's zenity, if
# any, shows error boxes the runner can't (it has none): build/vr/catalog/docker's runner image is the closer copy, and
# .github/workflows/catalog-test.yml runs the test on a GitHub runner itself. Output under build/vr/catalog (not /tmp).
set -uo pipefail
cd "$(dirname "$0")/../.."
TARGET=$(realpath "${1:?usage: tools/linux/test_catalog_window.sh <AppImage|AppDir> [none|lavapipe|system]}")
VK=${2:-none}
OUT=$PWD/build/vr/catalog/run-$VK
rm -rf "$OUT" && mkdir -p "$OUT/home"
if [ -d "$TARGET" ]; then RUN=("$TARGET/AppRun")
else RUN=(env APPIMAGE_EXTRACT_AND_RUN=1 TMPDIR="$OUT" "$TARGET"); fi   # (no FUSE inside the namespace)

DISP=:$((90 + RANDOM % 9))
Xvfb "$DISP" -screen 0 800x600x24 >"$OUT/xvfb.log" 2>&1 &
XPID=$!
sleep 1
case $VK in
    none) VKENV=(VK_ICD_FILENAMES=/nonexistent.json VK_DRIVER_FILES=/nonexistent.json) ;;
    lavapipe) VKENV=(VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json) ;;
    system) VKENV=() ;;
    *) echo "vulkan: none, lavapipe or system" >&2; kill $XPID; exit 2 ;;
esac
# (a clean environment: only what the catalog's runner would have, its UTF-8 locale too - in one SDL's X11 message box
# names its window in "UTF-8" text, which the catalog's xdotool can't read; XR_RUNTIME_JSON to nothing: no OpenXR runtime)
(cd "$OUT/home" && exec env -i PATH=/usr/bin:/bin HOME="$OUT/home" DISPLAY="$DISP" LANG=C.UTF-8 XR_RUNTIME_JSON=/nonexistent.json "${VKENV[@]}" \
    unshare -rn -- "${RUN[@]}") >"$OUT/stdout.log" 2>&1 &   # (in the empty home: an extracted/ in the working folder is game files)
APID=$!
START=$(date +%s)
RESULT=1
for _ in $(seq 1 30); do
    sleep 1
    if DISPLAY=$DISP timeout 5 xdotool search --onlyvisible --name '.' >"$OUT/windows.txt" 2>/dev/null; then RESULT=0; break; fi
    kill -0 $APID 2>/dev/null || break
done
ELAPSED=$(( $(date +%s) - START ))
# (then, as the catalog does, the windows xwininfo -tree lists with a name and a class: SDL's X11 message box has no
# WM_CLASS, and alone it was "not a single window on screen", 28.1)
if [ $RESULT = 0 ]; then
    sleep 2
    NWIN=$(DISPLAY=$DISP timeout 20 xwininfo -tree -root | grep 0x | grep -c '": ("' || true)
    [ "$NWIN" -ge 1 ] || { RESULT=1; echo "no window with a class (xwininfo -tree):"; DISPLAY=$DISP xwininfo -tree -root | grep 0x; }
fi
if [ $RESULT = 0 ]; then
    sleep 2
    for w in $(cat "$OUT/windows.txt"); do echo "window: $(DISPLAY=$DISP xdotool getwindowname "$w")"; done
    DISPLAY=$DISP import -window root "$OUT/screen.png" 2>/dev/null || ffmpeg -loglevel error -y -f x11grab -video_size 800x600 -i "$DISP" -frames:v 1 "$OUT/screen.png" || true
    echo "PASS ($VK Vulkan): a window within ${ELAPSED}s (screen: $OUT/screen.png)"
elif kill -0 $APID 2>/dev/null; then
    echo "FAIL ($VK Vulkan): no window after ${ELAPSED}s, still running"
else
    wait $APID; echo "FAIL ($VK Vulkan): exited with $? after ${ELAPSED}s, no window"
fi
pkill -TERM -P $APID 2>/dev/null; kill -TERM $APID 2>/dev/null; sleep 2; pkill -KILL -P $APID 2>/dev/null; kill -KILL $APID 2>/dev/null
kill $XPID 2>/dev/null
LOG=$(find "$OUT/home" -name savagewind-launcher.log 2>/dev/null | head -1)
echo "--- launcher log ($LOG), last lines:"; [ -n "$LOG" ] && tail -n 25 "$LOG"
exit $RESULT
