#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${DISPLAY:?Set the Xephyr host DISPLAY}"
TEST_TMP=$(mktemp -d)
WM_PID= SERVER_PID=
cleanup() {
    [ -z "$WM_PID" ] || kill "$WM_PID" 2>/dev/null || true
    [ -z "$SERVER_PID" ] || kill "$SERVER_PID" 2>/dev/null || true
    [ -z "$WM_PID" ] || wait "$WM_PID" 2>/dev/null || true
    [ -z "$SERVER_PID" ] || wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$TEST_TMP"
}
trap cleanup EXIT INT TERM
EVIDENCE_DIR=${EVIDENCE_DIR:-tests/evidence/switcher}
mkdir -p "$EVIDENCE_DIR"
export SWITCHER_EVIDENCE=$(CDPATH= cd -- "$EVIDENCE_DIR" && pwd)
if DISPLAY=:151 xdpyinfo >/dev/null 2>&1; then echo ':151 already in use' >&2; exit 1; fi
cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Wpedantic -Werror ${TEST_SANITIZERS:-} \
    tests/switcher_test.c -o "$TEST_TMP/client" -lX11 -lXtst
cp "${WM_BINARY:-./antoniowm}" "$TEST_TMP/antoniowm"
Xephyr :151 -sw-cursor -no-host-grab -screen 1280x800 -ac -noreset -nolisten tcp >"$EVIDENCE_DIR/server.log" 2>&1 &
SERVER_PID=$!
i=0
until DISPLAY=:151 xdpyinfo >/dev/null 2>&1; do i=$((i+1)); [ "$i" -lt 100 ] || exit 1; sleep 0.05; done
OUTPUT=$(DISPLAY=:151 xrandr --query | sed -n 's/^\([^ ][^ ]*\) connected.*/\1/p' | head -n 1)
DISPLAY=:151 xrandr --setmonitor SWITCHER-LEFT 640/170x800/212+0+0 "$OUTPUT" >"$EVIDENCE_DIR/monitors.log" 2>&1
DISPLAY=:151 xrandr --setmonitor SWITCHER-RIGHT 640/170x800/212+640+0 none >>"$EVIDENCE_DIR/monitors.log" 2>&1
DISPLAY=:151 xrandr --listmonitors >>"$EVIDENCE_DIR/monitors.log"
printf 'focus_follows_mouse=true\ndesktop.count=4\nautostart.count=0\ntaskbar.launcher_count=0\n' >"$TEST_TMP/config"
DISPLAY=:151 ANTONIOWM_CONFIG="$TEST_TMP/config" ANTONIOWM_LOG_XERRORS=1 \
    "$TEST_TMP/antoniowm" >"$EVIDENCE_DIR/wm.log" 2>&1 &
WM_PID=$!
DISPLAY=:151 "$TEST_TMP/client" >"$EVIDENCE_DIR/client.log" 2>&1
[ "$(grep -c 'iniciado en' "$EVIDENCE_DIR/wm.log")" -ge 2 ] || { echo 'Restart was not observed' >&2; exit 1; }
kill "$WM_PID"; wait "$WM_PID"; WM_PID=
if grep -E 'X11 diagnostic|error X11|AddressSanitizer|runtime error:' "$EVIDENCE_DIR/wm.log"; then exit 1; fi
for file in "$EVIDENCE_DIR"/*.ppm; do convert "$file" "${file%.ppm}.png"; rm "$file"; done
cat "$EVIDENCE_DIR/client.log"
