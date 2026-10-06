#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_AUTOSTART_TEST_DISPLAY:-:94}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_AUTOSTART_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
WM_LOG=
PERSISTENT_PID=
TRAY_PID=
PANEL_WINDOW=
ROOT_WINDOW=

cleanup()
{
    set +e
    for process in "$WM_PID" "$PERSISTENT_PID" "$TRAY_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && kill "$process" 2>/dev/null
    done
    for process in "$WM_PID" "$PERSISTENT_PID" "$TRAY_PID" "$XEPHYR_PID"; do
        [ -n "$process" ] && wait "$process" 2>/dev/null
    done
    rm -rf "$TEST_TMP"
}

fail()
{
    echo "$1" >&2
    if [ -n "$WM_LOG" ] && [ -s "$WM_LOG" ]; then
        echo "Registro de AntonioWM:" >&2
        sed -n '1,300p' "$WM_LOG" >&2
    fi
    exit 1
}

wait_for_file()
{
    path=$1
    attempt=0
    while [ ! -s "$path" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 120 ] || fail "No aparecio $path."
        sleep 0.05
    done
}

wait_for_count()
{
    path=$1
    pattern=$2
    wanted=$3
    description=$4
    attempt=0
    while :; do
        current=$(grep -c "^$pattern$" "$path" 2>/dev/null || true)
        [ "$current" -ge "$wanted" ] && return
        attempt=$((attempt + 1))
        [ "$attempt" -le 120 ] || fail "No se observo $description."
        sleep 0.05
    done
}

parent_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
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
    fail "El icono autostart $icon no se alojo en el System Tray."
}

start_wm()
{
    config=$1
    marker=$2
    WM_LOG=$3
    env _ANTONIOWM_RESTART_PID="$marker" \
        ANTONIOWM_CONFIG="$config" \
        AWM_AUTOSTART_LOG="$TEST_TMP/autostart.log" \
        AWM_PERSISTENT_PID="$TEST_TMP/persistent.pid" \
        AWM_TRAY_PID="$TEST_TMP/tray.pid" \
        AWM_TRAY_ID="$TEST_TMP/tray.id" \
        AWM_TRAY_LOG="$TEST_TMP/tray.log" \
        AWM_TRAY_CLIENT="$PROJECT_DIR/tests/awm-tray-client" \
        DISPLAY="$NESTED_CLIENT_DISPLAY" \
        "$PROJECT_DIR/antoniowm" >>"$WM_LOG" 2>&1 &
    WM_PID=$!

    attempt=0
    while [ "$attempt" -lt 120 ]; do
        PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xdotool search --name '^AntonioWM Panel$' 2>/dev/null |
            tail -n 1 || true)
        if [ -n "$PANEL_WINDOW" ]; then
            return
        fi
        kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."
        attempt=$((attempt + 1))
        sleep 0.05
    done
    fail "AntonioWM no creo el panel."
}

stop_wm()
{
    [ -n "$WM_PID" ] || return
    kill "$WM_PID"
    wait "$WM_PID" 2>/dev/null || true
    WM_PID=
    attempt=0
    while DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool search \
        --name '^AntonioWM Panel$' >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "El panel no desaparecio al cerrar el WM."
        sleep 0.05
    done
}

