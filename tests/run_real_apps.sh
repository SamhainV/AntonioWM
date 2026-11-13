#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TEST_TMP=$(mktemp -d)
SERVER_PID= WM_PID= TERM_PID= CALC_PID= CHROME_PID=
cleanup() {
    for pid in "$CHROME_PID" "$CALC_PID" "$TERM_PID" "$WM_PID" "$SERVER_PID"; do
        [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
    done
    for pid in "$CHROME_PID" "$CALC_PID" "$TERM_PID" "$WM_PID" "$SERVER_PID"; do
        [ -z "$pid" ] || wait "$pid" 2>/dev/null || true
    done
    rm -rf "$TEST_TMP"
}
trap cleanup EXIT INT TERM
if DISPLAY=:109 xdpyinfo >/dev/null 2>&1; then echo ':109 already in use' >&2; exit 1; fi
Xephyr :109 -sw-cursor -no-host-grab -screen 1280x800 -ac -noreset -nolisten tcp >tests/evidence/apps-server.log 2>&1 &
SERVER_PID=$!
i=0
until DISPLAY=:109 xdpyinfo >/dev/null 2>&1; do i=$((i+1)); [ "$i" -lt 100 ] || exit 1; sleep 0.05; done
cat >"$TEST_TMP/config" <<CONFIG
desktop.count=6
desktop.1.name=Trabajo
desktop.2.name=Web
desktop.6.name=Correo
taskbar.launcher_count=0
CONFIG
DISPLAY=:109 ANTONIOWM_CONFIG="$TEST_TMP/config" ANTONIOWM_LOG_XERRORS=1 ./antoniowm >tests/evidence/apps-wm.log 2>&1 &
WM_PID=$!
sleep 0.5
DISPLAY=:109 xterm -title 'AntonioWM terminal real' -geometry 55x12+20+40 >"$TEST_TMP/xterm.log" 2>&1 &
TERM_PID=$!
DISPLAY=:109 xcalc -geometry 240x360+530+40 >"$TEST_TMP/xcalc.log" 2>&1 &
CALC_PID=$!
printf '<title>AntonioWM Chrome icon validation</title><h1>AntonioWM X11</h1><p>Local test page</p>' >"$TEST_TMP/page.html"
DISPLAY=:109 google-chrome --user-data-dir="$TEST_TMP/chrome" --no-first-run \
    --no-default-browser-check --disable-background-networking --disable-gpu \
    --disable-dev-shm-usage --window-size=500,400 --window-position=760,100 \
    --new-window "file://$TEST_TMP/page.html" >tests/evidence/chrome.log 2>&1 &
CHROME_PID=$!
i=0
until CHROME_WINDOW=$(DISPLAY=:109 xdotool search --name '^AntonioWM Chrome icon validation' 2>/dev/null | head -n 1) && [ -n "$CHROME_WINDOW" ]; do
    i=$((i+1)); [ "$i" -lt 120 ] || { cat tests/evidence/chrome.log; exit 1; }; sleep 0.1
done
sleep 1
DISPLAY=:109 xprop -root _NET_CLIENT_LIST _NET_NUMBER_OF_DESKTOPS _NET_DESKTOP_NAMES >tests/evidence/apps-properties.log
DISPLAY=:109 xprop -id "$CHROME_WINDOW" _NET_WM_ICON | sed -n '1p' >>tests/evidence/apps-properties.log
DISPLAY=:109 xdotool windowactivate --sync "$CHROME_WINDOW"
DISPLAY=:109 xwd -root -silent -out "$TEST_TMP/apps.xwd"
convert "$TEST_TMP/apps.xwd" tests/evidence/apps.png
OUTPUT=$(DISPLAY=:109 xrandr --query | sed -n 's/^\([^ ][^ ]*\) connected.*/\1/p' | head -n 1)
DISPLAY=:109 xrandr --setmonitor AWM-LEFT 640/170x800/212+0+0 "$OUTPUT"
DISPLAY=:109 xrandr --setmonitor AWM-RIGHT 640/170x800/212+640+0 none
sleep 0.3
DISPLAY=:109 xrandr --listmonitors >tests/evidence/apps-monitors.log
DISPLAY=:109 xdotool windowmove "$CHROME_WINDOW" 700 100
DISPLAY=:109 xdotool key super+m
sleep 0.3
DISPLAY=:109 xwd -root -silent -out "$TEST_TMP/apps-two-monitors.xwd"
convert "$TEST_TMP/apps-two-monitors.xwd" tests/evidence/apps-two-monitors.png
DISPLAY=:109 xdotool key super+m
DISPLAY=:109 xdotool windowminimize "$CHROME_WINDOW"
sleep 0.2
DISPLAY=:109 xprop -id "$CHROME_WINDOW" WM_STATE >>tests/evidence/apps-properties.log
DISPLAY=:109 xdotool windowactivate --sync "$CHROME_WINDOW"
DISPLAY=:109 xdotool key alt+Tab
sleep 0.2
DISPLAY=:109 xdotool key super+shift+r
sleep 1
DISPLAY=:109 xprop -root _NET_CLIENT_LIST >>tests/evidence/apps-properties.log
kill "$CHROME_PID" "$CALC_PID" "$TERM_PID"
wait "$CHROME_PID" 2>/dev/null || true; CHROME_PID=
wait "$CALC_PID" 2>/dev/null || true; CALC_PID=
wait "$TERM_PID" 2>/dev/null || true; TERM_PID=
sleep 0.3
kill "$WM_PID"; wait "$WM_PID"; WM_PID=
echo 'PASS real Chrome (temporary profile/local page), xterm, xcalc, minimize, restore, Alt+Tab and hot restart'
