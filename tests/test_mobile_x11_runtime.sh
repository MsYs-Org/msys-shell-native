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
DISPLAY_NUMBER=${MSYS_TEST_DISPLAY_NUMBER:-96}
export DISPLAY=:$DISPLAY_NUMBER
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

Xvfb "$DISPLAY" -screen 0 320x480x24 -ac -nolisten tcp \
    >"$TMP/xvfb.log" 2>&1 &
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
    cat "$TMP/xvfb.log" >&2
    exit 1
}

MSYS_LAYOUT_PROFILE=mobile \
MSYS_ORIENTATION=portrait \
MSYS_INSETS=auto \
    "$POLICY" >"$TMP/policy.log" 2>&1 &
policy_pid=$!
ready=0
for _attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    layout=$($POLICY --print-layout 2>/dev/null || true)
    case "$layout" in
        *"workarea=0,42,320,396;navigation=bottom;navigation_region=0,438,320,42"*)
            ready=1
            break
            ;;
    esac
    sleep 0.1
done
test "$ready" = 1 || {
    cat "$TMP/policy.log" >&2
    echo "test_mobile_x11_runtime: mobile layout did not become ready: $layout" >&2
    exit 1
}

for mode in buttons pill; do
    DISPLAY="$DISPLAY" \
    PYTHONDONTWRITEBYTECODE=1 \
    MSYS_LAYOUT_PROFILE=mobile \
    MSYS_NATIVE_NAV_MODE="$mode" \
    MSYS_PROBE_INPUT_MODE="$mode" \
    MSYS_PROBE_EXPECT_MOBILE=1 \
    MSYS_X11_POLICY_DEBUG="$POLICY" \
        "$PYTHON" "$ROOT/tests/runtime_probe.py" \
        "$ROOT/bin/msys-shell-native" >"$TMP/$mode.json"
done

printf '%s\n' "$(cat "$TMP/buttons.json")" "$(cat "$TMP/pill.json")"
echo "test_mobile_x11_runtime: ok"
