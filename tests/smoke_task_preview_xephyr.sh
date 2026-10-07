#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_PREVIEW_TEST_DISPLAY:-:93}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_PREVIEW_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
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
    if [ "${ANTONIOWM_KEEP_TEST_LOGS:-0}" = 1 ]; then
        printf 'Registros de la prueba: %s\n' "$TEST_TMP" >&2
    else
        rm -rf "$TEST_TMP"
    fi
}

fail()
{
    echo "$1" >&2
    if [ -s "$TEST_TMP/wm.log" ]; then
        echo "Registro de AntonioWM:" >&2
        sed -n '1,240p' "$TEST_TMP/wm.log" >&2
    fi
    if [ -s "$TEST_TMP/xephyr.log" ]; then
        echo "Ultimas lineas de Xephyr:" >&2
        tail -n 80 "$TEST_TMP/xephyr.log" >&2
    fi
    exit 1
}

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
}

window_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$1" |
        sed -n "s/^$2=//p"
}

find_preview()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xdotool search --name '^AntonioWM Task Preview' 2>/dev/null |
        tail -n 1 || true
}

wait_for_preview_state()
{
    wanted=$1
    attempt=0
    while :; do
        PREVIEW_WINDOW=$(find_preview)
        if [ -n "$PREVIEW_WINDOW" ] &&
           [ "$(map_state "$PREVIEW_WINDOW" || true)" = "$wanted" ]; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "La preview no alcanzo $wanted."
        sleep 0.05
    done
}

wait_for_preview_hidden()
{
    attempt=0
    while :; do
        preview=$(find_preview)
        if [ -z "$preview" ] ||
           [ "$(map_state "$preview" || true)" != IsViewable ]; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "La preview continuo visible."
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
        [ "$attempt" -le 100 ] || fail "No aparecio '$title'."
        sleep 0.05
    done
}

wait_for_wm_state()
{
    window=$1
    wanted=$2
    attempt=0
    while :; do
        state=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xprop -id "$window" WM_STATE 2>/dev/null || true)
        printf '%s\n' "$state" | grep -q "window state: $wanted" && return
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "$window no alcanzo WM_STATE $wanted."
        sleep 0.05
    done
}

wait_for_wm()
{
    attempt=0
    while :; do
        if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root \
            _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'; then
            return
        fi
        kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "AntonioWM no publico EWMH."
        sleep 0.05
    done
}

start_client()
{
    title=$1
    geometry=$2
    shift 2
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
        "$title" "$geometry" none "$@" \
        >"$TEST_TMP/client-$title.log" 2>&1 &
    LAST_CLIENT_PID=$!
    CLIENT_PIDS="$CLIENT_PIDS $LAST_CLIENT_PID"
}

hover_task()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --sync --window "$PANEL_WINDOW" 300 15
}

move_off_panel()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove 500 250
}

trap cleanup EXIT INT TERM

for command in Xephyr xdpyinfo xdotool xprop xwininfo xrandr grep; do
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
taskbar.gap=4
taskbar.padding=4
taskbar.launcher_count=0
taskbar.preview=true
taskbar.preview_delay=350
taskbar.preview_width=320
taskbar.preview_height=200
autostart.count=0
EOF

cat >"$TEST_TMP/disabled-config" <<'EOF'
taskbar.enabled=true
taskbar.height=30
taskbar.gap=4
taskbar.padding=4
taskbar.launcher_count=0
taskbar.preview=false
autostart.count=0
EOF

if [ "${ANTONIOWM_TEST_TCP_ONLY:-0}" = 1 ]; then
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
    [ "$attempt" -le 100 ] || fail "Xephyr no arranco."
    sleep 0.05
done

DISPLAY="$NESTED_CLIENT_DISPLAY" xdpyinfo -queryExtensions |
    grep -q 'Composite' || fail "Xephyr no ofrece Composite para la prueba."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdpyinfo -queryExtensions |
    grep -q 'RENDER' || fail "Xephyr no ofrece Render para la prueba."

