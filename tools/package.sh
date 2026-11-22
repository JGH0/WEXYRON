#!/usr/bin/env bash
# Package the game client for release. Only the client is packaged; the
# dedicated server has its own build in the Makefiles.
#
# Usage:
#   tools/package.sh appimage <game-binary> [--no-build]
#       Build an AppImage from a Linux build of the game.
#   tools/package.sh exe <game.exe> [--no-build]
#       Build a portable Windows zip containing game.exe, the MinGW runtime
#       DLLs and assets/.
#
# --no-build only runs the packaging step (used in CI, where the binary has
# already been built by a previous job). Without it the script builds first.
#
# Output goes to dist/.
#
# Environment:
#   VERSION                optional override for the version in file names
#                          (the CI passes the tag name); default is
#                          "git describe".
#   APPIMAGE_TOOL          path to the AppImage runtime/tool (default appimagetool)
#   LINUXDEPLOY            path to linuxdeploy (default linuxdeploy-x86_64.AppImage)
#   MESA_DIR               directory with 64-bit Mesa DLLs (opengl32.dll, ...) to
#                          ship as a software-rendering fallback in the exe zip
#   MINGW_DLL_DIR          directory with the MinGW runtime + glfw3 DLLs
#   CC/CXX                 cross compiler for the Windows build, e.g.
#                          x86_64-w64-mingw32-g++ (only used by the exe target
#                          when it has to build).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DIST="$ROOT/dist"
VERSION="${VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo "dev")}"

usage() {
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
}

[ $# -ge 2 ] || usage
TARGET="$1"
GAME_BIN="$2"
NO_BUILD=0
for arg in "${@:3}"; do
    case "$arg" in
        --no-build) NO_BUILD=1 ;;
        *) usage ;;
    esac
done

# assets/settings.cfg is a live, user-editable config. Ship the example instead
# so the release does not overwrite a player's settings.
stage_assets() {
    local dest="$1"
    mkdir -p "$dest"
    cp -a assets/. "$dest"/
    rm -f "$dest/settings.cfg" "$dest/machine_id"
}

mkdir -p "$DIST"

case "$TARGET" in
    appimage)
        if [ "$NO_BUILD" -eq 0 ]; then
            make -j"$(nproc)" client
        fi
        [ -f "$GAME_BIN" ] || { echo "error: $GAME_BIN not found" >&2; exit 1; }

        APPDIR="$DIST/WEXYRON.AppDir"
        rm -rf "$APPDIR"
        mkdir -p "$APPDIR/usr/bin"

        # linuxdeploy wants a ready-made desktop file + icon.
        DESKTOP="$DIST/wexyron.desktop"
        sed "s|@EXEC@|wexyron|g; s|@ICON@|wexyron|g" \
            "$ROOT/packaging/wexyron.desktop" > "$DESKTOP"

        # Copy the binary and pull in the shared libraries it needs (glfw,
        # libGL, libstdc++, ...) into the AppDir.
        cp "$GAME_BIN" "$APPDIR/usr/bin/wexyron"
        "$LINUXDEPLOY" --appdir "$APPDIR" \
            --executable "$APPDIR/usr/bin/wexyron" \
            --desktop-file "$DESKTOP" \
            --icon-file "$ROOT/packaging/wexyron.png"

        # The game resolves everything relative to its working directory
        # (assets/...), so assets live next to the binary inside the AppDir and
        # the launcher cds there.
        stage_assets "$APPDIR/usr/bin/assets"

        # License notices, as required by the bundled third-party components.
        mkdir -p "$APPDIR/usr/share/doc/wexyron"
        cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APPDIR/usr/share/doc/wexyron/"

        # Launch through a tiny wrapper so the working directory is right.
        mv "$APPDIR/usr/bin/wexyron" "$APPDIR/usr/bin/wexyron.real"
        cat > "$APPDIR/usr/bin/wexyron" <<'EOF'
