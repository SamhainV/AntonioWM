#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_TRAY_TEST_DISPLAY:-:93}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_TRAY_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
NORMAL_PID=
MOCK_PID=
ICON_PIDS=
PANEL_WINDOW=
ROOT_WINDOW=

cleanup()
{
    set +e
    for process in $ICON_PIDS "$NORMAL_PID" "$MOCK_PID" "$WM_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && kill "$process" 2>/dev/null
    done
    for process in $ICON_PIDS "$NORMAL_PID" "$MOCK_PID" "$WM_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && wait "$process" 2>/dev/null
    done
    rm -rf "$TEST_TMP"
}

fail()
{
    echo "$1" >&2
    if [ -s "$TEST_TMP/wm.log" ]; then
        echo "Registro de AntonioWM:" >&2
        sed -n '1,300p' "$TEST_TMP/wm.log" >&2
    fi
    for log in "$TEST_TMP"/*.log; do
        [ -s "$log" ] || continue
        [ "$log" = "$TEST_TMP/wm.log" ] && continue
        echo "Registro $(basename "$log"):" >&2
        sed -n '1,120p' "$log" >&2
    done
    exit 1
}

window_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$1" |
        sed -n "s/^$2=//p"
}

parent_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
}

relative_y()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Relative upper-left Y:[[:space:]]*//p'
}

relative_x()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Relative upper-left X:[[:space:]]*//p'
}

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
}

wait_for_file()
{
    path=$1
    attempt=0
    while [ ! -s "$path" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No aparecio $path."
        sleep 0.05
    done
}

wait_for_window()
{
    title=$1
    attempt=0
    while :; do
        found=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xdotool search --name "^$title$" 2>/dev/null | head -n 1 || true)
        if [ -n "$found" ]; then
            printf '%s\n' "$found"
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 120 ] || fail "No aparecio '$title'."
        sleep 0.05
    done
}

wait_for_gone()
{
    window=$1
    attempt=0
    while DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$window" \
        >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 120 ] || fail "La ventana $window no desaparecio."
        sleep 0.05
    done
}

wait_for_state()
{
    window=$1
    atom=$2
    wanted=$3
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$window" \
            _NET_WM_STATE 2>/dev/null | grep -q "$atom"; then
            present=1
        else
            present=0
        fi
        [ "$present" = "$wanted" ] && return
        attempt=$((attempt + 1))
        sleep 0.05
    done
    fail "$window no alcanzo $atom=$wanted."
}

selection_owner()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-tray-client" \
        query 2>/dev/null || true
}

wait_for_owner()
{
    attempt=0
    while :; do
        owner=$(selection_owner)
        if [ -n "$owner" ]; then
            printf '%s\n' "$owner"
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No se adquirio _NET_SYSTEM_TRAY_S0."
        sleep 0.05
    done
}

wait_for_container()
{
    icon=$1
    panel=$2
    panel_hex=$(printf '0x%x' "$panel")
    attempt=0
    while [ "$attempt" -lt 160 ]; do
        container=$(parent_of "$icon" || true)
        if [ -n "$container" ] && [ "$container" != "$ROOT_WINDOW" ] &&
           [ "$(parent_of "$container" || true)" = "$panel_hex" ]; then
            printf '%s\n' "$container"
            return
        fi
        attempt=$((attempt + 1))
        sleep 0.05
    done
    fail "El icono $icon no fue alojado mediante XEmbed."
}

wait_for_map_state()
{
    window=$1
    wanted=$2
    attempt=0
    while [ "$(map_state "$window" || true)" != "$wanted" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "$window no alcanzo $wanted."
        sleep 0.05
    done
}

start_wm()
{
    ANTONIOWM_CONFIG="$TEST_TMP/config" \
    DISPLAY="$NESTED_CLIENT_DISPLAY" \
        "$PROJECT_DIR/antoniowm" >>"$TEST_TMP/wm.log" 2>&1 &
    WM_PID=$!
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root \
            _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; then
            break
        fi
        kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."
        attempt=$((attempt + 1))
        sleep 0.05
    done
    [ "$attempt" -lt 100 ] || fail "AntonioWM no publico EWMH."
    PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xdotool search --name '^AntonioWM Panel$' 2>/dev/null | tail -n 1 || true)
    [ -n "$PANEL_WINDOW" ] || fail "No se creo el panel inferior."
}

