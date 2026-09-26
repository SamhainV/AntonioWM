#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_SCREENSHOT_TEST_DISPLAY:-:92}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_SCREENSHOT_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=

cleanup()
{
    set +e
    if [ -f "$TEST_TMP/xclip-pids" ]; then
        while IFS= read -r process; do
            [ -n "$process" ] && kill "$process" 2>/dev/null
        done <"$TEST_TMP/xclip-pids"
    fi
    for process in "$WM_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && kill "$process" 2>/dev/null
    done
    for process in "$WM_PID" "$XEPHYR_PID"; do
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
    if [ -s "$TEST_TMP/xephyr.log" ]; then
        echo "Ultimas lineas de Xephyr:" >&2
        tail -n 50 "$TEST_TMP/xephyr.log" >&2
    fi
    exit 1
}

line_count()
{
    if [ -f "$1" ]; then
        wc -l <"$1" | tr -d '[:space:]'
    else
        printf '0\n'
    fi
}

wait_for_count()
{
    count_file=$1
    wanted=$2
    description=$3
    attempt=0

    while [ "$(line_count "$count_file")" -lt "$wanted" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No se completo $description."
        sleep 0.05
    done
}

wait_for_clipboard()
{
    attempt=0
    while ! DISPLAY="$NESTED_CLIENT_DISPLAY" \
            "$PROJECT_DIR/tests/awm-clipboard-helper" \
            probe "$TEST_TMP/capture.png" >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] ||
            fail "CLIPBOARD no ofrece la captura como image/png."
        sleep 0.05
    done
}

wait_for_desktop()
{
    wanted=$1
    attempt=0

    while :; do
        current=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xprop -root _NET_CURRENT_DESKTOP 2>/dev/null |
            sed -n 's/.* = //p')
        [ "$current" = "$wanted" ] && return
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "No se cambio al escritorio $wanted."
        sleep 0.05
    done
}

wait_for_restart()
{
    previous=$1
    attempt=0

    while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" 2>/dev/null || true)" \
            -le "$previous" ]; do
        kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al reiniciar."
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "AntonioWM no completo el reinicio."
        sleep 0.05
    done
}

restart_wm()
{
    starts=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" 2>/dev/null || true)
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
    wait_for_restart "$starts"
}

press_screenshot()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key ctrl+shift+Print
}

assert_no_wm_zombie()
{
    if ps -o stat= --ppid "$WM_PID" 2>/dev/null |
       grep -Eq '^[[:space:]]*Z'; then
        fail "Un proceso hijo directo de AntonioWM quedo zombie."
    fi
}

trap cleanup EXIT INT TERM

for command in Xephyr base64 xdpyinfo xdotool xprop ps; do
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
printf '%s' \
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFgAI/ScL9WQAAAABJRU5ErkJggg==' |
    base64 -d >"$TEST_TMP/capture.png"

{
    printf '%s\n' '#!/bin/sh' 'set -eu'
    printf '%s\n' 'printf "capture\\n" >>"${AWM_TEST_CAPTURE_LOG:?}"'
    printf '%s\n' 'exec /bin/cat "${AWM_TEST_PNG:?}"'
} >"$TEST_TMP/bin/maim"
{
    printf '%s\n' '#!/bin/sh' 'set -eu'
    printf '%s\n' 'printf "override\\n" >>"${AWM_TEST_OVERRIDE_LOG:?}"'
    printf '%s\n' 'maim | xclip -selection clipboard -t image/png -i'
} >"$TEST_TMP/bin/awm-test-screenshot"
chmod +x "$TEST_TMP/bin/maim" "$TEST_TMP/bin/awm-test-screenshot"
ln -s "$PROJECT_DIR/tests/awm-clipboard-helper" "$TEST_TMP/bin/xclip"

{
    printf '%s\n' \
        '# screenshot.command se omite a proposito para probar el valor predeterminado' \
        'taskbar.enabled=false'
} >"$TEST_TMP/config"

