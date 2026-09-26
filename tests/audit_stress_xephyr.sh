#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_AUDIT_TEST_DISPLAY:-:96}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_AUDIT_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
CLIENT_PIDS=

cleanup()
{
    set +e
    for process in $CLIENT_PIDS "$WM_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && kill "$process" 2>/dev/null
    done
    for process in $CLIENT_PIDS "$WM_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && wait "$process" 2>/dev/null
    done
    rm -rf "$TEST_TMP"
}

fail()
{
    echo "$1" >&2
    if [ -s "$TEST_TMP/wm.log" ]; then
        echo "Registro de AntonioWM:" >&2
        sed -n '1,320p' "$TEST_TMP/wm.log" >&2
    fi
    exit 1
}

client_ids()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CLIENT_LIST 2>/dev/null |
        sed -n 's/.*# //p' | tr ',' ' '
}

client_count()
{
    set -- $(client_ids)
    printf '%s\n' "$#"
}

wait_for_count()
{
    wanted=$1
    attempt=0
    while [ "$(client_count)" -ne "$wanted" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 160 ] ||
            fail "_NET_CLIENT_LIST no alcanzo $wanted clientes."
        sleep 0.05
    done
}

wait_for_gone()
{
    gone_window=$1
    attempt=0
    while DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$gone_window" \
          >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 120 ] || fail "La ventana $gone_window no desaparecio."
        sleep 0.05
    done
}

frame_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
}

frame_field()
{
    frame=$(frame_of "$1")
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$frame" |
        sed -n "s/^$2=//p"
}

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
}

state_has()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$1" _NET_WM_STATE 2>/dev/null |
        grep -q "$2"
}

wait_for_state()
{
    state_window=$1
    atom=$2
    wanted=$3
    attempt=0
    while :; do
        if state_has "$state_window" "$atom"; then
            present=1
        else
            present=0
        fi
        [ "$present" -eq "$wanted" ] && return
        attempt=$((attempt + 1))
        [ "$attempt" -le 120 ] || fail "$atom no alcanzo el estado $wanted."
        sleep 0.05
    done
}

ewmh_request()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" "$@"
}

trap cleanup EXIT INT TERM

for command in Xephyr xdpyinfo xdotool xprop xwininfo; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Falta el comando requerido: $command" >&2
        exit 1
    }
done
[ -n "${DISPLAY:-}" ] || {
    echo "Xephyr necesita un servidor X anfitrion." >&2
    exit 1
}

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

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/antoniowm" \
    >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.3
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."

# Crea muchas ventanas en paralelo para forzar MapRequest, reparenting,
# actualizaciones de lista/stacking y destrucciones muy proximas entre si.
index=1
while [ "$index" -le 24 ]; do
    title=$(printf 'Audit Burst %02d' "$index")
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
        "$title" "180x100" none --min 1x1 --inc 1x1 \
        >"$TEST_TMP/client-$index.log" 2>&1 &
    CLIENT_PIDS="$CLIENT_PIDS $!"
    index=$((index + 1))
done
wait_for_count 24

ids=$(client_ids)
index=0
remaining=
for window in $ids; do
    if [ "$index" -lt 10 ]; then
        ewmh_request close "$window"
    else
        remaining="$remaining $window"
    fi
    index=$((index + 1))
done
wait_for_count 14

set -- $remaining
MOVE_CLOSE=$1
shift
RESIZE_CLOSE=$1
shift
ewmh_request interactive "$MOVE_CLOSE" 220 180 8
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove 300 240
ewmh_request close "$MOVE_CLOSE"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mouseup 1
wait_for_gone "$MOVE_CLOSE"

ewmh_request interactive "$RESIZE_CLOSE" 300 240 4
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove 390 310
ewmh_request close "$RESIZE_CLOSE"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mouseup 3
wait_for_gone "$RESIZE_CLOSE"
wait_for_count 12
kill -0 "$WM_PID" 2>/dev/null ||
    fail "Cerrar clientes durante drag dejo inestable el WM."

set -- $(client_ids)
ACTIVE=$1
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowactivate --sync "$ACTIVE"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+Tab alt+shift+Tab
ewmh_request moveresize "$ACTIVE" 2147483647 -2147483648 300 200
sleep 0.1
[ "$(frame_field "$ACTIVE" X)" = 32767 ] &&
[ "$(frame_field "$ACTIVE" Y)" = -32768 ] ||
    fail "_NET_MOVERESIZE_WINDOW no limito coordenadas al protocolo X11."
ewmh_request moveresize "$ACTIVE" 120 100 300 200
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowminimize "$ACTIVE"
attempt=0
while [ "$(map_state "$ACTIVE")" != IsUnMapped ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "El cliente no quedo realmente desmapeado."
    sleep 0.05
done
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowmap "$ACTIVE"
attempt=0
while [ "$(map_state "$(frame_of "$ACTIVE")")" != IsViewable ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "MapRequest no restauro el cliente."
    sleep 0.05
done

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
wait_for_state "$ACTIVE" _NET_WM_STATE_MAXIMIZED_VERT 1
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
wait_for_state "$ACTIVE" _NET_WM_STATE_FULLSCREEN 1
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
wait_for_state "$ACTIVE" _NET_WM_STATE_FULLSCREEN 0
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
wait_for_state "$ACTIVE" _NET_WM_STATE_MAXIMIZED_VERT 0

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+2
sleep 0.15
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$ACTIVE" _NET_WM_DESKTOP |
    grep -q '= 1' || fail "El envio de escritorio fallo bajo carga."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
sleep 0.15

start_count=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
attempt=0
while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)" -le "$start_count" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 160 ] || fail "El reinicio bajo carga no termino."
    sleep 0.05
done
wait_for_count 12

# Deja un cliente vivo y termina el WM con SIGTERM. Debe ejecutar su limpieza:
# reparentar al root, retirar propiedades propias y no cerrar la aplicacion.
set -- $(client_ids)
SURVIVOR=$1
shift
for window in "$@"; do
    ewmh_request close "$window"
done
wait_for_count 1
ROOT_HEX=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -root 2>/dev/null |
    sed -n 's/^xwininfo: Window id: \(0x[0-9a-fA-F]*\).*/\1/p')
kill -TERM "$WM_PID"
wait "$WM_PID" || fail "AntonioWM no termino limpiamente tras SIGTERM."
WM_PID=

[ "$(frame_of "$SURVIVOR")" = "$ROOT_HEX" ] ||
    fail "SIGTERM no devolvio el cliente al root."
[ "$(map_state "$SURVIVOR")" = IsViewable ] ||
    fail "SIGTERM no dejo visible el cliente superviviente."
if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_SUPPORTING_WM_CHECK \
   2>/dev/null | grep -q 'window id'; then
    fail "SIGTERM dejo _NET_SUPPORTING_WM_CHECK obsoleto."
fi
if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$SURVIVOR" \
   _NET_FRAME_EXTENTS 2>/dev/null | grep -q '= '; then
    fail "SIGTERM dejo extents de un frame ya destruido."
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowclose "$SURVIVOR" || true
echo "Prueba de auditoria superada: 24 clientes concurrentes, cierre durante move/resize, estados, escritorios, reinicio y SIGTERM limpio."
