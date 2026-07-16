#!/bin/sh
set -eu

for command in Xvfb xwininfo xprop; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "lvgl-shell-probe: missing optional command: $command" >&2
        exit 77
    }
done

display=:96
log=${TMPDIR:-/tmp}/msys-shell-lvgl-xvfb.$$.log
shell_log=${TMPDIR:-/tmp}/msys-shell-lvgl.$$.log
Xvfb "$display" -screen 0 320x480x24 -nolisten tcp >"$log" 2>&1 &
xvfb_pid=$!
shell_pid=
cleanup() {
    [ -z "$shell_pid" ] || kill "$shell_pid" 2>/dev/null || true
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
    rm -f "$log" "$shell_log"
}
trap cleanup EXIT INT TERM

sleep 0.2
DISPLAY="$display" MSYS_LOCALE=zh_CN.UTF-8 \
    bin/msys-shell-lvgl --output spi --ui-dir files/share/ui/shell \
    --run-ms 1600 2>"$shell_log" &
shell_pid=$!

attempt=0
tree=
while [ "$attempt" -lt 40 ]; do
    tree=$(DISPLAY="$display" xwininfo -root -tree 2>/dev/null || true)
    count=$(printf '%s\n' "$tree" | grep -c 'MSYS \(Launcher\|Chrome\|Navigation\|Recents\)' || true)
    [ "$count" -eq 4 ] && break
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "${count:-0}" -eq 4 ] || {
    echo "lvgl-shell-probe: expected four X11 surfaces" >&2
    cat "$shell_log" >&2
    exit 1
}

launcher=$(printf '%s\n' "$tree" | awk '/MSYS Launcher/{print $1; exit}')
chrome=$(printf '%s\n' "$tree" | awk '/MSYS Chrome/{print $1; exit}')
navigation=$(printf '%s\n' "$tree" | awk '/MSYS Navigation/{print $1; exit}')
recents=$(printf '%s\n' "$tree" | awk '/MSYS Recents/{print $1; exit}')

check_role() {
    window=$1
    role=$2
    DISPLAY="$display" xprop -id "$window" _MSYS_WINDOW_ROLE |
        grep -q "${role}"
}
check_geometry() {
    window=$1
    width=$2
    height=$3
    x=$4
    y=$5
    geometry=$(DISPLAY="$display" xwininfo -id "$window")
    printf '%s\n' "$geometry" | grep -q "Width: $width"
    printf '%s\n' "$geometry" | grep -q "Height: $height"
    printf '%s\n' "$geometry" | grep -q "Absolute upper-left X:  $x"
    printf '%s\n' "$geometry" | grep -q "Absolute upper-left Y:  $y"
}

check_role "$launcher" launcher
check_role "$chrome" system-chrome
check_role "$navigation" navigation-bar
check_role "$recents" task-switcher
check_geometry "$launcher" 320 396 0 42
check_geometry "$chrome" 320 42 0 0
check_geometry "$navigation" 320 42 0 438
check_geometry "$recents" 320 396 0 42

# The clock is allowed to damage its own label once per second.  The launcher
# has no idle timer, so its last exact LVGL invalid area must remain unchanged.
sleep 0.45
first=$(DISPLAY="$display" xprop -id "$launcher" _MSYS_LVGL_LAST_FLUSH)
sleep 0.30
second=$(DISPLAY="$display" xprop -id "$launcher" _MSYS_LVGL_LAST_FLUSH)
[ "$first" = "$second" ] || {
    echo "lvgl-shell-probe: idle launcher kept refreshing" >&2
    exit 1
}

wait "$shell_pid"
shell_pid=
echo "lvgl-shell-probe: ok"