# Dos monitores lógicos permiten verificar que el popup nunca cruza el borde
# del monitor que contiene el panel primario.
OUTPUT=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr --query |
    sed -n 's/^\([^ ][^ ]*\) connected.*/\1/p' | head -n 1)
[ -n "$OUTPUT" ] || fail "Xephyr no publico una salida RandR."
DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr \
    --setmonitor AWM-PREVIEW-LEFT 600/160x700/186+0+0 "$OUTPUT" \
    >"$TEST_TMP/xrandr.log" 2>&1
DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr \
    --setmonitor AWM-PREVIEW-RIGHT 600/160x700/186+600+0 none \
    >>"$TEST_TMP/xrandr.log" 2>&1
DISPLAY="$NESTED_CLIENT_DISPLAY" xrandr --listmonitors |
    grep -q '^Monitors: 2$' || fail "No se crearon dos monitores logicos."

ANTONIOWM_CONFIG="$TEST_TMP/config" DISPLAY="$NESTED_CLIENT_DISPLAY" \
    "$PROJECT_DIR/antoniowm" >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
wait_for_wm
PANEL_WINDOW=$(wait_for_window "AntonioWM Panel")
[ "$(window_field "$PANEL_WINDOW" X)" = 0 ] &&
[ "$(window_field "$PANEL_WINDOW" Y)" = 670 ] &&
[ "$(window_field "$PANEL_WINDOW" WIDTH)" = 600 ] ||
    fail "El panel no quedo en el monitor primario de prueba."

start_client "Preview Red Client" "320x200+60+60" --red
RED_PID=$LAST_CLIENT_PID
RED_WINDOW=$(wait_for_window "Preview Red Client")
sleep 0.25
ACTIVE_BEFORE=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xprop -root _NET_ACTIVE_WINDOW)

# El popup no aparece antes del retardo configurado.
hover_task
sleep 0.12
[ -z "$(find_preview)" ] || fail "La preview ignoro el retardo de 350 ms."
sleep 0.32
wait_for_preview_state IsViewable

PREVIEW_WIDTH=$(window_field "$PREVIEW_WINDOW" WIDTH)
PREVIEW_HEIGHT=$(window_field "$PREVIEW_WINDOW" HEIGHT)
PREVIEW_X=$(window_field "$PREVIEW_WINDOW" X)
PREVIEW_Y=$(window_field "$PREVIEW_WINDOW" Y)
[ "$PREVIEW_WIDTH" -le 334 ] && [ "$PREVIEW_HEIGHT" -le 236 ] ||
    fail "La preview supera los limites configurados."
[ "$PREVIEW_X" -ge 0 ] &&
[ $((PREVIEW_X + PREVIEW_WIDTH)) -le 600 ] &&
[ "$PREVIEW_Y" -ge 0 ] &&
[ $((PREVIEW_Y + PREVIEW_HEIGHT)) -le 670 ] ||
    fail "La preview salio del monitor o solapo el panel."
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_ACTIVE_WINDOW)" = \
   "$ACTIVE_BEFORE" ] || fail "La preview robo el foco."
PREVIEW_HEX=$(printf '0x%x' "$PREVIEW_WINDOW")
if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CLIENT_LIST |
   grep -qi "$PREVIEW_HEX"; then
    fail "La preview aparecio en _NET_CLIENT_LIST."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-pixel-probe" \
    "$PREVIEW_WINDOW" $((PREVIEW_WIDTH / 2)) $((PREVIEW_HEIGHT / 2)) ||
    fail "La preview no contiene la imagen real del cliente."
if [ -n "${ANTONIOWM_VISUAL_CAPTURE:-}" ]; then
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$ANTONIOWM_VISUAL_CAPTURE" \
        "$ANTONIOWM_VISUAL_DIR/normal.ppm"
fi

# Los cambios de título se reflejan sin recrear ni enfocar el popup.
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$RED_WINDOW" \
    -f _NET_WM_NAME 8u -set _NET_WM_NAME \
    'Titulo dinamico extremadamente largo para probar elipsis'
