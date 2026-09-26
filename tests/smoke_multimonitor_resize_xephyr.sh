#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_MULTIMONITOR_TEST_DISPLAY:-:93}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_MULTIMONITOR_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
CLIENT_PID=

cleanup()
{
    for process in "$CLIENT_PID" "$WM_PID" "$XEPHYR_PID"; do
        if [ -n "$process" ]; then
            kill "$process" 2>/dev/null || true
        fi
    done
    for process in "$CLIENT_PID" "$WM_PID" "$XEPHYR_PID"; do
        if [ -n "$process" ]; then
            wait "$process" 2>/dev/null || true
        fi
    done
    rm -rf "$TEST_TMP"
}

fail()
{
    echo "$1" >&2
    if [ -s "$TEST_TMP/wm.log" ]; then
        echo "Registro de AntonioWM:" >&2
        sed -n '1,220p' "$TEST_TMP/wm.log" >&2
    fi
    if [ -s "$TEST_TMP/xephyr.log" ]; then
        echo "Ultimas lineas de Xephyr:" >&2
        tail -n 50 "$TEST_TMP/xephyr.log" >&2
    fi
    exit 1
}

window_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$1" |
        sed -n "s/^$2=//p"
}

xwin_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n "s/^  $2:[[:space:]]*//p"
}

frame_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
}

child_named()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n "s/^[[:space:]]*\(0x[0-9a-fA-F]*\) \"$2\".*/\1/p" |
        head -n 1
}

wait_for_window()
{
    attempt=0
    while :; do
        found=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xdotool search --name '^AWM MultiMonitor Resize$' 2>/dev/null |
            head -n 1 || true)
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "No aparecio el cliente multimonitor."
        sleep 0.1
    done
}

wait_for_management()
{
    managed=$1
    attempt=0
    while :; do
        frame=$(frame_of "$managed" || true)
        if [ -n "$frame" ] &&
           DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$managed" WM_STATE \
               2>/dev/null | grep -q '^WM_STATE'; then
            printf '%s\n' "$frame"
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "AntonioWM no reparento el cliente."
        sleep 0.1
    done
}

wait_for_geometry()
{
    geometry_window=$1
    expected_x=$2
    expected_y=$3
    expected_width=$4
    expected_height=$5
    attempt=0

    while :; do
        actual_x=$(window_field "$geometry_window" X)
        actual_y=$(window_field "$geometry_window" Y)
        actual_width=$(window_field "$geometry_window" WIDTH)
        actual_height=$(window_field "$geometry_window" HEIGHT)
        if [ "$actual_x" = "$expected_x" ] &&
           [ "$actual_y" = "$expected_y" ] &&
           [ "$actual_width" = "$expected_width" ] &&
           [ "$actual_height" = "$expected_height" ]; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 50 ] ||
            fail "Geometria $actual_width x $actual_height + $actual_x + $actual_y; se esperaba $expected_width x $expected_height + $expected_x + $expected_y."
        sleep 0.1
    done
}

drag_handle()
{
    handle_name=$1
    handle_x=$2
    handle_y=$3
    delta_x=$4
    delta_y=$5
    handle=$(child_named "$FRAME" "$handle_name")

    [ -n "$handle" ] || fail "Falta la zona '$handle_name'."
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --sync --window "$handle" "$handle_x" "$handle_y"
    sleep 0.08
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousedown 1
    sleep 0.12
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove_relative --sync -- "$delta_x" "$delta_y"
    sleep 0.12
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mouseup 1
    sleep 0.15
}

trap cleanup EXIT INT TERM
for command in Xephyr xdpyinfo xdotool xprop xwininfo xrandr; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Falta el comando requerido: $command" >&2
        exit 1
    }
done
[ -n "${DISPLAY:-}" ] || {
    echo "Xephyr necesita un servidor X anfitrion." >&2
    exit 1
}

if [ "${ANTONIOWM_TEST_TCP_ONLY:-0}" = "1" ]; then
    Xephyr "$NESTED_SERVER_DISPLAY" -screen 1200x700 -ac -br -noreset \
        -listen tcp -nolisten local -nolisten unix \
        >"$TEST_TMP/xephyr.log" 2>&1 &
else
    Xephyr "$NESTED_SERVER_DISPLAY" -screen 1200x700 -ac -br -noreset \
        >"$TEST_TMP/xephyr.log" 2>&1 &
