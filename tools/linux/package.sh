#!/bin/bash
# tools/linux/package.sh <x86_64|aarch64> (from tools/linux/appimage.sh or the local PC Maker): the game built
# against build/linux/<arch>/deps and, outside Maker mode, the game code generator helper put together
# as an AppDir and packed by appimagetool: build/dist/Savage_Wind-<version>-<arch>.AppImage; VR builds
# build/dist/BOTW-VR-Recomp-<version>-<arch>.AppImage, -SteamFrame-aarch64 for the Steam Frame (no "Linux" in the name:
# every AppImage is, and the AppImage catalog warns about it).
# Shipped next to the executable (usr/bin, where runtime/paths.c rt_resource looks): what the start screen
# builds the game code with on the player's machine (runtime/gamebuild.c: swgen/, zig/ from tools/fetch_zig.sh,
# gamelib-src/), the window icon, the start screen font. Releases include no game code; Maker mode includes the user's
# prebuilt library. usr/lib: the libraries from deps/ only - glibc, libstdc++, zlib, the Vulkan loader
# and the GPU drivers are the system's (a bundled loader would not find the user's drivers). No game files or fonts.
set -euo pipefail
cd "$(dirname "$0")/../.."
ARCH=$1
L=$PWD/build/linux/$ARCH
P=$L/deps
B=$L/game
APPDIR=$L/AppDir
VERSION=$(sed -n 's/^#define SAVAGEWIND_VERSION "\(.*\)"/\1/p' runtime/ui/launcher.mm)
VR=${VR:-0}
if [ "$VR" = 1 ]; then   # (BOTW VR's own version: runtime/vr/botwvr_version.h)
    VERSION=$(sed -n 's/^#define BOTWVR_PHASE "\(.*\)"/\1/p' runtime/vr/botwvr_version.h)-$(sed -n 's/^#define BOTWVR_RELEASE "\(.*\)"/\1/p' runtime/vr/botwvr_version.h)
fi   # (VR=1: BOTW VR's build, with the OpenXR mode and its loader; it starts in VR unless SAVAGEWIND_VR=0)
VR_MAKER=${VR_MAKER:-0}  # (transfer package: includes this PC's compiled game library, no device-side generator)
SW_BUILD_JOBS=${SW_BUILD_JOBS:-$(nproc)}
if [ "$VR_MAKER" = 1 ] && [ "$VR" != 1 ]; then echo "package: VR_MAKER requires VR=1" >&2; exit 1; fi
STRIP=${LLVM_STRIP:-}
if [ -z "$STRIP" ]; then
    for candidate in llvm-strip $(for version in {30..11}; do printf 'llvm-strip-%s ' "$version"; done); do
        if command -v "$candidate" >/dev/null 2>&1; then STRIP=$(command -v "$candidate"); break; fi
    done
fi
[ -n "$STRIP" ] || { echo "package: install LLVM's llvm-strip (or set LLVM_STRIP)" >&2; exit 1; }
if [ "$VR" = 1 ]; then
    case "$ARCH" in
        x86_64) VR_ASSET_TARGET= ;;
        aarch64) VR_ASSET_TARGET=-SteamFrame ;;
        *) echo "package: unsupported VR AppImage architecture: $ARCH" >&2; exit 1 ;;
    esac
    # (the project's name, as the repository, the README and the desktop file say it: the AppImage catalog compares them)
    OUT=build/dist/BOTW-VR-Recomp-$VERSION$VR_ASSET_TARGET-$ARCH.AppImage
else
    OUT=build/dist/Savage_Wind-$VERSION-$ARCH.AppImage
fi
JOBS=$SW_BUILD_JOBS

tools/linux/build_deps.sh "$ARCH"

CROSSCC=clang
if [ "$ARCH" != "$(uname -m)" ]; then
    : "${SYSROOT:?cross build: SYSROOT (the arm64 root filesystem)}"
    CROSSCC="clang --target=$ARCH-linux-gnu --sysroot=$SYSROOT -fuse-ld=lld"
    MAKEARGS=(ARCH="$ARCH" SYSROOT="$SYSROOT")