start_icon()
{
    id_file=$1
    log_file=$2
    name=$3
    size=$4

    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-tray-client" \
        icon "$id_file" "$log_file" "$name" "$size" &
    STARTED_ICON_PID=$!
    ICON_PIDS="$ICON_PIDS $STARTED_ICON_PID"
    wait_for_file "$id_file"
    STARTED_ICON=$(sed -n '1p' "$id_file")
}

trap cleanup EXIT INT TERM

for command in Xephyr xdpyinfo xdotool xprop xwininfo ps; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Falta el comando requerido: $command" >&2
        exit 1
    }
done
[ -n "${DISPLAY:-}" ] || {
    echo "Xephyr necesita un servidor X anfitrion." >&2
    exit 1
}

cat >"$TEST_TMP/config" <<'EOF'
taskbar.enabled=true
taskbar.height=30
taskbar.gap=3
taskbar.padding=3
taskbar.launcher_count=0
desktop.1=Uno
desktop.2=Dos
desktop.3=Tres
desktop.4=Cuatro
EOF

if [ "${ANTONIOWM_TEST_TCP_ONLY:-0}" = 1 ]; then
    Xephyr "$NESTED_SERVER_DISPLAY" -screen 1024x768 -ac -br -noreset \
        -listen tcp -nolisten local -nolisten unix \
        >"$TEST_TMP/xephyr.log" 2>&1 &
else
    Xephyr "$NESTED_SERVER_DISPLAY" -screen 1024x768 -ac -br -noreset \
        >"$TEST_TMP/xephyr.log" 2>&1 &
fi
XEPHYR_PID=$!
attempt=0
while ! DISPLAY="$NESTED_CLIENT_DISPLAY" xdpyinfo >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || fail "Xephyr no arranco."
    sleep 0.05
done
ROOT_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -root |
    sed -n 's/^xwininfo: Window id: \(0x[0-9a-fA-F]*\).*/\1/p')
[ -n "$ROOT_WINDOW" ] || fail "No se obtuvo la root window."

# Un applet iniciado antes del WM espera el anuncio MANAGER y se aloja despues.
start_icon "$TEST_TMP/before.id" "$TEST_TMP/before.log" "Tray Before WM" 180
ICON1=$STARTED_ICON
ICON1_PID=$STARTED_ICON_PID
[ "$(parent_of "$ICON1")" = "$ROOT_WINDOW" ] ||
    fail "El icono previo no partio de la root window."

start_wm
OWNER=$(wait_for_owner)
[ "$OWNER" != "$(printf '0x%x' "$PANEL_WINDOW")" ] ||
    fail "El owner de seleccion no debe ser el panel visible."
CONTAINER1=$(wait_for_container "$ICON1" "$PANEL_WINDOW")
grep -q '^embedded-container=' "$TEST_TMP/before.log" ||
    fail "No se recibio XEMBED_EMBEDDED_NOTIFY."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$OWNER" \
    _NET_SYSTEM_TRAY_ORIENTATION | grep -q '= 0' ||
    fail "La orientacion tray no es horizontal."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$OWNER" \
    _NET_SYSTEM_TRAY_VISUAL | grep -q '_NET_SYSTEM_TRAY_VISUAL' ||
    fail "No se publico _NET_SYSTEM_TRAY_VISUAL."
