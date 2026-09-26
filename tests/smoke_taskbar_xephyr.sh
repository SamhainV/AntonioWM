#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_TASKBAR_TEST_DISPLAY:-:95}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_TASKBAR_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
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
        sed -n '1,240p' "$TEST_TMP/wm.log" >&2
    fi
    exit 1
}

window_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$1" |
        sed -n "s/^$2=//p"
}

frame_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
}

frame_field()
{
    managed_frame=$(frame_of "$1")
    window_field "$managed_frame" "$2"
}

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
}

active_window()
{
    active_hex=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xprop -root _NET_ACTIVE_WINDOW 2>/dev/null |
        sed -n 's/.*# \(0x[0-9a-fA-F]*\).*/\1/p')
    if [ -z "$active_hex" ] || [ "$active_hex" = 0x0 ]; then
        printf '0\n'
    else
        printf '%d\n' "$active_hex"
    fi
}

wait_for_window()
{
    wanted_title=$1
    attempt=0
    while :; do
        found_window=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xdotool search --name "^$wanted_title$" 2>/dev/null | head -n 1 || true)
        if [ -n "$found_window" ]; then
            printf '%s\n' "$found_window"
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No aparecio '$wanted_title'."
        sleep 0.1
    done
}

wait_for_managed()
{
    managed_window=$1
    attempt=0
    while :; do
        managed_frame=$(frame_of "$managed_window" || true)
        managed_state=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xprop -id "$managed_window" WM_STATE 2>/dev/null || true)
        if [ -n "$managed_frame" ] &&
           printf '%s\n' "$managed_state" | grep -q '^WM_STATE'; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No se gestiono $managed_window."
        sleep 0.1
    done
}

wait_for_gone()
{
    gone_window=$1
    attempt=0
    while DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$gone_window" \
          >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "La ventana $gone_window no se cerro."
        sleep 0.1
    done
}

wait_for_focus()
{
    wanted_window=$1
    attempt=0
    while [ "$(active_window)" != "$wanted_window" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No se enfoco $wanted_window."
        sleep 0.1
    done
}

wait_for_frame_state()
{
    state_window=$1
    wanted_state=$2
    attempt=0
    while :; do
        state_frame=$(frame_of "$state_window" || true)
        if [ -n "$state_frame" ] &&
           [ "$(map_state "$state_frame")" = "$wanted_state" ]; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] ||
            fail "El frame de $state_window no alcanzo $wanted_state."
        sleep 0.1
    done
}

click_taskbar()
{
    click_x=$1
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --window "$TASKBAR_WINDOW" "$click_x" 14 click 1
}

start_client()
{
    title=$1
    shift
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
        "$title" "$@" >"$TEST_TMP/client-$title.log" 2>&1 &
    LAST_CLIENT_PID=$!
    CLIENT_PIDS="$CLIENT_PIDS $LAST_CLIENT_PID"
}

trap cleanup EXIT INT TERM

for command in Xephyr base64 xdpyinfo xdotool xprop xwininfo ps; do
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
    sleep 0.1
done

mkdir -p "$TEST_TMP/bin"
for launcher in test-chrome test-terminal test-files test-gimp test-hotkey; do
    ln -s "$PROJECT_DIR/tests/awm-test-client" "$TEST_TMP/bin/$launcher"
done

printf '%s' \
    'iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAEklEQVR4nGO4o6HxHx9mGBkKAIMRisGi/0sOAAAAAElFTkSuQmCC' |
    base64 -d >"$TEST_TMP/red.png"
{
    printf '%s\n' \
        'terminal=test-hotkey' \
        'taskbar.enabled=true' \
        'taskbar.height=30' \
        'taskbar.gap=3' \
        'taskbar.padding=3' \
        'taskbar.launcher_count=4' \
        'launcher.1.label=Icono' \
        'launcher.1.command=test-chrome' \
        'launcher.1.icon=red.png' \
        'launcher.2.label=Fallback' \
        'launcher.2.command=test-terminal' \
        'launcher.2.icon=missing.png' \
        'launcher.3.label=Archivos' \
        'launcher.3.command=test-files' \
        'launcher.4.label=GIMP' \
        'launcher.4.command=test-gimp' \
        'focus_follows_mouse=true' \
        'desktop.1=Uno' \
        'desktop.2=Dos' \
        'desktop.3=Tres' \
        'desktop.4=Cuatro'
} >"$TEST_TMP/config"

