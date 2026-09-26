#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_PANEL_MENU_TEST_DISPLAY:-:91}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_PANEL_MENU_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
CLIENT_PIDS=
PANEL_WINDOW=
MENU_WINDOW=

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
        sed -n '1,260p' "$TEST_TMP/wm.log" >&2
    fi
    exit 1
}

window_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$1" |
        sed -n "s/^$2=//p"
}

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
}

frame_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
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
        sleep 0.1
    done
}

wait_for_managed()
{
    window=$1
    attempt=0
    while :; do
        frame=$(frame_of "$window" || true)
        state=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xprop -id "$window" WM_STATE 2>/dev/null || true)
        if [ -n "$frame" ] && printf '%s\n' "$state" | grep -q '^WM_STATE'; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No se gestiono $window."
        sleep 0.1
    done
}

wait_for_menu_state()
{
    wanted=$1
    attempt=0
    while [ "$(map_state "$MENU_WINDOW" || true)" != "$wanted" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "El menu no alcanzo $wanted."
        sleep 0.05
    done
}

wait_for_action()
{
    action=$1
    attempt=0
    while ! grep -qx "$action" "$TEST_TMP/actions.log" 2>/dev/null; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "No se ejecuto la accion '$action'."
        sleep 0.05
    done
}

close_client()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" \
        close "$1"
    sleep 0.15
}

click_panel()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --window "$PANEL_WINDOW" "$1" 15 click 1
}

open_menu()
{
    click_panel 48
    wait_for_menu_state IsViewable
}

click_menu()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --window "$MENU_WINDOW" "$1" "$2" click 1
}

start_wm()
{
    PATH="$TEST_TMP/bin:$PATH" \
    ANTONIOWM_CONFIG="$TEST_TMP/config" \
    ANTONIOWM_SYSTEM_APPLICATIONS_DIR="$TEST_TMP/applications-system" \
    ANTONIOWM_USER_APPLICATIONS_DIR="$TEST_TMP/applications-user" \
    AWM_CLIENT="$PROJECT_DIR/tests/awm-test-client" \
    AWM_ACTION_LOG="$TEST_TMP/actions.log" \
    AWM_APP_LOG="$TEST_TMP/apps.log" \
    AWM_TERMINAL_LOG="$TEST_TMP/terminal.log" \
    DISPLAY="$NESTED_CLIENT_DISPLAY" \
        "$PROJECT_DIR/antoniowm" >>"$TEST_TMP/wm.log" 2>&1 &
    WM_PID=$!
    sleep 0.4
    kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."
    PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xdotool search --name '^AntonioWM Panel$' 2>/dev/null | head -n 1 || true)
    MENU_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xdotool search --name '^AntonioWM Menu$' 2>/dev/null | head -n 1 || true)
    [ -n "$PANEL_WINDOW" ] || fail "No se creo el panel inferior."
    [ -n "$MENU_WINDOW" ] || fail "No se creo la ventana del menu."
}

trap cleanup EXIT INT TERM

for command in Xephyr xdpyinfo xdotool xprop xwininfo ps base64; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Falta el comando requerido: $command" >&2
        exit 1
    }
done
[ -n "${DISPLAY:-}" ] || {
    echo "Xephyr necesita un servidor X anfitrion." >&2
    exit 1
}

mkdir -p "$TEST_TMP/bin" "$TEST_TMP/applications-system" \
         "$TEST_TMP/applications-user"
cat >"$TEST_TMP/bin/menu-app" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$AWM_APP_LOG"
exec "$AWM_CLIENT" "Menu Normal App" "260x150+120+100" none --min 1x1 --inc 1x1
EOF
cat >"$TEST_TMP/bin/terminal-app" <<'EOF'
#!/bin/sh
exec "$AWM_CLIENT" "Menu Terminal App" "260x150+150+120" none --min 1x1 --inc 1x1
EOF
cat >"$TEST_TMP/bin/added-app" <<'EOF'
#!/bin/sh
exec "$AWM_CLIENT" "Menu Added App" "260x150+180+140" none --min 1x1 --inc 1x1
EOF
cat >"$TEST_TMP/bin/test-files" <<'EOF'
#!/bin/sh
exec "$AWM_CLIENT" "Menu Files" "260x150+210+160" none --min 1x1 --inc 1x1
EOF
cat >"$TEST_TMP/bin/test-terminal" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$AWM_TERMINAL_LOG"
if [ "$#" -eq 0 ]; then
    exec "$AWM_CLIENT" "Menu Terminal" "260x150+240+180" none --min 1x1 --inc 1x1
