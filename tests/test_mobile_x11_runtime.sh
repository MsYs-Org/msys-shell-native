#!/bin/sh
set -eu

for command in Xvfb xdpyinfo xwininfo xprop; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "test_mobile_x11_runtime: missing $command" >&2
        exit 77
    fi
done

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
POLICY=${MSYS_X11_POLICY_BINARY:-$ROOT/../msys-x11-session/bin/msys-x11-policy}
PYTHON=${MSYS_TEST_PYTHON:-python3}
PORTRAIT_DISPLAY=${MSYS_TEST_DISPLAY_NUMBER:-96}
LANDSCAPE_DISPLAY=${MSYS_TEST_LANDSCAPE_DISPLAY_NUMBER:-95}
TMP=$(mktemp -d)
xvfb_pid=
policy_pid=

cleanup()
{
    if test -n "$policy_pid"; then
        kill "$policy_pid" 2>/dev/null || true
        wait "$policy_pid" 2>/dev/null || true
    fi
    if test -n "$xvfb_pid"; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

test -x "$ROOT/bin/msys-shell-native" || {
    echo "test_mobile_x11_runtime: native Shell is not built" >&2
    exit 1
}
test -x "$POLICY" || {
    echo "test_mobile_x11_runtime: native X11 policy is not built: $POLICY" >&2
    exit 1
}

run_case()
{
    display_number=$1
    geometry=$2
    orientation=$3
    expected_name=$4
    expected_layout=$5
    xvfb_display=:$display_number
    if test "${MSYS_TEST_XVFB_TCP:-0}" = 1; then
        export DISPLAY=127.0.0.1:$display_number
        xvfb_transport='-listen tcp -nolisten unix'
    else
        export DISPLAY=$xvfb_display
        xvfb_transport='-nolisten tcp'
    fi

    # TCP mode is useful on WSLg where /tmp/.X11-unix is a read-only mount
    # without the sticky bit. The default remains a local Unix socket.
    Xvfb "$xvfb_display" -screen 0 "${geometry}x24" -ac $xvfb_transport \
        >"$TMP/xvfb-$expected_name.log" 2>&1 &
    xvfb_pid=$!
    ready=0
    for _attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        if xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep 0.1
    done
    test "$ready" = 1 || {
        cat "$TMP/xvfb-$expected_name.log" >&2
        exit 1
    }

    MSYS_LAYOUT_PROFILE=mobile \
    MSYS_ORIENTATION="$orientation" \
    MSYS_INSETS=auto \
        "$POLICY" >"$TMP/policy-$expected_name.log" 2>&1 &
    policy_pid=$!
    ready=0
    for _attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        layout=$($POLICY --print-layout 2>/dev/null || true)
        case "$layout" in
            *"$expected_layout"*)
                ready=1
                break
                ;;
        esac
        sleep 0.1
    done
    test "$ready" = 1 || {
        cat "$TMP/policy-$expected_name.log" >&2
        echo "test_mobile_x11_runtime: $expected_name layout did not become ready: $layout" >&2
        exit 1
    }

    for mode in buttons pill; do
        DISPLAY="$DISPLAY" \
        PYTHONDONTWRITEBYTECODE=1 \
        MSYS_LAYOUT_PROFILE=mobile \
        MSYS_NATIVE_NAV_MODE="$mode" \
        MSYS_NATIVE_CLOCK_DEBUG=1 \
        MSYS_PROBE_INPUT_MODE="$mode" \
        MSYS_PROBE_EXPECT_MOBILE="$expected_name" \
        MSYS_X11_POLICY_DEBUG="$POLICY" \
            "$PYTHON" "$ROOT/tests/runtime_probe.py" \
            "$ROOT/bin/msys-shell-native" >"$TMP/$expected_name-$mode.json"
    done

    kill "$policy_pid" 2>/dev/null || true
    wait "$policy_pid" 2>/dev/null || true
    policy_pid=
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
    xvfb_pid=
}

case "${MSYS_TEST_ORIENTATION_CASE:-all}" in
    all|portrait)
        run_case "$PORTRAIT_DISPLAY" 320x480 portrait portrait \
            "workarea=0,42,320,396;navigation=bottom;navigation_region=0,438,320,42"
        ;;
esac
case "${MSYS_TEST_ORIENTATION_CASE:-all}" in
    all|landscape)
        run_case "$LANDSCAPE_DISPLAY" 480x320 landscape landscape \
            "workarea=0,42,438,278;navigation=right;navigation_region=438,42,42,278"
        ;;
esac

for result in "$TMP"/*.json; do
    cat "$result"
    printf '\n'
done
echo "test_mobile_x11_runtime: ok"