CONTAINER1_W=$(window_field "$CONTAINER1" WIDTH)
CONTAINER1_H=$(window_field "$CONTAINER1" HEIGHT)
CONTAINER1_Y=$(relative_y "$CONTAINER1")
[ "$CONTAINER1_W" = 22 ] &&
[ "$CONTAINER1_H" = 22 ] &&
[ "$CONTAINER1_Y" = 4 ] ||
    fail "El primer icono no quedo centrado y limitado a 22 px (xembed=${CONTAINER1_W}x${CONTAINER1_H}+y${CONTAINER1_Y})."

# Segundo icono, layout horizontal, redimension abusivo y exclusiones EWMH.
start_icon "$TEST_TMP/second.id" "$TEST_TMP/second.log" "Tray After WM" 8
ICON2=$STARTED_ICON
ICON2_PID=$STARTED_ICON_PID
CONTAINER2=$(wait_for_container "$ICON2" "$PANEL_WINDOW")
X1=$(relative_x "$CONTAINER1")
X2=$(relative_x "$CONTAINER2")
[ $((X2 - X1)) -eq 25 ] ||
    fail "El gap horizontal del tray no es 3 px (x1=$X1, x2=$X2)."
[ $((X2 + 22)) -eq 958 ] || fail "El tray invade el reloj o deja un hueco."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowsize "$ICON2" 300 200
sleep 0.15
[ "$(window_field "$ICON2" WIDTH)" = 22 ] &&
[ "$(window_field "$ICON2" HEIGHT)" = 22 ] ||
    fail "ConfigureRequest permitio un icono excesivo."
for window in "$ICON1" "$ICON2" "$CONTAINER1" "$CONTAINER2"; do
    window_hex=$(printf '0x%x' "$window")
    if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CLIENT_LIST 2>/dev/null |
        grep -qi "$window_hex"; then
        fail "La ventana tray $window entro en _NET_CLIENT_LIST."
    fi
done

# Clic, rueda y popup externo: solo el icono solicitado queda embebido.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove --window "$ICON1" 10 10 click 4
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove --window "$ICON1" 10 10 click 5
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove --window "$ICON1" 10 10 click 1
POPUP=$(wait_for_window "AWM Tray Test Popup")
[ "$(parent_of "$POPUP")" = "$ROOT_WINDOW" ] ||
    fail "El popup del applet fue reparentado dentro del panel."
POPUP_HEX=$(printf '0x%x' "$POPUP")
if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CLIENT_LIST 2>/dev/null |
    grep -qi "$POPUP_HEX"; then
    fail "El popup override-redirect entro en _NET_CLIENT_LIST."
fi
grep -q '^button=0x4$' "$TEST_TMP/before.log" &&
grep -q '^button=0x5$' "$TEST_TMP/before.log" &&
grep -q '^button=0x1$' "$TEST_TMP/before.log" ||
    fail "El applet no recibio clic/rueda."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove --window "$ICON1" 10 10 click 1
wait_for_gone "$POPUP"

# _XEMBED_INFO controla visibilidad y recupera el hueco sin olvidar el icono.
kill -HUP "$ICON1_PID"
wait_for_map_state "$CONTAINER1" IsUnMapped
sleep 0.1
HIDDEN_X2=$(relative_x "$CONTAINER2")
[ "$HIDDEN_X2" = "$X2" ] && [ $((HIDDEN_X2 + 22)) -eq 958 ] ||
    fail "Ocultar un icono altero el anclaje derecho (antes=$X2, despues=$HIDDEN_X2)."
kill -HUP "$ICON1_PID"
wait_for_map_state "$CONTAINER1" IsViewable
sleep 0.1
[ "$(relative_x "$CONTAINER1")" = "$X1" ] ||
    fail "Restaurar XEMBED_MAPPED no recompuso el layout."

# El tray permanece ajeno a escritorios/Alt+Tab y no altera workarea/estados.
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "Tray Normal Window" "360x220+120+90" none --min 1x1 --inc 1x1 \
    >"$TEST_TMP/normal.log" 2>&1 &