else
    MAKEARGS=(ARCH="$ARCH")
fi
[ "$VR" = 1 ] && MAKEARGS+=(VR=1 VR_DEFAULT=1)
[ -n "${GENARCH:-}" ] && MAKEARGS+=(GENARCH="$GENARCH")
[ "${GENARCH_COMPAT+x}" = x ] && MAKEARGS+=(GENARCH_COMPAT="$GENARCH_COMPAT")
if [ "$VR" = 1 ] && [ "$ARCH" != "$(uname -m)" ]; then
    MAKEARGS+=(OPENXR_LIB="${OPENXR_LIB:--lopenxr_loader}")
fi

# the app alone (the game code is built on the player's machine)
make -j"$JOBS" B="$B" DEPS="$P" "${MAKEARGS[@]}" "$B/savagewind"  # (the DEPS link line asks for libshaderc_shared itself)
if nm "$B/savagewind" | grep ' f_70' >/dev/null; then echo "$B/savagewind has game code linked in"; exit 1; fi
if [ "$VR_MAKER" != 1 ]; then ZIG_TARGET=$ARCH-linux tools/fetch_zig.sh; fi

# ---- AppDir
rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
cp "$B/savagewind" "$APPDIR/usr/bin/"
"$STRIP" --strip-debug "$APPDIR/usr/bin/savagewind"  # (symbols stay: crash backtraces name functions)
cp docs/icon/icon_1024.png "$APPDIR/usr/bin/icon.png"
cp runtime/nvn/vulkan/vk_pipelines.seed "$APPDIR/usr/bin/"  # (the pipeline seed the precompilation reads: hashes and render state, nothing of the game)
cp third_party/fonts/FiraSans/FiraSans-SemiBold.ttf third_party/fonts/FiraSans/FiraSans-Medium.ttf "$APPDIR/usr/bin/"  # (menus, OFL)
cp third_party/fonts/FiraSans/OFL.txt "$APPDIR/usr/bin/FiraSans-OFL.txt"
cp third_party/fonts/NotoSansThai/NotoSansThai-Medium.ttf "$APPDIR/usr/bin/"  # (Thai menus, OFL)
cp third_party/fonts/NotoSansThai/OFL.txt "$APPDIR/usr/bin/NotoSansThai-OFL.txt"
cp third_party/fonts/NotoSansHebrew/NotoSansHebrew-Medium.ttf "$APPDIR/usr/bin/"  # (Hebrew menus, OFL)
cp third_party/fonts/NotoSansHebrew/OFL.txt "$APPDIR/usr/bin/NotoSansHebrew-OFL.txt"
cp third_party/FidelityFX/LICENSE.txt "$APPDIR/usr/bin/FidelityFX-LICENSE.txt"  # (AMD FidelityFX SDK, MIT: frame generation)
mkdir "$APPDIR/usr/bin/colour-luts" && cp third_party/luts/*.cube third_party/luts/README.md "$APPDIR/usr/bin/colour-luts/"  # (CC0: Video > Colour)
cp README.md LICENSE "$APPDIR/usr/bin/"  # (licenses and source links of the bundled components: README "License")
cp recomplauncher.json "$APPDIR/"  # (RecompLauncher reads /recomplauncher.json from inside the AppImage)
if [ "$VR_MAKER" = 1 ]; then
    [ -f "$B/swgame.so" ] && [ -f "$B/swgame.so.id" ] || { echo "Maker package needs $B/swgame.so and its .id file" >&2; exit 1; }
    cp "$B/swgame.so" "$B/swgame.so.id" "$APPDIR/usr/bin/"
else
    [ -d "$L/helper/swgen" ] || { echo "no game code generator helper: tools/linux/gen_helper.sh $ARCH first"; exit 1; }
    cp -R "$L/helper/swgen" build/zig "$APPDIR/usr/bin/"
    mkdir "$APPDIR/usr/bin/gamelib-src"
    cp runtime/cpu.h runtime/simd.h runtime/simd_fp.h runtime/simd_softfma.h runtime/gamelib.h tools/gamelib_entry.c \
       runtime/hle/hle_symbols.txt "$APPDIR/usr/bin/gamelib-src/"
fi
if [ "$VR_MAKER" = 1 ]; then
    cp tools/linux/AppRun.maker "$APPDIR/AppRun"
else
    cp tools/linux/AppRun "$APPDIR/AppRun"
    if [ "$VR" = 1 ]; then sed -i 's|^# SAVAGEWIND_VR_DEFAULT$|export SAVAGEWIND_VR=${SAVAGEWIND_VR:-1}|' "$APPDIR/AppRun"; fi
fi
chmod +x "$APPDIR/AppRun"
if [ "$VR" = 1 ]; then   # (the VR build starts in VR, as Windows' "Savage Wind VR.exe" does; its docs next to it)
    mkdir -p "$APPDIR/usr/bin/VR"
    cp docs/VR.md docs/vr/SETUP.md docs/vr/SETUP.es.md docs/vr/FEATURES.md docs/vr/FEATURES.es.md "$APPDIR/usr/bin/VR/"
fi
cp tools/linux/savagewind.desktop "$APPDIR/"
if [ "$VR_MAKER" = 1 ]; then sed -i 's/^Name=.*/Name=Savage Wind VR/; s/^Comment=.*/Comment=BOTW VR, prepared on PC/' "$APPDIR/savagewind.desktop"
elif [ "$VR" = 1 ]; then   # (BOTW VR's release: named as the project, the README and the AppImage file name are)
    sed -i 's/^Name=Savage Wind$/Name=BOTW VR Recomp/; s/^Comment=.*/Comment=Breath of the Wild in PC VR, from your own copy of the game/
            s/^Name=Savage Wind Configuration$/Name=BOTW VR Recomp Configuration/' "$APPDIR/savagewind.desktop"
    grep -qx 'Name=BOTW VR Recomp' "$APPDIR/savagewind.desktop" || { echo "package: the desktop file's Name wasn't set" >&2; exit 1; }