PATH="$TEST_TMP/bin:$PATH" ANTONIOWM_CONFIG="$TEST_TMP/config" \
    DISPLAY="$NESTED_CLIENT_DISPLAY" \
    "$PROJECT_DIR/antoniowm" >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.4
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."

TASKBAR_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Taskbar$' 2>/dev/null | head -n 1 || true)
[ -n "$TASKBAR_WINDOW" ] || fail "No se creo AntonioWM Taskbar."
[ "$(window_field "$TASKBAR_WINDOW" X)" = 0 ] &&
[ "$(window_field "$TASKBAR_WINDOW" Y)" = 738 ] &&
[ "$(window_field "$TASKBAR_WINDOW" WIDTH)" = 1024 ] &&
[ "$(window_field "$TASKBAR_WINDOW" HEIGHT)" = 30 ] ||
    fail "La barra inferior tiene una geometria incorrecta."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_WORKAREA |
    grep -q '= 0, 26, 1024, 712' ||
    fail "_NET_WORKAREA no reserva ambas barras."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_DESKTOP_NAMES |
    grep -q 'Uno.*Dos.*Tres.*Cuatro' ||
    fail "No se publicaron los nombres externos de escritorios."
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-pixel-probe" \
    "$TASKBAR_WINDOW" 18 15 || fail "No se dibujo el icono PNG configurado."
grep -q "missing.png.*se usara la etiqueta 'Fallback'" "$TEST_TMP/wm.log" ||
    fail "El icono inexistente no activo el fallback de texto."

# El primer boton es un icono de 30 px; los otros usan etiquetas de 78 px.
# Cada clic demuestra que se ejecuta exactamente el comando externo configurado.
for launcher_x in 18 75 156 237; do
    click_taskbar "$launcher_x"
    LAUNCHED=$(wait_for_window "AntonioWM test client")
    wait_for_managed "$LAUNCHED"
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
        close "$LAUNCHED"
    wait_for_gone "$LAUNCHED"
done
# El atajo de terminal tambien usa la configuracion externa.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+Return
LAUNCHED=$(wait_for_window "AntonioWM test client")
wait_for_managed "$LAUNCHED"
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
    close "$LAUNCHED"
wait_for_gone "$LAUNCHED"
sleep 0.2
if ps -o stat= --ppid "$WM_PID" 2>/dev/null | grep -q '^[[:space:]]*Z'; then
    fail "Los lanzadores dejaron un proceso zombie."
fi

start_client "Taskbar One" "300x180+80+100" none --min 1x1 --inc 1x1
WINDOW_ONE=$(wait_for_window "Taskbar One")
wait_for_managed "$WINDOW_ONE"
start_client "Taskbar Two" "300x180+430+140" none --min 1x1 --inc 1x1
WINDOW_TWO=$(wait_for_window "Taskbar Two")
wait_for_managed "$WINDOW_TWO"
wait_for_focus "$WINDOW_TWO"

# focus_follows_mouse=true debe reemplazar el valor interno por defecto.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove --window "$WINDOW_ONE" 20 20
wait_for_focus "$WINDOW_ONE"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove --window "$WINDOW_TWO" 20 20
wait_for_focus "$WINDOW_TWO"

# Los cuatro launchers consumen 276 px y los botones empiezan en x=279.
click_taskbar 379
wait_for_focus "$WINDOW_ONE"
click_taskbar 379
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$WINDOW_ONE" WM_STATE |
    grep -q Iconic || fail "Clic sobre la tarea activa no la minimizo."
click_taskbar 379
wait_for_frame_state "$WINDOW_ONE" IsViewable
wait_for_focus "$WINDOW_ONE"
click_taskbar 582
wait_for_focus "$WINDOW_TWO"

