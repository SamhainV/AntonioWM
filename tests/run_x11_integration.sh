#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${DISPLAY:?Set DISPLAY to the host for nested Xephyr}"
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
if DISPLAY=:96 xdpyinfo >/dev/null 2>&1; then echo ':96 already in use' >&2; exit 1; fi
cc -D_POSIX_C_SOURCE=200809L -Iinclude -I. $(pkg-config --cflags xft) \
    -std=c11 -Wall -Wextra -Wpedantic -Werror ${TEST_SANITIZERS:-} \
    tests/x11_integration.c src/icon.c -o "$TEST_TMP/integration" -lX11 -lXtst -lXft -lz -ldl
EVIDENCE_DIR=${EVIDENCE_DIR:-tests/evidence}
mkdir -p "$EVIDENCE_DIR"
Xephyr :96 -sw-cursor -no-host-grab -screen 1280x800 -ac -noreset -nolisten tcp >"$EVIDENCE_DIR/integration-server.log" 2>&1 &
SERVER_PID=$!
i=0
until DISPLAY=:96 xdpyinfo >/dev/null 2>&1; do
    i=$((i+1)); [ "$i" -lt 100 ] || exit 1; sleep 0.05
done
printf 'desktop.count=4\ntaskbar.launcher_count=0\ntaskbar.preview_delay=180\n' >"$TEST_TMP/config"
DISPLAY=:96 ANTONIOWM_CONFIG="$TEST_TMP/config" ANTONIOWM_LOG_XERRORS=1 \
    "${WM_BINARY:-./antoniowm}" >"$EVIDENCE_DIR/integration-wm.log" 2>&1 &
WM_PID=$!
DISPLAY=:96 "$TEST_TMP/integration" "$TEST_TMP/config"
kill "$WM_PID"; wait "$WM_PID"; WM_PID=
if grep -E 'X11 diagnostic|error X11|AddressSanitizer|runtime error:' "$EVIDENCE_DIR/integration-wm.log"; then exit 1; fi
for file in tests/evidence/*.ppm; do convert "$file" "${file%.ppm}.png"; rm "$file"; done