fi
cp docs/icon/icon_1024.png "$APPDIR/savagewind.png" && ln -sf savagewind.png "$APPDIR/.DirIcon"

# libraries: what savagewind needs (DT_NEEDED, recursively) that deps/ provides; anything else must be a system one
needed() { readelf -d "$1" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
SYSTEM_LIBS='^(libc|libm|libdl|libpthread|librt|libresolv|libstdc\+\+|libgcc_s|libz|libvulkan)\.so|^ld-linux'
# (tools/linux/appimage.sh passes SYSROOT, the arm64 root, to every container run: it is only the target's libraries
# when cross-compiling; a native build takes them from this system, so no arm64 library ends up in an x86_64 AppImage)
CROSS=0; [ "$ARCH" != "$(uname -m)" ] && CROSS=1
sysroot_lib() {  # target libraries supplied with a cross sysroot, including OpenXR's non-base dependencies
    [ "$CROSS" = 1 ] && [ -n "${SYSROOT:-}" ] || return 1
    local dir found
    for dir in "$SYSROOT/usr/lib/$ARCH-linux-gnu" "$SYSROOT/lib/$ARCH-linux-gnu" \
               "$SYSROOT/usr/lib64" "$SYSROOT/usr/lib" "$SYSROOT/lib64" "$SYSROOT/lib"; do
        [ -d "$dir" ] || continue
        found=$(find "$dir" -maxdepth 1 -name "$1" -print -quit)
        if [ -n "$found" ]; then printf '%s\n' "$found"; return 0; fi
    done
    return 1
}
host_lib() {  # native non-base dependency (e.g. libjsoncpp required by the OpenXR loader)
    ldconfig -p | awk -v name="$1" '$1 == name && !path { path = $NF } END { if (path) print path }'
}
todo=("$APPDIR/usr/bin/savagewind")
while [ ${#todo[@]} -gt 0 ]; do
    f=${todo[0]}; todo=("${todo[@]:1}")
    for n in $(needed "$f"); do
        [ -e "$APPDIR/usr/lib/$n" ] && continue
        if [ "$VR" = 1 ] && [[ $n == libopenxr_loader.so* ]]; then   # (the OpenXR loader: system or explicit cross sysroot)
            loader=${OPENXR_LOADER:-}
            if [ -z "$loader" ]; then   # (cross: the target's own loader, from the sysroot)
                if [ "$CROSS" = 1 ]; then loader=$(sysroot_lib "$n" || true); else loader=$(host_lib "$n"); fi
            fi
            [ -n "$loader" ] && [ -e "$loader" ] || { echo "package: can't find $n (set OPENXR_LOADER to its file)" >&2; exit 1; }
            cp -L "$loader" "$APPDIR/usr/lib/$n"
            patchelf --set-rpath '$ORIGIN' "$APPDIR/usr/lib/$n"
            todo+=("$APPDIR/usr/lib/$n")
        elif [[ $n =~ $SYSTEM_LIBS ]]; then
            continue  # (glibc, libstdc++, zlib, and the Vulkan loader come from the target system)
        elif [ -e "$P/lib/$n" ]; then
            cp -L "$P/lib/$n" "$APPDIR/usr/lib/$n"
            patchelf --set-rpath '$ORIGIN' "$APPDIR/usr/lib/$n"
            todo+=("$APPDIR/usr/lib/$n")
        elif [ "$CROSS" = 1 ] && extra=$(sysroot_lib "$n"); then
            cp -L "$extra" "$APPDIR/usr/lib/$n"
            patchelf --set-rpath '$ORIGIN' "$APPDIR/usr/lib/$n"
            todo+=("$APPDIR/usr/lib/$n")
        elif [ "$CROSS" = 0 ] && extra=$(host_lib "$n") && [ -n "$extra" ] && [ -e "$extra" ]; then
            cp -L "$extra" "$APPDIR/usr/lib/$n"
            patchelf --set-rpath '$ORIGIN' "$APPDIR/usr/lib/$n"
            todo+=("$APPDIR/usr/lib/$n")
        elif ! [[ $n =~ $SYSTEM_LIBS ]]; then
            echo "package: $(basename "$f") needs $n, neither bundled nor a base system library"; exit 1
        fi
    done
done
patchelf --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/savagewind"
if ls "$APPDIR/usr/lib" | grep -iE 'x264|x265|xvid|postproc|vulkan'; then  # (no GPL codecs, no Vulkan loader)
    echo "package: library that must not ship"; exit 1
fi
# every binary for the target's CPU (a loader copied from the build machine once made the arm64 AppImage unable to start)
case "$ARCH" in x86_64) MACHINE='X86-64' ;; aarch64) MACHINE='AArch64' ;; *) MACHINE='' ;; esac
for f in "$APPDIR/usr/bin/savagewind" "$APPDIR"/usr/lib/*; do
    m=$(LC_ALL=C readelf -h "$f" | sed -n 's/^ *Machine: *//p')
    [[ $m == *"$MACHINE"* ]] || { echo "package: $(basename "$f") is for $m, not $ARCH" >&2; exit 1; }