# SKIP_TASKBAR no debe crear un tercer boton en x=740..940.
start_client "Taskbar Skipped" "220x120+700+260" none \
    --initial-state SKIP_TASKBAR --min 1x1 --inc 1x1
WINDOW_SKIPPED=$(wait_for_window "Taskbar Skipped")
wait_for_managed "$WINDOW_SKIPPED"
click_taskbar 582
wait_for_focus "$WINDOW_TWO"
click_taskbar 784
[ "$(active_window)" != "$WINDOW_TWO" ] &&
    fail "SKIP_TASKBAR aparecio en la barra inferior."

# Maximizado usa el espacio entre ambas barras; fullscreen usa toda la pantalla.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
sleep 0.2
[ "$(frame_field "$WINDOW_TWO" X)" = 0 ] &&
[ "$(frame_field "$WINDOW_TWO" Y)" = 26 ] &&
[ "$(frame_field "$WINDOW_TWO" WIDTH)" = 1024 ] &&
[ "$(frame_field "$WINDOW_TWO" HEIGHT)" = 712 ] ||
    fail "El maximizado no respeto la barra inferior."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
sleep 0.2
[ "$(frame_field "$WINDOW_TWO" X)" = 0 ] &&
[ "$(frame_field "$WINDOW_TWO" Y)" = 0 ] &&
[ "$(frame_field "$WINDOW_TWO" WIDTH)" = 1024 ] &&
[ "$(frame_field "$WINDOW_TWO" HEIGHT)" = 768 ] ||
    fail "Fullscreen no cubrio ambas barras."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
sleep 0.2

# En otro escritorio no deben quedar tareas del primero.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
sleep 0.2
click_taskbar 379
[ "$(active_window)" = "$WINDOW_ONE" ] &&
    fail "La barra inferior mostro una ventana de otro escritorio."
start_client "Taskbar Desktop Two" "280x160+200+180" none --min 1x1 --inc 1x1
WINDOW_THREE=$(wait_for_window "Taskbar Desktop Two")
wait_for_managed "$WINDOW_THREE"
wait_for_focus "$WINDOW_THREE"
click_taskbar 379
wait_for_frame_state "$WINDOW_THREE" IsUnMapped
click_taskbar 379
wait_for_frame_state "$WINDOW_THREE" IsViewable
wait_for_focus "$WINDOW_THREE"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+1
sleep 0.2
click_taskbar 379
wait_for_focus "$WINDOW_ONE"

# El reinicio recrea la barra sin cerrar clientes ni perder sus acciones.
start_count=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
attempt=0
while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)" -le "$start_count" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "AntonioWM no termino de reiniciarse."
    sleep 0.1
done
TASKBAR_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Taskbar$' 2>/dev/null | head -n 1 || true)
[ -n "$TASKBAR_WINDOW" ] || fail "El reinicio no recreo la barra inferior."
click_taskbar 582
wait_for_focus "$WINDOW_TWO"

# La misma configuracion puede desactivar por completo la barra y su reserva.
kill "$WM_PID"
wait "$WM_PID"
WM_PID=
printf '%s\n' 'taskbar.enabled=false' >"$TEST_TMP/disabled-config"
PATH="$TEST_TMP/bin:$PATH" ANTONIOWM_CONFIG="$TEST_TMP/disabled-config" \
    DISPLAY="$NESTED_CLIENT_DISPLAY" \
    "$PROJECT_DIR/antoniowm" >>"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.5
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM no arranco sin taskbar."
DISABLED_TASKBAR=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Taskbar$' 2>/dev/null | head -n 1 || true)
[ -z "$DISABLED_TASKBAR" ] || fail "taskbar.enabled=false no oculto la barra."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_WORKAREA |
    grep -q '= 0, 26, 1024, 742' ||
    fail "La taskbar desactivada siguio reservando workarea."

echo "Prueba de configuracion/taskbar superada: PNG, fallback, comandos, foco, escritorios, geometria, minimizar/restaurar, fullscreen, reinicio y desactivacion."