assert_no_wm_zombie()
{
    if ps -o stat= --ppid "$WM_PID" 2>/dev/null |
       grep -Eq '^[[:space:]]*Z'; then
        fail "Un comando de autostart dejo un hijo zombie."
    fi
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

mkdir -p "$TEST_TMP/bin"
cat >"$TEST_TMP/bin/record" <<'EOF'
#!/bin/sh
printf '%s\n' "$1" >>"$AWM_AUTOSTART_LOG"
EOF
cat >"$TEST_TMP/bin/persistent" <<'EOF'
#!/bin/sh
printf '%s\n' "$$" >"$AWM_PERSISTENT_PID"
printf '%s\n' persistent >>"$AWM_AUTOSTART_LOG"
exec sleep 3600
EOF
cat >"$TEST_TMP/bin/tray-applet" <<'EOF'
#!/bin/sh
printf '%s\n' "$$" >"$AWM_TRAY_PID"
printf '%s\n' tray >>"$AWM_AUTOSTART_LOG"
exec "$AWM_TRAY_CLIENT" icon "$AWM_TRAY_ID" "$AWM_TRAY_LOG" \
    "Autostart Tray" 22
EOF
chmod +x "$TEST_TMP/bin/record" "$TEST_TMP/bin/persistent" \
    "$TEST_TMP/bin/tray-applet"

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

# Un archivo inexistente y una configuracion sin claves autostart son validos.
start_wm "$TEST_TMP/no-existe" 999999 "$TEST_TMP/missing.log"
kill -0 "$WM_PID" 2>/dev/null || fail "Fallo con configuracion inexistente."
stop_wm
cat >"$TEST_TMP/no-autostart.conf" <<'EOF'
taskbar.enabled=true
desktop.1=Sin autostart
EOF
start_wm "$TEST_TMP/no-autostart.conf" 999999 "$TEST_TMP/no-autostart.log"
sleep 0.15
[ ! -e "$TEST_TMP/autostart.log" ] ||
    fail "Se ejecuto autostart sin claves configuradas."
stop_wm

# count=0 prevalece aunque haya comandos, y una cuenta mal formada no arranca nada.
cat >"$TEST_TMP/zero.conf" <<EOF
autostart.count=0
autostart.1.command=$TEST_TMP/bin/record count-zero
EOF
start_wm "$TEST_TMP/zero.conf" 999999 "$TEST_TMP/zero.log"
sleep 0.15
[ ! -e "$TEST_TMP/autostart.log" ] || fail "autostart.count=0 ejecuto comandos."
stop_wm

cat >"$TEST_TMP/single.conf" <<EOF
autostart.count=1
autostart.1.command=$TEST_TMP/bin/record single
EOF
start_wm "$TEST_TMP/single.conf" 999999 "$TEST_TMP/single.log"
wait_for_count "$TEST_TMP/autostart.log" single 1 "el unico comando"
[ "$(grep -c '^single$' "$TEST_TMP/autostart.log")" -eq 1 ] ||
    fail "autostart.count=1 no ejecuto exactamente un comando."
stop_wm
rm -f "$TEST_TMP/autostart.log"

cat >"$TEST_TMP/malformed.conf" <<EOF
autostart.count=no-es-numero
autostart.1.command=$TEST_TMP/bin/record malformed
autostart.17.command=$TEST_TMP/bin/record out-of-range
autostart.2.otro=valor
linea-sin-igual
EOF
start_wm "$TEST_TMP/malformed.conf" 999999 "$TEST_TMP/malformed.log"
sleep 0.15
[ ! -e "$TEST_TMP/autostart.log" ] ||
    fail "Una configuracion autostart mal formada ejecuto comandos."
grep -q "valor no valido para 'autostart.count'" "$TEST_TMP/malformed.log" ||
    fail "Falta el diagnostico para autostart.count mal formado."
kill -0 "$WM_PID" 2>/dev/null || fail "La configuracion mal formada cerro el WM."
stop_wm

# Comandos independientes: dos cortos, uno persistente, un applet tray, uno
# inexistente, un indice ausente y un comando vacio.
: >"$TEST_TMP/autostart.log"
cat >"$TEST_TMP/main.conf" <<EOF
taskbar.enabled=true
taskbar.height=31
taskbar.launcher_count=0
desktop.1=Auto Uno
desktop.2=Auto Dos
autostart.count=7
autostart.1.command=$TEST_TMP/bin/record one
autostart.2.command=$TEST_TMP/bin/record two
autostart.3.command=$TEST_TMP/bin/persistent
autostart.4.command=$TEST_TMP/bin/tray-applet
autostart.5.command=antoniowm-command-that-does-not-exist
autostart.7.command=
EOF
start_wm "$TEST_TMP/main.conf" 999999 "$TEST_TMP/main.log"
wait_for_count "$TEST_TMP/autostart.log" one 1 "el primer comando"
wait_for_count "$TEST_TMP/autostart.log" two 1 "el segundo comando"
wait_for_count "$TEST_TMP/autostart.log" persistent 1 "el comando persistente"
wait_for_count "$TEST_TMP/autostart.log" tray 1 "el applet tray"
wait_for_file "$TEST_TMP/persistent.pid"
wait_for_file "$TEST_TMP/tray.pid"
wait_for_file "$TEST_TMP/tray.id"
PERSISTENT_PID=$(sed -n '1p' "$TEST_TMP/persistent.pid")
TRAY_PID=$(sed -n '1p' "$TEST_TMP/tray.pid")
TRAY_WINDOW=$(sed -n '1p' "$TEST_TMP/tray.id")
kill -0 "$PERSISTENT_PID" 2>/dev/null || fail "El comando persistente termino."
kill -0 "$TRAY_PID" 2>/dev/null || fail "El applet tray termino."
TRAY_CONTAINER=$(wait_for_container "$TRAY_WINDOW" "$PANEL_WINDOW")
[ -n "$TRAY_CONTAINER" ] || fail "No se creo el contenedor XEmbed."
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell \
    "$PANEL_WINDOW" | sed -n 's/^HEIGHT=//p')" = 31 ] ||
    fail "La configuracion restante no conservo taskbar.height=31."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_DESKTOP_NAMES |
    grep -q 'Auto Uno' || fail "La configuracion restante no publico escritorios."
