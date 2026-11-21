#!/usr/bin/env bash
# WEXYRON Launcher for Linux
# Checks GitHub for newer releases, downloads & applies updates, then launches the game.
# Works with both AppImage releases and local source builds.
#
# Usage:
#   ./launcher.sh [--no-update] [-- <game args>]
#
# --no-update  skip the update check and launch immediately

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/version.txt"
GITHUB_API="https://api.github.com/repos/JGH0/WEXYRON/releases/latest"

# ---- Helpers ----

current_version() {
    [ -f "$VERSION_FILE" ] && cat "$VERSION_FILE" | tr -d '[:space:]' || echo "v0.0.0"
}

# Returns 0 if $2 is semantically newer than $1 (both "vX.Y.Z")
is_newer() {
    local cur="$1" latest="$2"
    [ "$cur" != "$latest" ] || return 1
    local newer
    # sort -V: version-aware sort; newer version comes last
    newer=$(printf '%s\n%s\n' "$cur" "$latest" | sort -V | tail -1)
    [ "$newer" = "$latest" ]
}

check_command() {
    command -v "$1" >/dev/null 2>&1
}

# ---- Update logic ----

do_update_check() {
    if ! check_command curl; then
        echo "[launcher] curl not found — skipping update check."
        return 0
    fi

    echo "[launcher] Checking for updates..."
    local json
    json=$(curl -sf --max-time 10 \
        -H "Accept: application/vnd.github+json" \
        -H "X-GitHub-Api-Version: 2022-11-28" \
        "$GITHUB_API") || {
        echo "[launcher] No connection — skipping update check."
        return 0
    }

    local tag dl_url
    tag=$(echo "$json"    | grep -o '"tag_name":"[^"]*"'                              | head -1 | cut -d'"' -f4)
    dl_url=$(echo "$json" | grep -o '"browser_download_url":"[^"]*x86_64\.AppImage"' | head -1 | cut -d'"' -f4)

    if [ -z "$tag" ] || [ -z "$dl_url" ]; then
        echo "[launcher] Could not parse release info — skipping update."
        return 0
    fi

    local cur
    cur=$(current_version)

    if ! is_newer "$cur" "$tag"; then
        echo "[launcher] Up to date ($cur)."
        return 0
    fi

    echo "[launcher] Update available: $cur → $tag"

    # Find the existing AppImage (if any) so we know where to put the new one
    local old_appimage new_appimage
    old_appimage=$(find "$SCRIPT_DIR" -maxdepth 1 -name 'WEXYRON-*.AppImage' \
        ! -name '*.download' 2>/dev/null | sort -V | tail -1 || true)

    new_appimage="$SCRIPT_DIR/WEXYRON-${tag}-x86_64.AppImage"

    echo "[launcher] Downloading $tag..."
    local tmp="$new_appimage.download"

    if ! curl -Lf --progress-bar -o "$tmp" "$dl_url"; then
        echo "[launcher] Download failed — keeping current version."
        rm -f "$tmp"
        return 0
    fi

    chmod +x "$tmp"

    # Atomically replace: remove old AppImages, move new one into place
    if [ -n "$old_appimage" ] && [ "$old_appimage" != "$new_appimage" ]; then
        rm -f "$old_appimage"
    fi
    mv -f "$tmp" "$new_appimage"
    echo "$tag" > "$VERSION_FILE"

    echo "[launcher] Updated to $tag."
}

# ---- Launch logic ----

launch_game() {
    # 1) AppImage (binary release) — prefer newest if multiple exist
    local appimage
    appimage=$(find "$SCRIPT_DIR" -maxdepth 1 -name 'WEXYRON-*.AppImage' \
        ! -name '*.download' 2>/dev/null | sort -V | tail -1 || true)

    if [ -n "$appimage" ]; then
        exec "$appimage" "$@"
    fi

    # 2) Compiled binary next to this script
    if [ -f "$SCRIPT_DIR/game" ]; then
        cd "$SCRIPT_DIR"
        exec ./game "$@"
    fi

    # 3) Build/bin from a source checkout
    local built="$SCRIPT_DIR/../build/bin/game"
    if [ -f "$built" ]; then
        cd "$(dirname "$built")"
        exec ./game "$@"
    fi

    echo "[launcher] Error: no game binary found."
    echo "  Expected: WEXYRON-*.AppImage, $SCRIPT_DIR/game, or build/bin/game"
    exit 1
}

# ---- Main ----

SKIP_UPDATE=0
GAME_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --no-update) SKIP_UPDATE=1; shift ;;
        --) shift; GAME_ARGS=("$@"); break ;;
        *)  GAME_ARGS+=("$1"); shift ;;
    esac
done

if [ "$SKIP_UPDATE" -eq 0 ]; then
    do_update_check
fi

launch_game "${GAME_ARGS[@]+"${GAME_ARGS[@]}"}"