attempt=0
while ! DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$PREVIEW_WINDOW" WM_NAME |
          grep -q 'Titulo dinamico'; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || fail "La preview no actualizo el titulo."
    sleep 0.05
done

# LeaveNotify y hover sobre controles que no son tareas ocultan/no crean popup.
move_off_panel
wait_for_preview_hidden
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --sync --window "$PANEL_WINDOW" 35 15
sleep 0.45
wait_for_preview_hidden

# Al minimizar se conserva el último backing pixmap Composite. El press oculta
# inmediatamente la preview y el release mantiene el clic normal de taskbar.
hover_task
wait_for_preview_state IsViewable
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousedown 1
wait_for_preview_hidden
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mouseup 1
wait_for_wm_state "$RED_WINDOW" Iconic
move_off_panel
hover_task
wait_for_preview_state IsViewable
PREVIEW_WIDTH=$(window_field "$PREVIEW_WINDOW" WIDTH)
PREVIEW_HEIGHT=$(window_field "$PREVIEW_WINDOW" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-pixel-probe" \
    "$PREVIEW_WINDOW" $((PREVIEW_WIDTH / 2)) $((PREVIEW_HEIGHT / 2)) ||
    fail "La preview minimizada no conservo el ultimo contenido real."
if [ -n "${ANTONIOWM_VISUAL_CAPTURE:-}" ]; then
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$ANTONIOWM_VISUAL_CAPTURE" \
        "$ANTONIOWM_VISUAL_DIR/iconic.ppm"
fi

# Un resize solicitado durante Iconic no puede descartar el último pixmap.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowsize "$RED_WINDOW" 340 210
sleep 0.15
move_off_panel
hover_task
wait_for_preview_state IsViewable
PREVIEW_WIDTH=$(window_field "$PREVIEW_WINDOW" WIDTH)
PREVIEW_HEIGHT=$(window_field "$PREVIEW_WINDOW" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-pixel-probe" \
    "$PREVIEW_WINDOW" $((PREVIEW_WIDTH / 2)) $((PREVIEW_HEIGHT / 2)) ||
    fail "Un cambio de geometria Iconic descarto la ultima imagen."

# El mismo botón restaura y conserva exactamente su semántica previa.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool click 1
wait_for_wm_state "$RED_WINDOW" Normal
move_off_panel

# Dos tareas cambian la miniatura y la identidad sin crear un segundo popup.
start_client "Preview Second Client" "290x180+120+90" --red
SECOND_PID=$LAST_CLIENT_PID
SECOND_WINDOW=$(wait_for_window "Preview Second Client")
sleep 0.15
hover_task
wait_for_preview_state IsViewable
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --sync --window "$PANEL_WINDOW" 420 15
wait_for_preview_state IsViewable
attempt=0
while ! DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$PREVIEW_WINDOW" WM_NAME |
        grep -q 'Preview Second Client'; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || fail "La preview no siguio el cambio de tarea."
    sleep 0.05
done
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Task Preview' | wc -l)" -eq 1 ] ||
    fail "Se crearon varias ventanas de preview."

# Si el cliente se destruye durante el delay, el XID pendiente se invalida.
move_off_panel
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --sync --window "$PANEL_WINDOW" 420 15
sleep 0.07
kill -KILL "$SECOND_PID"
wait "$SECOND_PID" 2>/dev/null || true
CLIENT_PIDS=$(printf '%s\n' "$CLIENT_PIDS" | sed "s/ $SECOND_PID//")
sleep 0.45
wait_for_preview_hidden
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM murio durante el delay."

for cycle in 1 2 3 4 5 6 7 8 9 10; do
    move_off_panel
    hover_task
    wait_for_preview_state IsViewable
    move_off_panel
    wait_for_preview_hidden
done

# La salida antes de que expire el delay tampoco crea un popup tardío.
hover_task
sleep 0.08
move_off_panel
sleep 0.4
wait_for_preview_hidden

# Cambio de escritorio y destrucción del cliente invalidan el popup.
hover_task
wait_for_preview_state IsViewable
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
wait_for_preview_hidden
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+1
sleep 0.15
hover_task
wait_for_preview_state IsViewable
kill -KILL "$RED_PID"
wait "$RED_PID" 2>/dev/null || true
CLIENT_PIDS=$(printf '%s\n' "$CLIENT_PIDS" | sed "s/ $RED_PID//")
wait_for_preview_hidden
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM murio al destruir el cliente."

# Reiniciar el WM destruye el popup interno y conserva el cliente gestionado.
start_client "Preview Restart Client" "300x180+90+90" --red
RESTART_WINDOW=$(wait_for_window "Preview Restart Client")
sleep 0.2
hover_task
wait_for_preview_state IsViewable
STARTS=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
attempt=0
while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)" -le "$STARTS" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "AntonioWM no completo el reinicio."
    sleep 0.05
done
PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Panel$' 2>/dev/null | tail -n 1 || true)
[ -n "$PANEL_WINDOW" ] || PANEL_WINDOW=$(wait_for_window "AntonioWM Panel")
[ -n "$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^Preview Restart Client$' 2>/dev/null | head -n 1)" ] ||
    fail "El reinicio perdio el cliente."
wait_for_preview_hidden
hover_task
wait_for_preview_state IsViewable
move_off_panel
wait_for_preview_hidden

# La configuración false no crea infraestructura de preview y deja el panel
# y la gestión normal disponibles.
kill "$WM_PID"
wait "$WM_PID"
WM_PID=
ANTONIOWM_CONFIG="$TEST_TMP/disabled-config" DISPLAY="$NESTED_CLIENT_DISPLAY" \
    "$PROJECT_DIR/antoniowm" >>"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
wait_for_wm
PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Panel$' 2>/dev/null | tail -n 1)
hover_task
sleep 0.5
[ -z "$(find_preview)" ] || fail "taskbar.preview=false creo una ventana interna."
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM fallo con previews desactivadas."

# Sin la extensión Composite el WM continúa arrancando y desactiva únicamente
# esta mejora visual.
kill "$WM_PID"
wait "$WM_PID"
WM_PID=
for process in $CLIENT_PIDS; do
    kill "$process" 2>/dev/null || true
done
for process in $CLIENT_PIDS; do
    wait "$process" 2>/dev/null || true
done
CLIENT_PIDS=
kill "$XEPHYR_PID"
wait "$XEPHYR_PID" 2>/dev/null || true
XEPHYR_PID=
if [ "${ANTONIOWM_TEST_TCP_ONLY:-0}" = 1 ]; then
    Xephyr "$NESTED_SERVER_DISPLAY" -screen 1200x700 -ac -br -noreset \
        -extension Composite -listen tcp -nolisten local -nolisten unix \
        >"$TEST_TMP/xephyr-no-composite.log" 2>&1 &
else
    Xephyr "$NESTED_SERVER_DISPLAY" -screen 1200x700 -ac -br -noreset \
        -extension Composite >"$TEST_TMP/xephyr-no-composite.log" 2>&1 &
fi
XEPHYR_PID=$!
attempt=0
while ! DISPLAY="$NESTED_CLIENT_DISPLAY" xdpyinfo >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || fail "Xephyr sin Composite no arranco."
    sleep 0.05
done
if DISPLAY="$NESTED_CLIENT_DISPLAY" xdpyinfo -queryExtensions |
   grep -q 'Composite'; then
    fail "Xephyr no desactivo Composite para probar el fallback."
fi
ANTONIOWM_CONFIG="$TEST_TMP/config" DISPLAY="$NESTED_CLIENT_DISPLAY" \
    "$PROJECT_DIR/antoniowm" >>"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
wait_for_wm
grep -q 'no ofrece Composite/Render; previews de tareas desactivadas' \
    "$TEST_TMP/wm.log" || fail "No se diagnostico el fallback sin Composite."
kill -0 "$WM_PID" 2>/dev/null || fail "El WM no sobrevivio sin Composite."

echo "Prueba task-preview superada: retardo, imagen real visible/Iconic, titulo, foco, exclusiones, invalidacion, multimonitor, reinicio, configuracion false y fallback sin Composite."