done
echo "bundled: $(ls "$APPDIR/usr/lib" | tr '\n' ' ')"

# Mesa's software Vulkan (lavapipe) and the libraries it needs beyond the base system, in usr/lib/lavapipe with its
# driver manifest: with no Vulkan driver on the system the app starts again on it (device.c software_vulkan_retry), so
# the start screen opens and says what is missing (the AppImage catalog's test machine has none). x86_64 only: the
# Steam Frame always has its GPU's driver. Their licenses next to them (licenses/<package>).
if [ "$ARCH" = x86_64 ] && [ "$CROSS" = 0 ] && [ "$VR_MAKER" != 1 ]; then
    LVP=$(ls /usr/lib/x86_64-linux-gnu/libvulkan_lvp.so 2>/dev/null) || { echo "package: no lavapipe (mesa-vulkan-drivers)" >&2; exit 1; }
    LD=$APPDIR/usr/lib/lavapipe
    mkdir -p "$LD/licenses"
    LVP_BASE='^(libc|libm|libdl|libpthread|librt|libresolv|libstdc\+\+|libgcc_s|libz)\.so|^ld-linux|^linux-vdso'
    for f in "$LVP" $(ldd "$LVP" | awk '$3 ~ /^\// {print $1 " " $3}' | while read -r n path; do
                          [[ $n =~ $LVP_BASE ]] || echo "$path"; done); do
        cp -L "$f" "$LD/$(basename "$f")"
        patchelf --set-rpath '$ORIGIN' "$LD/$(basename "$f")"
        pkg=$(dpkg -S "$(realpath "$f")" 2>/dev/null | head -1 | cut -d: -f1 || true)  # (usrmerge: /lib or /usr/lib)
        if [ -z "$pkg" ]; then pkg=$(dpkg -S "$f" 2>/dev/null | head -1 | cut -d: -f1 || true); fi
        if [ -n "$pkg" ] && [ -f "/usr/share/doc/$pkg/copyright" ]; then cp "/usr/share/doc/$pkg/copyright" "$LD/licenses/$pkg"
        else echo "package: no license file found for $(basename "$f") (${pkg:-no package})" >&2; exit 1; fi
    done
    printf '{\n    "ICD": {\n        "api_version": "1.3.0",\n        "library_path": "./libvulkan_lvp.so"\n    },\n    "file_format_version": "1.0.0"\n}\n' > "$LD/lvp_icd.json"
    [ -f "$LD/licenses/mesa-vulkan-drivers" ] || { echo "package: lavapipe's license wasn't found" >&2; exit 1; }
    echo "lavapipe: $(ls "$LD" | tr '\n' ' ') ($(du -sh "$LD" | cut -f1))"