fi
XEPHYR_PID=$!
attempt=0
while ! DISPLAY="$NESTED_CLIENT_DISPLAY" xdpyinfo >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "Xephyr no arranco."
    sleep 0.1
done

OUTPUT=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr --query |
    sed -n 's/^\([^ ][^ ]*\) connected.*/\1/p' | head -n 1)
[ -n "$OUTPUT" ] || fail "Xephyr no publico una salida RandR."
DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr \
    --setmonitor AWM-LEFT 600/160x700/186+0+0 "$OUTPUT" \
    >"$TEST_TMP/xrandr.log" 2>&1
DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr \
    --setmonitor AWM-RIGHT 600/160x700/186+600+0 none \
    >>"$TEST_TMP/xrandr.log" 2>&1
MONITORS=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr --listmonitors)
printf '%s\n' "$MONITORS" | grep -q '^Monitors: 2$' ||
    fail "RandR no creo exactamente dos monitores logicos."
printf '%s\n' "$MONITORS" | grep -q '600/160x700/186+0+0' ||
    fail "Falta el monitor izquierdo de prueba."
printf '%s\n' "$MONITORS" | grep -q '600/160x700/186+600+0' ||
    fail "Falta el monitor derecho de prueba."

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/antoniowm" \
    >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.4
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino durante el arranque."

PANEL=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Panel$' 2>/dev/null | head -n 1 || true)
[ -n "$PANEL" ] || fail "No se creo el panel en el monitor primario."
wait_for_geometry "$PANEL" 0 672 600 28
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_WORKAREA |
    grep -q '= 0, 0, 1200, 672' ||
    fail "_NET_WORKAREA global no refleja la reserva conservadora del panel."

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "AWM MultiMonitor Resize" "300x200+100+100" \
    >"$TEST_TMP/client.log" 2>&1 &
CLIENT_PID=$!
WINDOW=$(wait_for_window)
FRAME=$(wait_for_management "$WINDOW")
wait_for_geometry "$FRAME" 100 100 304 226

# Maximizar usa sólo el workarea izquierdo: y=0 y 28 px para el panel.
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" add MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 0 0 600 672
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" remove MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 100 100 304 226

# El monitor derecho no contiene panel y conserva sus 700 px completos.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowmove "$WINDOW" 700 100
wait_for_geometry "$FRAME" 700 100 304 226
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" add MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 600 0 600 700
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" remove MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 700 100 304 226

# Fullscreen cubre el monitor derecho y restaura exacto.
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" add FULLSCREEN
wait_for_geometry "$FRAME" 600 0 600 700
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" remove FULLSCREEN
wait_for_geometry "$FRAME" 700 100 304 226

# Mayor interseccion: 100 px quedan a la izquierda y 204 px a la derecha.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowmove "$WINDOW" 500 140
wait_for_geometry "$FRAME" 500 140 304 226
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" add MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 600 0 600 700
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" remove MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 500 140 304 226

for handle_name in "AntonioWM Resize Left" "AntonioWM Resize Right" \
                   "AntonioWM Resize Bottom" \
                   "AntonioWM Resize Bottom Left" \
                   "AntonioWM Resize Bottom Right"; do
    [ -n "$(child_named "$FRAME" "$handle_name")" ] ||
        fail "No se creo '$handle_name'."
done
RIGHT_HANDLE=$(child_named "$FRAME" "AntonioWM Resize Right")
[ "$(xwin_field "$RIGHT_HANDLE" Width)" = 7 ] ||
    fail "La zona derecha no mide los 7 px configurados."
RIGHT_HANDLE_X=$(xwin_field "$RIGHT_HANDLE" 'Relative upper-left X')
EXPECTED_HANDLE_X=$(( $(window_field "$FRAME" WIDTH) - 7 ))
[ "$RIGHT_HANDLE_X" = "$EXPECTED_HANDLE_X" ] ||
    fail "La zona derecha esta en x=$RIGHT_HANDLE_X; se esperaba x=$EXPECTED_HANDLE_X dentro del frame."