fi
if [ "$1" = "-e" ] || [ "$1" = "--" ]; then
    shift
    exec "$@"
fi
exit 2
EOF
cat >"$TEST_TMP/bin/test-action" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$AWM_ACTION_LOG"
EOF
chmod +x "$TEST_TMP/bin/menu-app" "$TEST_TMP/bin/terminal-app" \
    "$TEST_TMP/bin/added-app" "$TEST_TMP/bin/test-files" \
    "$TEST_TMP/bin/test-terminal" "$TEST_TMP/bin/test-action"

cat >"$TEST_TMP/applications-system/accessory.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Aplicacion Accesorio
Exec=menu-app %f %F %u %U %i %c %k
Categories=Utility;
TryExec=menu-app
EOF
cat >"$TEST_TMP/applications-system/graphics.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Aplicacion Grafica
Exec=terminal-app %U
Icon=icono-inexistente
Categories=Graphics;
Terminal=true
TryExec=terminal-app
EOF
cat >"$TEST_TMP/applications-system/hidden.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=No debe aparecer
Exec=menu-app
Hidden=true
EOF
cat >"$TEST_TMP/applications-system/nodisplay.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Tampoco debe aparecer
Exec=menu-app
NoDisplay=true
EOF
cat >"$TEST_TMP/applications-system/tryexec.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=TryExec ausente
Exec=menu-app
TryExec=no-existe-antonio-wm-test
EOF

cat >"$TEST_TMP/config" <<'EOF'
terminal=test-terminal
taskbar.enabled=true
taskbar.height=30
taskbar.gap=3
taskbar.padding=3
taskbar.launcher_count=1
launcher.1.label=Archivos
launcher.1.command=test-files
system.logout.command=test-action logout
system.switch_user.command=test-action switch
system.reboot.command=test-action reboot
system.poweroff.command=test-action poweroff
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
    sleep 0.1
done

start_wm
[ "$(window_field "$PANEL_WINDOW" X)" = 0 ] &&
[ "$(window_field "$PANEL_WINDOW" Y)" = 738 ] &&
[ "$(window_field "$PANEL_WINDOW" WIDTH)" = 1024 ] &&
[ "$(window_field "$PANEL_WINDOW" HEIGHT)" = 30 ] ||
    fail "El panel unificado tiene geometria incorrecta."
[ -z "$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Bar$' 2>/dev/null | head -n 1 || true)" ] ||
    fail "La barra superior antigua sigue existiendo."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_WORKAREA |
    grep -q '= 0, 0, 1024, 738' ||
    fail "_NET_WORKAREA no comienza en y=0 o no reserva el panel."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_DESKTOP_NAMES |
    grep -q 'Uno.*Dos.*Tres.*Cuatro' ||
    fail "El panel perdio los nombres de escritorios configurados."

# Escape y clic fuera cierran el menu sin activar el destino inferior.
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Escape
wait_for_menu_state IsUnMapped
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove 800 300 click 1
wait_for_menu_state IsUnMapped

# Navegacion completa con raton: Aplicaciones > Accesorios > aplicacion.
open_menu
click_menu 110 13
click_menu 110 37
click_menu 140 37
NORMAL_WINDOW=$(wait_for_window "Menu Normal App")
wait_for_managed "$NORMAL_WINDOW"
CLIENT_PIDS="$CLIENT_PIDS $(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool getwindowpid "$NORMAL_WINDOW" 2>/dev/null || true)"
wait_for_menu_state IsUnMapped
if grep -Eq '%[fFuUick]' "$TEST_TMP/apps.log"; then
    fail "El menu ejecuto codigos de campo .desktop sin limpiar."
fi

# El boton de tarea activo minimiza y el mismo boton restaura/enfoca.
click_panel 395
sleep 0.15
[ "$(map_state "$(frame_of "$NORMAL_WINDOW")")" = IsUnMapped ] ||
    fail "La tarea activa no se minimizo desde el panel."
click_panel 395
sleep 0.15
[ "$(map_state "$(frame_of "$NORMAL_WINDOW")")" = IsViewable ] ||
    fail "La tarea minimizada no se restauro desde el panel."

# El panel cambia de escritorio inmediatamente y conserva la lista por escritorio.
click_panel 140
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CURRENT_DESKTOP |
    grep -q '= 1' || fail "El boton del escritorio 2 no cambio de escritorio."
