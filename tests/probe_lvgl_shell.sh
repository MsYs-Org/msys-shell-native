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
probe_icon=${TMPDIR:-/tmp}/msys-shell-lvgl-icon.$$.ppm
probe_state=${TMPDIR:-/tmp}/msys-shell-lvgl-state.$$
mkdir "$probe_state"
printf 'P6\n1 1\n255\n\000\377\000' >"$probe_icon"
Xvfb "$display" -screen 0 320x480x24 -nolisten tcp >"$log" 2>&1 &
xvfb_pid=$!
shell_pid=
cleanup() {
    [ -z "$shell_pid" ] || kill "$shell_pid" 2>/dev/null || true
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
    rm -f "$log" "$shell_log" "$probe_icon"
    rm -f "$probe_state/launcher-layout.v1"
    rmdir "$probe_state" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 0.2
DISPLAY="$display" MSYS_LOCALE=zh_CN.UTF-8 \
    MSYS_COMPONENT_STATE_DIR="$probe_state" \
    bin/msys-shell-lvgl --output spi --ui-dir files/share/ui/shell \
    --probe-launcher "$probe_icon" --run-ms 3200 2>"$shell_log" &
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

# The probe inventory is rendered before mapping. Assert the live LVGL object
# geometry rather than only checking that the outer X11 window exists.
attempt=0
geometry_line=
while [ "$attempt" -lt 20 ]; do
    geometry_line=$(grep 'launcher geometry apps=' "$shell_log" | tail -n 1 || true)
    [ -n "$geometry_line" ] && break
    attempt=$((attempt + 1))
    sleep 0.05
done
[ -n "$geometry_line" ] || {
    echo "lvgl-shell-probe: missing launcher geometry" >&2
    cat "$shell_log" >&2
    exit 1
}
set -- $(printf '%s\n' "$geometry_line" | awk '
    {
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^visible=/) { split($i, a, "="); visible=a[2] }
            if ($i ~ /^children=/) { split($i, a, "="); children=a[2] }
            if ($i ~ /^root=/) { split($i, a, "[=,x]"); rx=a[2]; ry=a[3]; rw=a[4]; rh=a[5] }
            if ($i ~ /^grid=/) { split($i, a, "[=,x]"); gx=a[2]; gy=a[3]; gw=a[4]; gh=a[5] }
            if ($i ~ /^content=/) { split($i, a, "[=x]"); cw=a[2]; ch=a[3] }
            if ($i ~ /^tile=/) { split($i, a, "[=,x]"); tx=a[2]; ty=a[3]; tw=a[4]; th=a[5] }
        }
        print visible, children, rx, ry, rw, rh, gx, gy, gw, gh, cw, ch, tx, ty, tw, th
    }')
[ "$1" -gt 0 ] && [ "$1" -lt 8 ] && [ "$2" -eq "$1" ]
printf '%s\n' "$geometry_line" | grep -Eq 'page=1/[2-9][0-9]*'
[ "$9" -gt 0 ] && [ "${10}" -gt 0 ] && [ "${11}" -gt 0 ] && [ "${12}" -gt 0 ]
[ "$7" -ge "$3" ] && [ "$8" -ge "$4" ]
[ $(( $7 + $9 )) -le $(( $3 + $5 )) ]
[ $(( $8 + ${10} )) -le $(( $4 + $6 )) ]
[ "${13}" -ge "$7" ] && [ "${14}" -ge "$8" ]
[ $(( ${13} + ${15} )) -le $(( $7 + $9 )) ]
[ $(( ${14} + ${16} )) -le $(( $8 + ${10} )) ]

# On a physical 320-pixel Launcher the hint owns a separate row below the
# title/pager row. This live-object assertion catches XML flex regressions
# which make the Chinese hint render underneath the pager controls.
header_line=$(grep 'launcher header hint=' "$shell_log" | tail -n 1 || true)
printf '%s\n' "$header_line" | grep -q 'overlap=0'
set -- $(printf '%s\n' "$header_line" | awk '
    {
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^hint=/) { split($i, a, "[=,x]"); hw=a[4]; hh=a[5] }
            if ($i ~ /^pager=/) { split($i, a, "[=,x]"); pw=a[4]; ph=a[5] }
        }
        print hw, hh, pw, ph
    }')
[ "$1" -gt 0 ] && [ "$2" -gt 0 ] && [ "$3" -gt 0 ] && [ "$4" -gt 0 ]

# Mapping can produce one late Expose after the window first becomes visible.
# Wait for that finite startup damage to settle, then observe longer than one
# clock period. The clock may damage Chrome, never the launcher surface.
attempt=0
stable=0
previous=$(DISPLAY="$display" xprop -id "$launcher" _MSYS_LVGL_LAST_FLUSH)
while [ "$attempt" -lt 10 ]; do
    sleep 0.10
    current=$(DISPLAY="$display" xprop -id "$launcher" _MSYS_LVGL_LAST_FLUSH)
    if [ "$current" = "$previous" ]; then
        stable=$((stable + 1))
        [ "$stable" -ge 3 ] && break
    else
        stable=0
    fi
    previous=$current
    attempt=$((attempt + 1))
done
[ "$stable" -ge 3 ] || {
    echo "lvgl-shell-probe: launcher did not settle after mapping" >&2
    exit 1
}
first=$current
sleep 1.10
second=$(DISPLAY="$display" xprop -id "$launcher" _MSYS_LVGL_LAST_FLUSH)
[ "$first" = "$second" ] || {
    echo "lvgl-shell-probe: idle launcher kept refreshing" >&2
    exit 1
}

# The 1x1 pure-green probe icon is scaled into every tile. Check pixels only
# after startup damage has settled so XGetImage cannot race the first flush.
DISPLAY="$display" build/x11-pixel-probe "$launcher" 128 || {
    echo "lvgl-shell-probe: launcher tile pixels are not visible" >&2
    cat "$shell_log" >&2
    exit 1
}

wait "$shell_pid"
shell_pid=
echo "lvgl-shell-probe: ok"
