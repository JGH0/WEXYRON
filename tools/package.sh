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
#   APPIMAGE_TOOL          path to the AppImage runtime/tool (default appimagetool)
#   LINUXDEPLOY            path to linuxdeploy (default linuxdeploy-x86_64.AppImage)
#   CC/CXX                 cross compiler for the Windows build, e.g.
#                          x86_64-w64-mingw32-g++ (only used by the exe target
#                          when it has to build).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DIST="$ROOT/dist"
VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo "dev")"
if [ -n "${CI_COMMIT_TAG:-}" ]; then
    VERSION="${CI_COMMIT_TAG#v}"
fi

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

        APPDIR="$DIST/CubeCSLite.AppDir"
        rm -rf "$APPDIR"
        mkdir -p "$APPDIR/usr/bin"

        # linuxdeploy wants a ready-made desktop file + icon.
        DESKTOP="$DIST/cube-cs-lite.desktop"
        sed "s|@EXEC@|cube-cs-lite|g; s|@ICON@|cube-cs-lite|g" \
            "$ROOT/packaging/cube-cs-lite.desktop" > "$DESKTOP"

        # Copy the binary and pull in the shared libraries it needs (glfw,
        # libGL, libstdc++, ...) into the AppDir.
        cp "$GAME_BIN" "$APPDIR/usr/bin/cube-cs-lite"
        "$LINUXDEPLOY" --appdir "$APPDIR" \
            --executable "$APPDIR/usr/bin/cube-cs-lite" \
            --desktop-file "$DESKTOP" \
            --icon-file "$ROOT/packaging/cube-cs-lite.png"

        # The game resolves everything relative to its working directory
        # (assets/...), so assets live next to the binary inside the AppDir and
        # the launcher cds there.
        stage_assets "$APPDIR/usr/bin/assets"

        # Launch through a tiny wrapper so the working directory is right.
        mv "$APPDIR/usr/bin/cube-cs-lite" "$APPDIR/usr/bin/cube-cs-lite.real"
        cat > "$APPDIR/usr/bin/cube-cs-lite" <<'EOF'
#!/bin/sh
cd "$(dirname "$(readlink -f "$0")")" || exit 1
exec ./cube-cs-lite.real "$@"
EOF
        chmod +x "$APPDIR/usr/bin/cube-cs-lite"

        OUTPUT="$DIST/Cube-CS-Lite-${VERSION}-x86_64.AppImage"
        ARCH=x86_64 "$APPIMAGE_TOOL" "$APPDIR" "$OUTPUT"
        rm -rf "$APPDIR" "$DESKTOP"
        echo "Created $OUTPUT"
        ;;

    exe)
        if [ "$NO_BUILD" -eq 0 ]; then
            : "${CXX:=x86_64-w64-mingw32-g++}"
            make -f Makefile.windows client CXX="$CXX"
        fi
        [ -f "$GAME_BIN" ] || { echo "error: $GAME_BIN not found" >&2; exit 1; }

        STAGE="$DIST/cube-cs-lite"
        rm -rf "$STAGE"
        mkdir -p "$STAGE"
        cp "$GAME_BIN" "$STAGE/game.exe"
        cp "$ROOT/README.md" "$STAGE/README.md"

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

        stage_assets "$STAGE/assets"

        ZIP="$DIST/Cube-CS-Lite-${VERSION}-windows-x86_64.zip"
        rm -f "$ZIP"
        if command -v zip >/dev/null 2>&1; then
            (cd "$DIST" && zip -qr "$ZIP" cube-cs-lite)
        else
            (cd "$DIST" && python3 -m zipfile -c "$ZIP" cube-cs-lite)
        fi
        rm -rf "$STAGE"
        echo "Created $ZIP"
        ;;

    *)
        usage
        ;;
esac