PATH="$TEST_TMP/bin:$PATH" \
AWM_TEST_PNG="$TEST_TMP/capture.png" \
AWM_TEST_CAPTURE_LOG="$TEST_TMP/captures" \
AWM_TEST_OVERRIDE_LOG="$TEST_TMP/overrides" \
AWM_TEST_XCLIP_PID_LOG="$TEST_TMP/xclip-pids" \
ANTONIOWM_CONFIG="$TEST_TMP/config" \
DISPLAY="$NESTED_CLIENT_DISPLAY" \
    "$PROJECT_DIR/antoniowm" >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.4
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino durante el arranque."

CLIENT_LIST_BEFORE=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xprop -root _NET_CLIENT_LIST 2>/dev/null)

# Valor predeterminado y transferencia PNG real mediante la seleccion X11.
press_screenshot
wait_for_count "$TEST_TMP/captures" 1 "la primera captura"
wait_for_count "$TEST_TMP/xclip-pids" 1 "el primer propietario de CLIPBOARD"
wait_for_clipboard

# CapsLock y NumLock no deben alterar el grab global.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Caps_Lock
press_screenshot
wait_for_count "$TEST_TMP/captures" 2 "la captura con CapsLock"
wait_for_count "$TEST_TMP/xclip-pids" 2 "el propietario con CapsLock"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Caps_Lock

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Num_Lock
press_screenshot
wait_for_count "$TEST_TMP/captures" 3 "la captura con NumLock"
wait_for_count "$TEST_TMP/xclip-pids" 3 "el propietario con NumLock"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Num_Lock

# Varias capturas consecutivas sustituyen al propietario anterior sin bloquear.
for expected in 4 5 6 7; do
    press_screenshot
    wait_for_count "$TEST_TMP/captures" "$expected" "la captura consecutiva $expected"
    wait_for_count "$TEST_TMP/xclip-pids" "$expected" \
        "el propietario consecutivo $expected"
done
wait_for_clipboard
sleep 0.3
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM no responde tras varias capturas."
assert_no_wm_zombie

CURRENT_OWNER=$(tail -n 1 "$TEST_TMP/xclip-pids")
kill -0 "$CURRENT_OWNER" 2>/dev/null ||
    fail "El propietario actual de CLIPBOARD termino prematuramente."
for old_owner in $(sed '$d' "$TEST_TMP/xclip-pids"); do
    attempt=0
    while kill -0 "$old_owner" 2>/dev/null; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 60 ] ||
            fail "El propietario anterior $old_owner no termino al perder CLIPBOARD."
        sleep 0.05
    done
done

CLIENT_LIST_AFTER=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xprop -root _NET_CLIENT_LIST 2>/dev/null)
[ "$CLIENT_LIST_AFTER" = "$CLIENT_LIST_BEFORE" ] ||
    fail "La captura creo una ventana gestionada o una interfaz visible."

# La opcion runtime sustituye el comando sin recompilar.
{
    printf '%s\n' \
        'screenshot.command=awm-test-screenshot' \
        'taskbar.enabled=false'
} >"$TEST_TMP/config"
restart_wm
press_screenshot
wait_for_count "$TEST_TMP/captures" 8 "la captura configurada externamente"
wait_for_count "$TEST_TMP/overrides" 1 "el comando externo de captura"
wait_for_count "$TEST_TMP/xclip-pids" 8 "el propietario configurado externamente"
wait_for_clipboard

# Un atajo anterior sigue funcionando después de añadir el nuevo grab.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
wait_for_desktop 1
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+1
wait_for_desktop 0

# Sin maim ni xclip el shell falla, pero el WM continúa y recoge al hijo.
rm -f "$TEST_TMP/bin/maim" "$TEST_TMP/bin/xclip"
{
    printf '%s\n' \
        '# se vuelve al comando predeterminado, ahora sin herramientas instaladas' \
        'taskbar.enabled=false'
} >"$TEST_TMP/config"
restart_wm
press_screenshot
sleep 0.5
[ "$(line_count "$TEST_TMP/captures")" = 8 ] ||
    fail "La prueba sin herramientas ejecuto una captura inesperada."
kill -0 "$WM_PID" 2>/dev/null ||
    fail "La ausencia de maim/xclip termino AntonioWM."
assert_no_wm_zombie
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
wait_for_desktop 1

echo "OK: captura PNG silenciosa, locks, configuracion runtime y procesos hijos"