grep -q 'autostart.6.command esta vacio o ausente' "$TEST_TMP/main.log" ||
    fail "Falta el diagnostico del indice autostart ausente."
grep -q 'autostart.7.command esta vacio o ausente' "$TEST_TMP/main.log" ||
    fail "Falta el diagnostico del comando autostart vacio."
kill -0 "$WM_PID" 2>/dev/null || fail "El comando inexistente cerro AntonioWM."
grep -q 'antoniowm-command-that-does-not-exist' "$TEST_TMP/main.log" ||
    fail "Falta el diagnostico del comando autostart inexistente."
assert_no_wm_zombie

# El exec del reinicio conserva el PID del WM, consume su marcador privado y
# no vuelve a lanzar ningun comando. El applet existente debe redockearse.
restart=1
while [ "$restart" -le 3 ]; do
    starts=$(grep -c 'iniciado en' "$TEST_TMP/main.log" || true)
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
    attempt=0
    while [ "$(grep -c 'iniciado en' "$TEST_TMP/main.log" || true)" -le "$starts" ]; do
        kill -0 "$WM_PID" 2>/dev/null || fail "El reinicio $restart termino el WM."
        attempt=$((attempt + 1))
        [ "$attempt" -le 160 ] || fail "El reinicio $restart no termino."
        sleep 0.05
    done
    PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xdotool search --name '^AntonioWM Panel$' 2>/dev/null | tail -n 1)
    TRAY_CONTAINER=$(wait_for_container "$TRAY_WINDOW" "$PANEL_WINDOW")
    container_count=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xwininfo -id "$PANEL_WINDOW" -tree |
        grep -c 'AntonioWM Tray Container' || true)
    [ "$container_count" -eq 1 ] ||
        fail "El reinicio $restart duplico el icono tray."
    for item in one two persistent tray; do
        [ "$(grep -c "^$item$" "$TEST_TMP/autostart.log")" -eq 1 ] ||
            fail "El reinicio $restart repitio el comando '$item'."
    done
    kill -0 "$PERSISTENT_PID" 2>/dev/null ||
        fail "El reinicio $restart sustituyo el proceso persistente."
    kill -0 "$TRAY_PID" 2>/dev/null ||
        fail "El reinicio $restart sustituyo el applet tray."
    assert_no_wm_zombie
    restart=$((restart + 1))
done
[ "$(grep -c 'reinicio interno detectado; se omite el autostart' \
    "$TEST_TMP/main.log")" -eq 3 ] ||
    fail "No se consumio exactamente una vez el marcador de cada reinicio."

# Cerrar AntonioWM no debe bloquear ni terminar programas auxiliares.
stop_wm
kill -0 "$PERSISTENT_PID" 2>/dev/null ||
    fail "El cierre normal del WM termino el comando persistente."
kill -0 "$TRAY_PID" 2>/dev/null ||
    fail "El cierre normal del WM termino el applet tray."

echo "Prueba autostart superada: configuracion ausente/cero/malformada, comandos independientes, fallo tolerado, XEmbed, cero duplicados en tres reinicios, sin zombies y cierre limpio."
