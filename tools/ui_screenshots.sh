#!/bin/bash
# Render the menu screens on a headless Xvfb display and save a screenshot of
# each one (ImageMagick's `import` grabs the X framebuffer). Needs Xvfb and
# ImageMagick; screenshots land in $OUT (default /tmp/cube_ui).
#
# The screens are driven by the built-in scripted run (CUBE_MENU_TEST), so no
# keyboard is needed and the result is deterministic. Run from the project
# root after building: tools/ui_screenshots.sh
set -u
cd "$(dirname "$0")/.." || exit 1
OUT=${OUT:-/tmp/cube_ui}
DISP=${DISP:-:92}
mkdir -p "$OUT"

if ! python3 -c "from Xlib import display; display.Display('$DISP').close()" 2>/dev/null; then
    num="${DISP#:}"
    rm -f "/tmp/.X${num}-lock" "/tmp/.X11-unix/X${num}"
    Xvfb "$DISP" -screen 0 1600x900x24 >"$OUT/xvfb.log" 2>&1 &
    sleep 2
fi

export DISPLAY=$DISP

shoot() {   # shoot <name>
    import -window root -crop 800x600+0+0 "$OUT/$1.png"
}

run_steps() {  # run_steps <name> <steps...>
    local name=$1; shift
    local script
    script=$(IFS=';'; echo "$*")
    rm -f "$OUT/steps.log"
    LIBGL_ALWAYS_SOFTWARE=1 CUBE_MENU_TEST="$script" ./build/bin/game >"$OUT/steps.log" 2>&1 &
    local pid=$!
    for _ in $(seq 1 60); do
        grep -q '^ready' "$OUT/steps.log" 2>/dev/null && break
        sleep 0.2
    done
    sleep 0.4
    shoot "$name"
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    grep -E '^===|^>|^  [A-Z]|^--' "$OUT/steps.log" | tail -40
    echo "--- $name"
}

run_steps main       menu:main
run_steps settings   menu:settings
run_steps settings2  menu:settings down down down down down down enter
run_steps maps       menu:maps
run_steps server     menu:server
run_steps controls   menu:settings select:CONTROLS enter
run_steps capture    menu:settings select:CONTROLS enter down down down down enter
run_steps capture2   menu:settings select:CONTROLS enter down down down down enter capture:70