# Borde derecho: crecer y reducir conserva x, y y altura.
RIGHT_X=$(window_field "$FRAME" X)
RIGHT_Y=$(window_field "$FRAME" Y)
RIGHT_WIDTH=$(window_field "$FRAME" WIDTH)
RIGHT_HEIGHT=$(window_field "$FRAME" HEIGHT)
drag_handle "AntonioWM Resize Right" 3 30 80 0
wait_for_geometry "$FRAME" "$RIGHT_X" "$RIGHT_Y" \
    $((RIGHT_WIDTH + 80)) "$RIGHT_HEIGHT"
drag_handle "AntonioWM Resize Right" 3 30 -50 0
wait_for_geometry "$FRAME" "$RIGHT_X" "$RIGHT_Y" \
    $((RIGHT_WIDTH + 30)) "$RIGHT_HEIGHT"

# Esquina inferior derecha modifica ambos ejes sin mover el origen.
CORNER_X=$(window_field "$FRAME" X)
CORNER_Y=$(window_field "$FRAME" Y)
CORNER_WIDTH=$(window_field "$FRAME" WIDTH)
CORNER_HEIGHT=$(window_field "$FRAME" HEIGHT)
drag_handle "AntonioWM Resize Bottom Right" 3 3 30 40
wait_for_geometry "$FRAME" "$CORNER_X" "$CORNER_Y" \
    $((CORNER_WIDTH + 30)) $((CORNER_HEIGHT + 40))

# Esquina inferior izquierda conserva el borde derecho y respeta increments.
LEFT_X=$(window_field "$FRAME" X)
LEFT_Y=$(window_field "$FRAME" Y)
LEFT_WIDTH=$(window_field "$FRAME" WIDTH)
LEFT_HEIGHT=$(window_field "$FRAME" HEIGHT)
drag_handle "AntonioWM Resize Bottom Left" 3 3 -40 30
wait_for_geometry "$FRAME" $((LEFT_X - 40)) "$LEFT_Y" \
    $((LEFT_WIDTH + 40)) $((LEFT_HEIGHT + 30))
CLIENT_WIDTH=$(window_field "$WINDOW" WIDTH)
CLIENT_HEIGHT=$(window_field "$WINDOW" HEIGHT)
[ $(((CLIENT_WIDTH - 120) % 10)) -eq 0 ] &&
    [ $(((CLIENT_HEIGHT - 80) % 10)) -eq 0 ] ||
    fail "El resize por frame no respeto los increments ICCCM."

# El borde derecho sigue accesible cerca del limite de cada monitor.
for near_x in 445 1045; do
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowsize "$WINDOW" 140 120
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowmove "$WINDOW" "$near_x" 180
    wait_for_geometry "$FRAME" "$near_x" 180 144 146
    drag_handle "AntonioWM Resize Right" 3 25 10 0
    wait_for_geometry "$FRAME" "$near_x" 180 154 146
done

# Alt+boton derecho conserva su ruta independiente del hit-testing del frame.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowactivate --sync "$WINDOW"
ALT_WIDTH=$(window_field "$FRAME" WIDTH)
ALT_HEIGHT=$(window_field "$FRAME" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --sync --window "$WINDOW" 125 95
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool keydown Alt mousedown 3
sleep 0.12
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove_relative --sync 40 30
sleep 0.12
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mouseup 3 keyup Alt
sleep 0.15
[ "$(window_field "$FRAME" WIDTH)" -gt "$ALT_WIDTH" ] &&
    [ "$(window_field "$FRAME" HEIGHT)" -gt "$ALT_HEIGHT" ] ||
    fail "Alt+boton derecho dejo de redimensionar."

# Tras un resize, maximizar/restaurar mantiene la nueva geometria exacta.
FINAL_X=$(window_field "$FRAME" X)
FINAL_Y=$(window_field "$FRAME" Y)
FINAL_WIDTH=$(window_field "$FRAME" WIDTH)
FINAL_HEIGHT=$(window_field "$FRAME" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" add MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" 600 0 600 700
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    state "$WINDOW" remove MAXIMIZED_VERT MAXIMIZED_HORZ
wait_for_geometry "$FRAME" "$FINAL_X" "$FINAL_Y" "$FINAL_WIDTH" "$FINAL_HEIGHT"

kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino durante la prueba."
echo "Prueba Xephyr/RandR superada: maximizado y fullscreen por monitor, restauracion exacta y resize por bordes/esquinas."
