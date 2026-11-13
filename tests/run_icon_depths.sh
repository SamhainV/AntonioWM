#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
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
cc -D_POSIX_C_SOURCE=200809L -Iinclude -I. $(pkg-config --cflags xft) -std=c11 -Wall -Wextra -Werror \
    tests/x11_icon_depth.c src/icon.c -o "$TEST_TMP/depth" -lX11 -lXft -lz
printf 'desktop.count=1\ntaskbar.launcher_count=0\n' >"$TEST_TMP/config"
for depth in 16 8; do
    screen=:121; [ "$depth" = 16 ] || screen=:122
    if DISPLAY=$screen xdpyinfo >/dev/null 2>&1; then echo "$screen already in use" >&2; exit 1; fi
    Xephyr "$screen" -sw-cursor -no-host-grab -screen "800x600x$depth" -ac -noreset -nolisten tcp >"tests/evidence/depth$depth-server.log" 2>&1 &
    SERVER_PID=$!
    i=0
    until DISPLAY=$screen xdpyinfo >/dev/null 2>&1; do i=$((i+1)); [ "$i" -lt 100 ] || exit 1; sleep 0.05; done
    DISPLAY=$screen ANTONIOWM_CONFIG="$TEST_TMP/config" ANTONIOWM_LOG_XERRORS=1 \
        ./antoniowm >"tests/evidence/depth$depth-wm.log" 2>&1 &
    WM_PID=$!
    sleep 0.5
    DISPLAY=$screen "$TEST_TMP/depth"
    sleep 0.1
    kill "$WM_PID"; wait "$WM_PID"; WM_PID=
    kill "$SERVER_PID"; wait "$SERVER_PID" 2>/dev/null || true; SERVER_PID=
    convert "tests/evidence/depth$depth.xwd" "tests/evidence/depth$depth.png"
    rm "tests/evidence/depth$depth.xwd"
done