fi

# ---- AppImage (appimagetool runs extracted: no FUSE in the container)
fetch() {  # url sha256 file
    [ -f "build/linux/src/$3" ] || { curl -fsSL -o "build/linux/src/$3.part" "$1" && mv "build/linux/src/$3.part" "build/linux/src/$3"; }
    echo "$2  build/linux/src/$3" | sha256sum -c --quiet
}
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
      ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 appimagetool-x86_64.AppImage
case $ARCH in
    x86_64) fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
                  2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d runtime-x86_64 ;;
    aarch64) fetch https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-aarch64 \
                   00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444 runtime-aarch64 ;;
esac
chmod +x build/linux/src/appimagetool-x86_64.AppImage
mkdir -p build/dist && rm -f "$OUT"
# BOTW VR's releases: update information for AppImageUpdate and the like (the newest release, pre-releases included:
# every one so far is), and its .zsync next to the AppImage, uploaded with it (vr-release.yml). The app's own updater
# (runtime/update_appimage.c) doesn't use them.
UPDARGS=()
if [ "$VR" = 1 ] && [ "$VR_MAKER" != 1 ]; then
    UPDATE_REPO=${BOTWVR_UPDATE_REPO:-guideahon/BOTW-VR-RECOMP}
    UPDARGS=(-u "gh-releases-zsync|${UPDATE_REPO%%/*}|${UPDATE_REPO#*/}|latest-all|BOTW-VR-Recomp-*$VR_ASSET_TARGET-$ARCH.AppImage.zsync")
fi
rm -f "$OUT.zsync" "$(basename "$OUT").zsync"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=$ARCH build/linux/src/appimagetool-x86_64.AppImage --no-appstream "${UPDARGS[@]}" \
    --runtime-file "build/linux/src/runtime-$ARCH" "$APPDIR" "$OUT"
if [ ${#UPDARGS[@]} -gt 0 ]; then   # (appimagetool writes the .zsync in the working folder)
    [ -f "$OUT.zsync" ] || mv "$(basename "$OUT").zsync" "$OUT.zsync"
    ls -la "$OUT.zsync"
fi
ls -la "$OUT"