#!/bin/sh
cd "$(dirname "$(readlink -f "$0")")" || exit 1
exec ./wexyron.real "$@"
EOF
        chmod +x "$APPDIR/usr/bin/wexyron"

        OUTPUT="$DIST/WEXYRON-${VERSION}-x86_64.AppImage"
        ARCH=x86_64 "$APPIMAGE_TOOL" "$APPDIR" "$OUTPUT"
        rm -rf "$APPDIR" "$DESKTOP"
        echo "Created $OUTPUT"

        # Also create a tarball that includes the launcher script and version.txt
        # so users can run ./launcher.sh for automatic update-on-launch.
        TARNAME="WEXYRON-${VERSION}-linux-x86_64"
        TARDIR="$DIST/$TARNAME"
        rm -rf "$TARDIR"
        mkdir -p "$TARDIR"
        cp "$OUTPUT"           "$TARDIR/"
        cp "$ROOT/launcher/launcher.sh" "$TARDIR/launcher.sh"
        chmod +x "$TARDIR/launcher.sh"
        echo "$VERSION" > "$TARDIR/version.txt"
        TAR_OUTPUT="$DIST/${TARNAME}.tar.gz"
        tar -czf "$TAR_OUTPUT" -C "$DIST" "$TARNAME"
        rm -rf "$TARDIR"
        echo "Created $TAR_OUTPUT"
        ;;

    exe)
        if [ "$NO_BUILD" -eq 0 ]; then
            : "${CXX:=x86_64-w64-mingw32-g++}"
            make -f Makefile.windows client CXX="$CXX"
        fi
        [ -f "$GAME_BIN" ] || { echo "error: $GAME_BIN not found" >&2; exit 1; }

        STAGE="$DIST/wexyron"
        rm -rf "$STAGE"
        mkdir -p "$STAGE"
        cp "$GAME_BIN" "$STAGE/game.exe"
        cp "$ROOT/README.md" "$STAGE/README.md"
        cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/"

        # AMD FSR frame generation (tools/fetch_sdks.py), with its licence:
        # the SDK licence requires the notice and disclaimers to ship along.
        FSR_KIT="$ROOT/third_party/sdks/fidelityfx/Kits/FidelityFX"
        if [ -f "$FSR_KIT/signedbin/amd_fidelityfx_loader_vk.dll" ]; then
            cp "$FSR_KIT/signedbin/amd_fidelityfx_loader_vk.dll" \
               "$FSR_KIT/signedbin/amd_fidelityfx_framegeneration_vk.dll" "$STAGE/"
            cp "$FSR_KIT/docs/license.md" "$STAGE/AMD_FidelityFX_LICENSE.md"
        else
            echo "note: AMD FSR not fetched (tools/fetch_sdks.py) - zip ships without FSR frame generation" >&2
        fi

        # MinGW runtime + glfw DLLs, taken from the sysroot next to the
        # compiler that built the exe (MSYS2 or mingw-w64). CI exports
        # MINGW_DLL_DIR; locally we derive it from the cross compiler.
        if [ -z "${MINGW_DLL_DIR:-}" ]; then
            CC1="$("${CXX:-x86_64-w64-mingw32-g++}" -print-prog-name=cc1plus)"
            MINGW_DLL_DIR="$(cd "$(dirname "$CC1")/../bin" && pwd)"
        fi
        for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll libgcc_s_dw2-1.dll glfw3.dll glfw3dll.dll; do
            cp "$MINGW_DLL_DIR/$dll" "$STAGE/" 2>/dev/null || true
        done
        if [ ! -f "$STAGE/libstdc++-6.dll" ] || [ ! -f "$STAGE/glfw3.dll" ]; then
            echo "warning: expected MinGW/GLFW DLLs not found in $MINGW_DLL_DIR" >&2
        fi

        # Mesa software renderer, used as a fallback when the machine has no
        # hardware OpenGL (e.g. VMs with qxl/basic display adapters). Only the
        # OpenGL parts are shipped, and opengl32.dll is renamed to
        # opengl32_sw.dll so it is NOT loaded automatically: the game copies it
        # to opengl32.dll and restarts itself if WGL fails. Hardware machines
        # never touch it.
        if [ -n "${MESA_DIR:-}" ] && [ -d "$MESA_DIR" ]; then
            for f in opengl32.dll libgallium_wgl.dll libdlclose-skip.dll; do
                cp "$MESA_DIR/$f" "$STAGE/" 2>/dev/null || true
            done
            if [ -f "$STAGE/opengl32.dll" ]; then
                mv -f "$STAGE/opengl32.dll" "$STAGE/opengl32_sw.dll"
            else
                echo "warning: MESA_DIR has no opengl32.dll; shipping without the software renderer" >&2
            fi
        else
            echo "note: MESA_DIR not set, building without the software renderer" >&2
        fi

        stage_assets "$STAGE/assets"

        # Launcher exe — built separately; non-fatal if not present locally
        # (CI builds it explicitly before calling this script).
        if [ -f "$ROOT/build/bin/launcher.exe" ]; then
            cp "$ROOT/build/bin/launcher.exe" "$STAGE/launcher.exe"
        else
            echo "note: launcher.exe not found in build/bin — zip will not include it" >&2
        fi

        # version.txt tells launcher.exe the installed version so it can
        # decide whether a GitHub release is newer.
        echo "$VERSION" > "$STAGE/version.txt"

        ZIP="$DIST/WEXYRON-${VERSION}-windows-x86_64.zip"
        rm -f "$ZIP"
        if command -v zip >/dev/null 2>&1; then
            (cd "$DIST" && zip -qr "$ZIP" wexyron)
        else
            (cd "$DIST" && python3 -m zipfile -c "$ZIP" wexyron)
        fi
        rm -rf "$STAGE"
        echo "Created $ZIP"
        ;;

    *)
        usage
        ;;
esac