NORMAL_PID=$!
NORMAL=$(wait_for_window "Tray Normal Window")
FRAME=$(parent_of "$NORMAL")
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowactivate --sync "$NORMAL"
NORMAL_X=$(window_field "$FRAME" X)
NORMAL_Y=$(window_field "$FRAME" Y)
NORMAL_W=$(window_field "$FRAME" WIDTH)
NORMAL_H=$(window_field "$FRAME" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+Tab alt+shift+Tab
ACTIVE=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_ACTIVE_WINDOW)
NORMAL_HEX=$(printf '0x%x' "$NORMAL")
printf '%s\n' "$ACTIVE" | grep -qi "$NORMAL_HEX" ||
    fail "Alt+Tab selecciono algo distinto del unico cliente normal."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
wait_for_state "$NORMAL" _NET_WM_STATE_MAXIMIZED_VERT 1
[ "$(window_field "$FRAME" X)" = 0 ] &&
[ "$(window_field "$FRAME" Y)" = 0 ] &&
[ "$(window_field "$FRAME" WIDTH)" = 1024 ] &&
[ "$(window_field "$FRAME" HEIGHT)" = 738 ] ||
    fail "El tray altero la geometria maximizada."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
wait_for_state "$NORMAL" _NET_WM_STATE_FULLSCREEN 1
[ "$(window_field "$FRAME" WIDTH)" = 1024 ] &&
[ "$(window_field "$FRAME" HEIGHT)" = 768 ] ||
    fail "El tray impidio fullscreen sobre el panel."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
wait_for_state "$NORMAL" _NET_WM_STATE_FULLSCREEN 0
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
wait_for_state "$NORMAL" _NET_WM_STATE_MAXIMIZED_VERT 0
[ "$(window_field "$FRAME" X)" = "$NORMAL_X" ] &&
[ "$(window_field "$FRAME" Y)" = "$NORMAL_Y" ] &&
[ "$(window_field "$FRAME" WIDTH)" = "$NORMAL_W" ] &&
[ "$(window_field "$FRAME" HEIGHT)" = "$NORMAL_H" ] ||
    fail "Maximizar/fullscreen con tray produjo deriva al restaurar."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
sleep 0.15
wait_for_map_state "$CONTAINER1" IsViewable
wait_for_map_state "$CONTAINER2" IsViewable
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+1
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    close "$NORMAL"
wait_for_gone "$NORMAL"
wait "$NORMAL_PID" 2>/dev/null || true
NORMAL_PID=

# Cierre limpio, SIGKILL, reinicio del applet y altas/bajas repetidas.
kill "$ICON1_PID"
wait "$ICON1_PID" 2>/dev/null || true
wait_for_gone "$ICON1"
wait_for_gone "$CONTAINER1"
sleep 0.1
CLOSED_X2=$(relative_x "$CONTAINER2")
[ "$CLOSED_X2" = "$X2" ] && [ $((CLOSED_X2 + 22)) -eq 958 ] ||
    fail "Cerrar un icono altero el anclaje derecho (antes=$X2, despues=$CLOSED_X2)."

start_icon "$TEST_TMP/restarted.id" "$TEST_TMP/restarted.log" \
    "Tray Restarted" 64
ICON1=$STARTED_ICON
ICON1_PID=$STARTED_ICON_PID
CONTAINER1=$(wait_for_container "$ICON1" "$PANEL_WINDOW")

kill -KILL "$ICON2_PID"
wait "$ICON2_PID" 2>/dev/null || true
wait_for_gone "$ICON2"
wait_for_gone "$CONTAINER2"
kill -0 "$WM_PID" 2>/dev/null || fail "SIGKILL de un applet cerro el WM."