click_panel 111
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CURRENT_DESKTOP |
    grep -q '= 0' || fail "El boton del escritorio 1 no cambio de escritorio."

# Maximizado desde y=0 hasta el panel; fullscreen cubre toda la pantalla.
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowactivate --sync "$NORMAL_WINDOW"
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
sleep 0.15
FRAME=$(frame_of "$NORMAL_WINDOW")
[ "$(window_field "$FRAME" X)" = 0 ] &&
[ "$(window_field "$FRAME" Y)" = 0 ] &&
[ "$(window_field "$FRAME" WIDTH)" = 1024 ] &&
[ "$(window_field "$FRAME" HEIGHT)" = 738 ] ||
    fail "Maximizar no uso 1024x738+0+0."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
sleep 0.15
[ "$(window_field "$FRAME" X)" = 0 ] &&
[ "$(window_field "$FRAME" Y)" = 0 ] &&
[ "$(window_field "$FRAME" WIDTH)" = 1024 ] &&
[ "$(window_field "$FRAME" HEIGHT)" = 768 ] ||
    fail "Fullscreen no cubrio el panel inferior."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
sleep 0.15

# Accesos directos Terminal/Archivos y Terminal=true desde .desktop.
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Return
TERMINAL_WINDOW=$(wait_for_window "Menu Terminal")
wait_for_managed "$TERMINAL_WINDOW"
close_client "$TERMINAL_WINDOW"
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Return
FILES_WINDOW=$(wait_for_window "Menu Files")
wait_for_managed "$FILES_WINDOW"
close_client "$FILES_WINDOW"
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Return
TERMINAL_APP_WINDOW=$(wait_for_window "Menu Terminal App")
wait_for_managed "$TERMINAL_APP_WINDOW"
grep -q -- '-e /bin/sh -c' "$TEST_TMP/terminal.log" ||
    fail "Terminal=true no paso por el terminal configurado."
close_client "$TERMINAL_APP_WINDOW"

# Acciones configuradas y confirmaciones internas para reinicio/apagado.
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Down Return
wait_for_action switch
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Down Down Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Return
wait_for_action reboot
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Down Down Down Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Return Escape
if grep -qx poweroff "$TEST_TMP/actions.log" 2>/dev/null; then
    fail "Cancelar ejecuto la orden de apagado."
fi
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Down Down Down Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Return
wait_for_action poweroff
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Return
wait_for_action logout

if ps -o stat= --ppid "$WM_PID" 2>/dev/null | grep -q '^[[:space:]]*Z'; then
    fail "El menu dejo procesos hijo zombie."
fi

# El catalogo se vuelve a leer durante el reinicio sin perder clientes.
cat >"$TEST_TMP/applications-system/internet.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Aplicacion Nueva
Exec=added-app
Categories=Network;
TryExec=added-app
EOF
STARTS=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
attempt=0
while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)" -le "$STARTS" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "AntonioWM no completo el reinicio."
    sleep 0.1
done
PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Panel$' 2>/dev/null | head -n 1)
MENU_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Menu$' 2>/dev/null | head -n 1)
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Return
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Return
ADDED_WINDOW=$(wait_for_window "Menu Added App")
wait_for_managed "$ADDED_WINDOW"
close_client "$ADDED_WINDOW"
[ -n "$(frame_of "$NORMAL_WINDOW" || true)" ] ||
    fail "El reinicio perdio el cliente existente."

# Con logout vacio, Cerrar sesion termina limpiamente el WM y deja el cliente vivo.
printf '%s\n' 'system.logout.command=' >>"$TEST_TMP/config"
STARTS=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
attempt=0
while [ "$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)" -le "$STARTS" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "No se recargo logout vacio."
    sleep 0.1
done
PANEL_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Panel$' 2>/dev/null | head -n 1)
MENU_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Menu$' 2>/dev/null | head -n 1)
open_menu
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key Down Down Down Return
attempt=0
while kill -0 "$WM_PID" 2>/dev/null; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || fail "Cerrar sesion no termino AntonioWM."
    sleep 0.05
done
wait "$WM_PID"
WM_PID=
DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$NORMAL_WINDOW" >/dev/null 2>&1 ||
    fail "Cerrar sesion destruyo una aplicacion cliente."

echo "Prueba panel/menu superada: panel unico, y=0, reloj/layout, escritorios, tareas, menu raton/teclado, Escape, clic exterior, .desktop, Terminal=true, acciones, confirmaciones y reinicio."