index=1
while [ "$index" -le 12 ]; do
    start_icon "$TEST_TMP/burst-$index.id" "$TEST_TMP/burst-$index.log" \
        "Tray Burst $index" 120
    BURST=$STARTED_ICON
    BURST_PID=$STARTED_ICON_PID
    BURST_CONTAINER=$(wait_for_container "$BURST" "$PANEL_WINDOW")
    if [ $((index % 2)) -eq 0 ]; then
        kill -KILL "$BURST_PID"
    else
        kill "$BURST_PID"
    fi
    wait "$BURST_PID" 2>/dev/null || true
    wait_for_gone "$BURST"
    wait_for_gone "$BURST_CONTAINER"
    kill -0 "$WM_PID" 2>/dev/null ||
        fail "El ciclo tray $index dejo inestable AntonioWM."
    index=$((index + 1))
done

# Tres reinicios del WM deben reanunciar MANAGER y realojar una sola copia.
restart=1
while [ "$restart" -le 3 ]; do
    starts=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
    attempt=0
    while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)" -le "$starts" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 160 ] || fail "El reinicio $restart no termino."
        sleep 0.05
    done
    PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xdotool search --name '^AntonioWM Panel$' 2>/dev/null | tail -n 1)
    OWNER=$(wait_for_owner)
    CONTAINER1=$(wait_for_container "$ICON1" "$PANEL_WINDOW")
    container_count=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xwininfo -id "$PANEL_WINDOW" -tree |
        grep -c 'AntonioWM Tray Container' || true)
    [ "$container_count" -eq 1 ] ||
        fail "El reinicio $restart produjo iconos duplicados."
    ICON_HEX=$(printf '0x%x' "$ICON1")
    if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CLIENT_LIST |
        grep -qi "$ICON_HEX"; then
        fail "El reinicio convirtio el icono tray en cliente normal."
    fi
    restart=$((restart + 1))
done

if ps -o stat= --ppid "$WM_PID" 2>/dev/null | grep -q '^[[:space:]]*Z'; then
    fail "AntonioWM dejo procesos hijo zombie."
fi
kill "$ICON1_PID"
wait "$ICON1_PID" 2>/dev/null || true
wait_for_gone "$ICON1"
kill "$WM_PID"
wait "$WM_PID" 2>/dev/null || true
WM_PID=

# Una seleccion preexistente no se roba y tampoco impide arrancar el WM.
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-tray-client" \
    manager "$TEST_TMP/mock.id" &
MOCK_PID=$!
wait_for_file "$TEST_TMP/mock.id"
MOCK_OWNER=$(sed -n '1p' "$TEST_TMP/mock.id")
[ "$(wait_for_owner)" = "$MOCK_OWNER" ] ||
    fail "El manager tray simulado no adquirio la seleccion."
start_wm
[ "$(wait_for_owner)" = "$MOCK_OWNER" ] ||
    fail "AntonioWM robo una seleccion tray existente."
grep -q 'ya pertenece.*bandeja del sistema queda desactivada' "$TEST_TMP/wm.log" ||
    fail "Falta el diagnostico de seleccion tray ocupada."
start_icon "$TEST_TMP/conflict.id" "$TEST_TMP/conflict.log" \
    "Tray Conflict" 32
CONFLICT=$STARTED_ICON
CONFLICT_PID=$STARTED_ICON_PID
sleep 0.3
[ "$(parent_of "$CONFLICT")" = "$ROOT_WINDOW" ] ||
    fail "AntonioWM alojo un icono sin poseer la seleccion tray."
kill -0 "$WM_PID" 2>/dev/null ||
    fail "La seleccion tray ocupada impidio funcionar al WM."
kill "$CONFLICT_PID"
wait "$CONFLICT_PID" 2>/dev/null || true
kill "$MOCK_PID"
wait "$MOCK_PID" 2>/dev/null || true
MOCK_PID=

if grep -q 'error X11:' "$TEST_TMP/wm.log"; then
    fail "La prueba tray produjo un error X11 no controlado."
fi

echo "Prueba systray superada: seleccion/MANAGER, dock XEmbed, layout, resize, clic/rueda, popup externo, EWMH, SIGKILL, ciclos, reinicios y conflicto de owner."
