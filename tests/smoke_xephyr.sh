#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_TEST_DISPLAY:-:98}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
CLIENT_ONE_PID=
CLIENT_TWO_PID=
CLIENT_AUTO_ONE_PID=
CLIENT_AUTO_TWO_PID=
CLIENT_FULLSCREEN_PID=
WINDOW_XTERM=
XTERM_TESTED=0

cleanup()
{
    if [ -n "$WINDOW_XTERM" ]; then
        DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowclose "$WINDOW_XTERM" \
            >/dev/null 2>&1 || true
    fi
    for process in "$CLIENT_ONE_PID" "$CLIENT_TWO_PID" \
                   "$CLIENT_AUTO_ONE_PID" "$CLIENT_AUTO_TWO_PID" \
                   "$CLIENT_FULLSCREEN_PID" "$WM_PID" "$XEPHYR_PID"; do
        if [ -n "$process" ]; then
            kill "$process" 2>/dev/null || true
        fi
    done
    for process in "$CLIENT_ONE_PID" "$CLIENT_TWO_PID" \
                   "$CLIENT_AUTO_ONE_PID" "$CLIENT_AUTO_TWO_PID" \
                   "$CLIENT_FULLSCREEN_PID" "$WM_PID" "$XEPHYR_PID"; do
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
        sed -n '1,260p' "$TEST_TMP/wm.log" >&2
    fi
    if [ -s "$TEST_TMP/xephyr.log" ]; then
        echo "Ultimas lineas de Xephyr:" >&2
        tail -n 40 "$TEST_TMP/xephyr.log" >&2
    fi
    exit 1
}

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
}

frame_of()
{
    parent=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1)
    if [ -n "$parent" ]; then
        printf '%d\n' "$parent"
    fi
}

child_named()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n "s/^[[:space:]]*\\(0x[0-9a-fA-F]*\\) \"$2\".*/\\1/p" |
        head -n 1
}

window_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowgeometry --shell "$1" |
        sed -n "s/^$2=//p"
}

frame_field()
{
    managed_frame=$(frame_of "$1")
    [ -n "$managed_frame" ] || return 1
    window_field "$managed_frame" "$2"
}

xwin_field()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n "s/^  $2: //p"
}

current_desktop()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CURRENT_DESKTOP 2>/dev/null |
        sed -n 's/.* = //p'
}

window_desktop()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$1" _NET_WM_DESKTOP 2>/dev/null |
        sed -n 's/.* = //p'
}

state_has()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$1" _NET_WM_STATE 2>/dev/null |
        grep -q "$2"
}

wm_state_has()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$1" WM_STATE 2>/dev/null |
        grep -q "window state: $2"
}

wait_for_management()
{
    managed_window=$1
    attempt=0
    while :; do
        managed_state=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xprop -id "$managed_window" WM_STATE 2>/dev/null || true)
        managed_frame=$(frame_of "$managed_window" || true)
        managed_title=
        if [ -n "$managed_frame" ]; then
            managed_title=$(child_named "$managed_frame" "AntonioWM Titlebar" || true)
        fi
        if printf '%s\n' "$managed_state" | grep -q '^WM_STATE' &&
           [ -n "$managed_frame" ] && [ -n "$managed_title" ]; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] ||
            fail "AntonioWM no termino de reparentar la ventana $managed_window."
        sleep 0.1
    done
}

wait_for_frame_state()
{
    state_window=$1
    expected_state=$2
    attempt=0
    while :; do
        state_frame=$(frame_of "$state_window" || true)
        if [ -n "$state_frame" ] && [ "$(map_state "$state_frame")" = "$expected_state" ]; then
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] ||
            fail "El marco de $state_window no alcanzo el estado $expected_state."
        sleep 0.1
    done
}

wait_for_desktop()
{
    expected_desktop=$1
    attempt=0
    while [ "$(current_desktop)" != "$expected_desktop" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "No se activo el escritorio $expected_desktop."
        sleep 0.1
    done
}

wait_for_window()
{
    search_name=$1
    attempt=0
    while :; do
        found_window=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xdotool search --name "^$search_name$" 2>/dev/null | head -n 1 || true)
        if [ -n "$found_window" ]; then
            printf '%s\n' "$found_window"
            return
        fi
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "No aparecio '$search_name'."
        sleep 0.1
    done
}

click_frame_button()
{
    button_frame=$(frame_of "$1")
    case "$2" in
        close) button_name="AntonioWM Close" ;;
        maximize) button_name="AntonioWM Maximize" ;;
        minimize) button_name="AntonioWM Minimize" ;;
        *) fail "Boton de prueba desconocido: $2" ;;
    esac
    button_window=$(child_named "$button_frame" "$button_name")
    [ -n "$button_window" ] || fail "No se encontro $button_name."
    button_width=$(window_field "$button_window" WIDTH)
    button_height=$(window_field "$button_window" HEIGHT)
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --window "$button_window" \
        $((button_width / 2)) $((button_height / 2)) click 1
}

click_bar()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
        mousemove --window "$BAR_WINDOW" "$1" 14 click 1
}

click_bar_client()
{
    target_client=$1
    target_index=-1
    visible_count=0
    bar_desktop=$(current_desktop)
    listed_windows=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xprop -root _NET_CLIENT_LIST 2>/dev/null |
        sed -n 's/.*# //p' | tr ',' ' ')

    for listed_window in $listed_windows; do
        listed_decimal=$(printf '%d' "$listed_window")
        if [ "$(window_desktop "$listed_decimal")" = "$bar_desktop" ]; then
            if [ "$listed_decimal" = "$target_client" ]; then
                target_index=$visible_count
            fi
            visible_count=$((visible_count + 1))
        fi
    done
    if [ "$target_index" -lt 0 ] || [ "$visible_count" -le 0 ]; then
        fail "No se encontro el cliente $target_client en la barra."
    fi

    slot_available=408
    slot_width=$(((slot_available - (visible_count - 1) * 4) / visible_count))
    [ "$slot_width" -le 200 ] || slot_width=200
    slot_x=$((548 + target_index * (slot_width + 4) + slot_width / 2))
    [ "$slot_x" -lt 956 ] || slot_x=955
    click_bar "$slot_x"
}

assert_client_listed()
{
    listed_hex=$(printf '0x%x' "$1")
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CLIENT_LIST |
        grep -qi "$listed_hex" || fail "_NET_CLIENT_LIST perdio el cliente $1."
}

trap cleanup EXIT INT TERM
for command in Xephyr xdpyinfo xdotool xprop xwininfo timeout; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Falta el comando requerido: $command" >&2
        exit 1
    }
done
[ -n "${DISPLAY:-}" ] || {
    echo "Xephyr necesita un servidor X anfitrion." >&2
    exit 1
}

if [ "${ANTONIOWM_SKIP_XTERM:-0}" != 1 ] &&
   command -v xterm >/dev/null 2>&1; then
    # The container cannot chown pseudo-terminals to the traditional tty group.
    # These flags disable utmp and terminal messages only for this test run.
    ANTONIOWM_TERMINAL=${ANTONIOWM_TEST_TERMINAL:-"xterm +ut -mesg"}
    export ANTONIOWM_TERMINAL
fi

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
    [ "$attempt" -le 80 ] || fail "Xephyr no arranco."
    sleep 0.1
done

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/antoniowm" \
    >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.4
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino durante el arranque."

ROOT_PROPERTIES=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root \
    _NET_SUPPORTED _NET_NUMBER_OF_DESKTOPS _NET_CURRENT_DESKTOP \
    _NET_WORKAREA _NET_SUPPORTING_WM_CHECK)
printf '%s\n' "$ROOT_PROPERTIES" |
    grep -q '_NET_NUMBER_OF_DESKTOPS(CARDINAL) = 4' ||
    fail "AntonioWM no publico cuatro escritorios."
printf '%s\n' "$ROOT_PROPERTIES" |
    grep -q '_NET_CURRENT_DESKTOP(CARDINAL) = 0' ||
    fail "AntonioWM no inicio en el primer escritorio."
printf '%s\n' "$ROOT_PROPERTIES" |
    grep -q '_NET_WORKAREA(CARDINAL) = 0, 0, 1024, 740' ||
    fail "_NET_WORKAREA no comienza en y=0 o no reserva el panel."
for atom in _NET_ACTIVE_WINDOW _NET_CLIENT_LIST _NET_CLIENT_LIST_STACKING \
            _NET_CURRENT_DESKTOP _NET_NUMBER_OF_DESKTOPS _NET_WORKAREA \
            _NET_FRAME_EXTENTS _NET_REQUEST_FRAME_EXTENTS \
            _NET_WM_DESKTOP _NET_WM_STATE _NET_WM_STATE_FULLSCREEN \
            _NET_WM_STATE_MAXIMIZED_VERT _NET_WM_STATE_MAXIMIZED_HORZ \
            _NET_WM_STATE_HIDDEN _NET_WM_ALLOWED_ACTIONS \
            _NET_WM_ACTION_MINIMIZE _NET_WM_ACTION_CLOSE; do
    printf '%s\n' "$ROOT_PROPERTIES" | grep -q "$atom" ||
        fail "Falta $atom en _NET_SUPPORTED."
done

SUPPORTING_WINDOW=$(printf '%s\n' "$ROOT_PROPERTIES" |
    sed -n 's/^_NET_SUPPORTING_WM_CHECK(WINDOW): window id # //p')
[ -n "$SUPPORTING_WINDOW" ] || fail "Falta _NET_SUPPORTING_WM_CHECK."
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$SUPPORTING_WINDOW" _NET_WM_NAME |
    grep -q AntonioWM || fail "La ventana EWMH no identifica a AntonioWM."

OLD_BAR=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Bar$' 2>/dev/null | head -n 1 || true)
[ -z "$OLD_BAR" ] || fail "La barra superior antigua sigue presente."
BAR_WINDOW=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
    xdotool search --name '^AntonioWM Panel$' 2>/dev/null | head -n 1 || true)
if [ -z "$BAR_WINDOW" ] ||
   [ "$(window_field "$BAR_WINDOW" X)" != 0 ] ||
   [ "$(window_field "$BAR_WINDOW" Y)" != 740 ] ||
   [ "$(window_field "$BAR_WINDOW" WIDTH)" != 1024 ] ||
   [ "$(window_field "$BAR_WINDOW" HEIGHT)" != 28 ] ||
   [ "$(map_state "$BAR_WINDOW")" != IsViewable ]; then
    fail "El panel unico no ocupa 1024x28+0+740."
fi
click_bar 143
wait_for_desktop 1
click_bar 113
wait_for_desktop 0

set +e
DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 "$PROJECT_DIR/antoniowm" \
    >"$TEST_TMP/second-wm.log" 2>&1
SECOND_STATUS=$?
set -e
if [ "$SECOND_STATUS" -eq 0 ] ||
   ! grep -q "ya existe otro gestor" "$TEST_TMP/second-wm.log"; then
    fail "Fallo la deteccion de un segundo gestor."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "AWM Test One" "320x200+80+90" >"$TEST_TMP/client-one.log" 2>&1 &
CLIENT_ONE_PID=$!
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "AWM Test Two" "340x220+430+250" >"$TEST_TMP/client-two.log" 2>&1 &
CLIENT_TWO_PID=$!
WINDOW_ONE=$(wait_for_window "AWM Test One")
WINDOW_TWO=$(wait_for_window "AWM Test Two")
wait_for_management "$WINDOW_ONE"
wait_for_management "$WINDOW_TWO"
wait_for_frame_state "$WINDOW_ONE" IsViewable
wait_for_frame_state "$WINDOW_TWO" IsViewable

FRAME_ONE=$(frame_of "$WINDOW_ONE")
FRAME_TWO=$(frame_of "$WINDOW_TWO")
if [ "$(frame_field "$WINDOW_ONE" X)" != 80 ] ||
   [ "$(frame_field "$WINDOW_ONE" Y)" != 90 ] ||
   [ "$(frame_field "$WINDOW_ONE" WIDTH)" != 324 ] ||
   [ "$(frame_field "$WINDOW_ONE" HEIGHT)" != 226 ] ||
   [ "$(frame_field "$WINDOW_TWO" X)" != 430 ] ||
   [ "$(frame_field "$WINDOW_TWO" Y)" != 250 ]; then
    fail "El reparenting altero una geometria explicita."
fi
for managed in "$WINDOW_ONE:$FRAME_ONE" "$WINDOW_TWO:$FRAME_TWO"; do
    managed_client=${managed%%:*}
    managed_frame=${managed##*:}
    for child_name in "AntonioWM Titlebar" "AntonioWM Minimize" \
                      "AntonioWM Maximize" "AntonioWM Close"; do
        [ -n "$(child_named "$managed_frame" "$child_name")" ] ||
            fail "Falta '$child_name' en el marco $managed_frame."
    done
    [ "$(xwin_field "$managed_client" 'Border width')" = 0 ] ||
        fail "El cliente reparentado conserva un borde propio."
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$managed_client" \
        _NET_FRAME_EXTENTS |
        grep -q '_NET_FRAME_EXTENTS(CARDINAL) = 2, 2, 24, 2' ||
        fail "El cliente no publica los extents de su decoracion."
done
TITLEBAR_ONE=$(child_named "$FRAME_ONE" "AntonioWM Titlebar")
if [ "$(window_field "$TITLEBAR_ONE" WIDTH)" != 320 ] ||
   [ "$(window_field "$TITLEBAR_ONE" HEIGHT)" != 22 ]; then
    fail "La barra de titulo no conserva la geometria compacta 320x22."
fi
for button_name in "AntonioWM Minimize" "AntonioWM Maximize" \
                   "AntonioWM Close"; do
    BUTTON_WINDOW=$(child_named "$FRAME_ONE" "$button_name")
    if [ "$(window_field "$BUTTON_WINDOW" WIDTH)" != 16 ] ||
       [ "$(window_field "$BUTTON_WINDOW" HEIGHT)" != 16 ]; then
        fail "Los botones de titulo no tienen un tamano uniforme de 16x16."
    fi
done
for action in _NET_WM_ACTION_CLOSE _NET_WM_ACTION_MINIMIZE \
              _NET_WM_ACTION_MAXIMIZE_VERT _NET_WM_ACTION_MAXIMIZE_HORZ \
              _NET_WM_ACTION_FULLSCREEN; do
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$WINDOW_ONE" \
        _NET_WM_ALLOWED_ACTIONS | grep -q "$action" ||
        fail "Falta la accion EWMH $action."
done
assert_client_listed "$WINDOW_ONE"
assert_client_listed "$WINDOW_TWO"

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_ONE" || fail "No se activo W1."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+Tab
sleep 0.15
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_TWO" ] ||
    fail "Alt+Tab no avanzo a W2."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+shift+Tab
sleep 0.15
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_ONE" ] ||
    fail "Alt+Mayus+Tab no retrocedio a W1."
DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_TWO" || fail "No se activo W2."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --window "$WINDOW_ONE" 35 35 click 1
sleep 0.15
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_ONE" ] ||
    fail "El clic sobre una ventana inactiva no la enfoco."

FRAME_ONE=$(frame_of "$WINDOW_ONE")
TITLE_OLD_X=$(window_field "$FRAME_ONE" X)
TITLE_OLD_Y=$(window_field "$FRAME_ONE" Y)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --window "$FRAME_ONE" 100 14 mousedown 1 \
    mousemove_relative --sync 70 40 mouseup 1
sleep 0.15
if [ "$(frame_field "$WINDOW_ONE" X)" -le "$TITLE_OLD_X" ] ||
   [ "$(frame_field "$WINDOW_ONE" Y)" -le "$TITLE_OLD_Y" ]; then
    fail "Arrastrar la barra de titulo no movio el marco."
fi

ALT_OLD_X=$(frame_field "$WINDOW_ONE" X)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --window "$WINDOW_ONE" 140 80 keydown Alt mousedown 1 \
    mousemove_relative --sync 60 30 mouseup 1 keyup Alt
sleep 0.15
[ "$(frame_field "$WINDOW_ONE" X)" -gt "$ALT_OLD_X" ] ||
    fail "Alt+boton izquierdo dejo de mover."

ALT_OLD_WIDTH=$(frame_field "$WINDOW_ONE" WIDTH)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --window "$WINDOW_ONE" 290 170 keydown Alt mousedown 3 \
    mousemove_relative --sync 80 50 mouseup 3 keyup Alt
sleep 0.15
[ "$(frame_field "$WINDOW_ONE" WIDTH)" -gt "$ALT_OLD_WIDTH" ] ||
    fail "Alt+boton derecho dejo de redimensionar."

FRAME_ONE=$(frame_of "$WINDOW_ONE")
BORDER_OLD_WIDTH=$(window_field "$FRAME_ONE" WIDTH)
BORDER_OLD_HEIGHT=$(window_field "$FRAME_ONE" HEIGHT)
BOTTOM_RIGHT_HANDLE=$(child_named "$FRAME_ONE" "AntonioWM Resize Bottom Right")
[ -n "$BOTTOM_RIGHT_HANDLE" ] ||
    fail "No se encontro la zona inferior derecha de resize."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove --sync --window "$BOTTOM_RIGHT_HANDLE" 3 3
sleep 0.08
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousedown 1
sleep 0.12
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool \
    mousemove_relative --sync -- 40 30
sleep 0.12
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mouseup 1
sleep 0.15
if [ "$(frame_field "$WINDOW_ONE" WIDTH)" -le "$BORDER_OLD_WIDTH" ] ||
   [ "$(frame_field "$WINDOW_ONE" HEIGHT)" -le "$BORDER_OLD_HEIGHT" ]; then
    fail "El borde inferior derecho no redimensiono."
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowmove "$WINDOW_TWO" 360 210
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowsize "$WINDOW_TWO" 380 240
sleep 0.2
if [ "$(frame_field "$WINDOW_TWO" X)" != 360 ] ||
   [ "$(frame_field "$WINDOW_TWO" Y)" != 210 ] ||
   [ "$(frame_field "$WINDOW_TWO" WIDTH)" != 384 ] ||
   [ "$(frame_field "$WINDOW_TWO" HEIGHT)" != 266 ]; then
    fail "ConfigureRequest no actualizo frame/cliente."
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_TWO" || fail "No se preparo el maximizado."
TWO_NORMAL_X=$(frame_field "$WINDOW_TWO" X)
TWO_NORMAL_Y=$(frame_field "$WINDOW_TWO" Y)
TWO_NORMAL_WIDTH=$(frame_field "$WINDOW_TWO" WIDTH)
TWO_NORMAL_HEIGHT=$(frame_field "$WINDOW_TWO" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
attempt=0
while :; do
    if state_has "$WINDOW_TWO" _NET_WM_STATE_MAXIMIZED_VERT &&
       state_has "$WINDOW_TWO" _NET_WM_STATE_MAXIMIZED_HORZ &&
       [ "$(frame_field "$WINDOW_TWO" X)" = 0 ] &&
       [ "$(frame_field "$WINDOW_TWO" Y)" = 0 ] &&
       [ "$(frame_field "$WINDOW_TWO" WIDTH)" = 1024 ] &&
       [ "$(frame_field "$WINDOW_TWO" HEIGHT)" = 740 ]; then
        break
    fi
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "Super+M no respeto la barra."
    sleep 0.1
done
if [ "$(xwin_field "$WINDOW_TWO" Width)" != 1020 ] ||
   [ "$(xwin_field "$WINDOW_TWO" Height)" != 714 ]; then
    fail "El contenido maximizado tiene geometria incorrecta."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
sleep 0.2
if state_has "$WINDOW_TWO" _NET_WM_STATE_MAXIMIZED_VERT ||
   [ "$(frame_field "$WINDOW_TWO" X)" != "$TWO_NORMAL_X" ] ||
   [ "$(frame_field "$WINDOW_TWO" Y)" != "$TWO_NORMAL_Y" ] ||
   [ "$(frame_field "$WINDOW_TWO" WIDTH)" != "$TWO_NORMAL_WIDTH" ] ||
   [ "$(frame_field "$WINDOW_TWO" HEIGHT)" != "$TWO_NORMAL_HEIGHT" ]; then
    fail "Super+M no restauro la geometria exacta."
fi
click_frame_button "$WINDOW_TWO" maximize
sleep 0.2
if ! state_has "$WINDOW_TWO" _NET_WM_STATE_MAXIMIZED_HORZ ||
   [ "$(frame_field "$WINDOW_TWO" Y)" != 0 ]; then
    fail "El boton maximizar no uso la logica de Super+M."
fi
click_frame_button "$WINDOW_TWO" maximize
sleep 0.2
if state_has "$WINDOW_TWO" _NET_WM_STATE_MAXIMIZED_HORZ ||
   [ "$(frame_field "$WINDOW_TWO" X)" != "$TWO_NORMAL_X" ] ||
   [ "$(frame_field "$WINDOW_TWO" WIDTH)" != "$TWO_NORMAL_WIDTH" ]; then
    fail "El boton maximizar no restauro exactamente."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_ONE" || fail "No se preparo la minimizacion."
click_frame_button "$WINDOW_ONE" minimize
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
if ! wm_state_has "$WINDOW_ONE" Iconic ||
   ! state_has "$WINDOW_ONE" _NET_WM_STATE_HIDDEN; then
    fail "Minimizar no publico Iconic y HIDDEN."
fi
assert_client_listed "$WINDOW_ONE"
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_TWO" ] ||
    fail "Minimizar no enfoco W2."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+Tab
sleep 0.15
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_TWO" ] ||
    fail "Alt+Tab incluyo una ventana minimizada."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+shift+Tab
sleep 0.15
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_TWO" ] ||
    fail "Alt+Mayus+Tab incluyo una ventana minimizada."

# ICCCM: al iconificar se desmapea tambien el cliente. Un XMapWindow del
# propio cliente debe generar MapRequest y restaurarlo.
[ "$(map_state "$WINDOW_ONE")" = IsUnMapped ] ||
    fail "La minimizacion dejo mapeada la ventana cliente."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowmap "$WINDOW_ONE"
wait_for_frame_state "$WINDOW_ONE" IsViewable
if ! wm_state_has "$WINDOW_ONE" Normal ||
   state_has "$WINDOW_ONE" _NET_WM_STATE_HIDDEN; then
    fail "MapRequest no desiconifico la ventana minimizada."
fi
click_frame_button "$WINDOW_ONE" minimize
wait_for_frame_state "$WINDOW_ONE" IsUnMapped

# La posicion de W1 se deriva de _NET_CLIENT_LIST para no depender del orden
# en que dos procesos concurrentes alcancen MapRequest.
click_bar_client "$WINDOW_ONE"
wait_for_frame_state "$WINDOW_ONE" IsViewable
if ! wm_state_has "$WINDOW_ONE" Normal ||
   state_has "$WINDOW_ONE" _NET_WM_STATE_HIDDEN ||
   [ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" != "$WINDOW_ONE" ]; then
    fail "La barra no restauro y enfoco W1."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowminimize "$WINDOW_ONE"
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
wm_state_has "$WINDOW_ONE" Iconic || fail "WM_CHANGE_STATE no produjo Iconic."
click_bar_client "$WINDOW_ONE"
wait_for_frame_state "$WINDOW_ONE" IsViewable

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "AWM Fullscreen" "300x180+150+120" fullscreen \
    >"$TEST_TMP/client-fullscreen.log" 2>&1 &
CLIENT_FULLSCREEN_PID=$!
WINDOW_FULLSCREEN=$(wait_for_window "AWM Fullscreen")
wait_for_management "$WINDOW_FULLSCREEN"
attempt=0
while :; do
    FULLSCREEN_FRAME=$(frame_of "$WINDOW_FULLSCREEN")
    FULLSCREEN_TITLE=$(child_named "$FULLSCREEN_FRAME" "AntonioWM Titlebar" || true)
    if state_has "$WINDOW_FULLSCREEN" _NET_WM_STATE_FULLSCREEN &&
       [ "$(window_field "$FULLSCREEN_FRAME" X)" = 0 ] &&
       [ "$(window_field "$FULLSCREEN_FRAME" Y)" = 0 ] &&
       [ "$(window_field "$FULLSCREEN_FRAME" WIDTH)" = 1024 ] &&
       [ "$(window_field "$FULLSCREEN_FRAME" HEIGHT)" = 768 ] &&
       [ "$(xwin_field "$WINDOW_FULLSCREEN" Width)" = 1024 ] &&
       [ "$(xwin_field "$WINDOW_FULLSCREEN" Height)" = 768 ] &&
       DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$WINDOW_FULLSCREEN" \
           _NET_FRAME_EXTENTS | grep -q '= 0, 0, 0, 0' &&
       [ "$(map_state "$FULLSCREEN_TITLE")" = IsUnMapped ]; then
        break
    fi
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "Fullscreen EWMH no cubrio la pantalla."
    sleep 0.1
done
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+f
sleep 0.2
if state_has "$WINDOW_FULLSCREEN" _NET_WM_STATE_FULLSCREEN ||
   [ "$(frame_field "$WINDOW_FULLSCREEN" X)" != 150 ] ||
   [ "$(frame_field "$WINDOW_FULLSCREEN" Y)" != 120 ] ||
   [ "$(frame_field "$WINDOW_FULLSCREEN" WIDTH)" != 304 ] ||
   [ "$(frame_field "$WINDOW_FULLSCREEN" HEIGHT)" != 206 ] ||
   ! DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$WINDOW_FULLSCREEN" \
       _NET_FRAME_EXTENTS | grep -q '= 2, 2, 24, 2' ||
   [ "$(map_state "$FULLSCREEN_TITLE")" != IsViewable ]; then
    fail "Salir de fullscreen no restauro marco y geometria."
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "AWM Auto One" "280x160" >"$TEST_TMP/client-auto-one.log" 2>&1 &
CLIENT_AUTO_ONE_PID=$!
DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" \
    "AWM Auto Two" "280x160" >"$TEST_TMP/client-auto-two.log" 2>&1 &
CLIENT_AUTO_TWO_PID=$!
WINDOW_AUTO_ONE=$(wait_for_window "AWM Auto One")
WINDOW_AUTO_TWO=$(wait_for_window "AWM Auto Two")
wait_for_management "$WINDOW_AUTO_ONE"
wait_for_management "$WINDOW_AUTO_TWO"
AUTO_ONE_X=$(frame_field "$WINDOW_AUTO_ONE" X)
AUTO_ONE_Y=$(frame_field "$WINDOW_AUTO_ONE" Y)
AUTO_TWO_X=$(frame_field "$WINDOW_AUTO_TWO" X)
AUTO_TWO_Y=$(frame_field "$WINDOW_AUTO_TWO" Y)
if [ "$AUTO_ONE_X" = "$AUTO_TWO_X" ] && [ "$AUTO_ONE_Y" = "$AUTO_TWO_Y" ]; then
    fail "La cascada superpuso exactamente las ventanas."
fi
if [ "$AUTO_ONE_Y" -lt 0 ] || [ "$AUTO_TWO_Y" -lt 0 ]; then
    fail "La cascada salio por la parte superior del monitor."
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_AUTO_TWO" || fail "No se activo Auto Two."
click_bar_client "$WINDOW_ONE"
sleep 0.15
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getwindowfocus)" = "$WINDOW_ONE" ] ||
    fail "La lista de la barra no enfoco W1."
click_bar 143
wait_for_desktop 1
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
click_bar 113
wait_for_desktop 0
wait_for_frame_state "$WINDOW_ONE" IsViewable

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_ONE" || fail "No se activo W1 para enviarla."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+2
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
if [ "$(window_desktop "$WINDOW_ONE")" != 1 ] ||
   [ "$(current_desktop)" != 0 ]; then
    fail "Super+Mayus+2 no envio W1 correctamente."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
wait_for_desktop 1
wait_for_frame_state "$WINDOW_ONE" IsViewable
wait_for_frame_state "$WINDOW_TWO" IsUnMapped
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+1
wait_for_desktop 0
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
wait_for_frame_state "$WINDOW_TWO" IsViewable

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_ONE" ||
    fail "_NET_ACTIVE_WINDOW no cambio al escritorio de W1."
wait_for_desktop 1
wait_for_frame_state "$WINDOW_ONE" IsViewable
[ "$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool getactivewindow)" = "$WINDOW_ONE" ] ||
    fail "_NET_ACTIVE_WINDOW no sincronizo el foco."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool set_desktop_for_window "$WINDOW_ONE" 0
attempt=0
while [ "$(window_desktop "$WINDOW_ONE")" != 0 ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "_NET_WM_DESKTOP no movio W1."
    sleep 0.1
done
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+1
wait_for_desktop 0
wait_for_frame_state "$WINDOW_ONE" IsViewable

click_frame_button "$WINDOW_TWO" close
attempt=0
while kill -0 "$CLIENT_TWO_PID" 2>/dev/null; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "El boton cerrar no envio WM_DELETE_WINDOW."
    sleep 0.1
done
CLIENT_TWO_PID=
DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_AUTO_TWO" || fail "No se activo Auto Two."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+F4
attempt=0
while kill -0 "$CLIENT_AUTO_TWO_PID" 2>/dev/null; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "Alt+F4 no envio WM_DELETE_WINDOW."
    sleep 0.1
done
CLIENT_AUTO_TWO_PID=

if [ "${ANTONIOWM_SKIP_XTERM:-0}" != 1 ] &&
   command -v xterm >/dev/null 2>&1; then
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+Return
    attempt=0
    while :; do
        WINDOW_XTERM=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
            xdotool search --class 'XTerm' 2>/dev/null | head -n 1 || true)
        [ -z "$WINDOW_XTERM" ] || break
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "Super+Enter no abrio xterm."
        sleep 0.1
    done
    wait_for_management "$WINDOW_XTERM"
    XTERM_TESTED=1
    DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
        windowactivate --sync "$WINDOW_XTERM" || fail "xterm no acepto activacion."
    DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key alt+F4
    attempt=0
    while DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$WINDOW_XTERM" \
          >/dev/null 2>&1; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 80 ] || fail "Alt+F4 no cerro xterm."
        sleep 0.1
    done
    WINDOW_XTERM=
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_ONE" || fail "No se preparo W1 para reiniciar."
ONE_RESTORE_X=$(frame_field "$WINDOW_ONE" X)
ONE_RESTORE_Y=$(frame_field "$WINDOW_ONE" Y)
ONE_RESTORE_WIDTH=$(frame_field "$WINDOW_ONE" WIDTH)
ONE_RESTORE_HEIGHT=$(frame_field "$WINDOW_ONE" HEIGHT)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
attempt=0
while ! state_has "$WINDOW_ONE" _NET_WM_STATE_MAXIMIZED_HORZ; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 80 ] || fail "No se maximizo W1 antes del reinicio."
    sleep 0.1
done
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+2
wait_for_frame_state "$WINDOW_ONE" IsUnMapped

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_AUTO_ONE" || fail "No se activo Auto One."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool windowminimize "$WINDOW_AUTO_ONE"
wait_for_frame_state "$WINDOW_AUTO_ONE" IsUnMapped
state_has "$WINDOW_AUTO_ONE" _NET_WM_STATE_HIDDEN ||
    fail "No se preparo minimizacion antes de reiniciar."

START_COUNT=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
attempt=0
while :; do
    kill -0 "$WM_PID" 2>/dev/null || fail "El reinicio termino AntonioWM."
    NEW_START_COUNT=$(grep -c 'iniciado en' "$TEST_TMP/wm.log" || true)
    [ "$NEW_START_COUNT" -le "$START_COUNT" ] || break
    attempt=$((attempt + 1))
    [ "$attempt" -le 120 ] || fail "AntonioWM no completo el reinicio."
    sleep 0.1
done
wait_for_management "$WINDOW_ONE"
wait_for_management "$WINDOW_AUTO_ONE"

if [ "$(current_desktop)" != 0 ] ||
   [ "$(window_desktop "$WINDOW_ONE")" != 1 ]; then
    fail "El reinicio no conservo los escritorios."
fi
wait_for_frame_state "$WINDOW_ONE" IsUnMapped
if ! state_has "$WINDOW_ONE" _NET_WM_STATE_MAXIMIZED_VERT ||
   ! state_has "$WINDOW_ONE" _NET_WM_STATE_MAXIMIZED_HORZ; then
    fail "El reinicio no conservo el maximizado."
fi
wait_for_frame_state "$WINDOW_AUTO_ONE" IsUnMapped
if ! wm_state_has "$WINDOW_AUTO_ONE" Iconic ||
   ! state_has "$WINDOW_AUTO_ONE" _NET_WM_STATE_HIDDEN; then
    fail "El reinicio no conservo la minimizacion."
fi
assert_client_listed "$WINDOW_ONE"
assert_client_listed "$WINDOW_AUTO_ONE"

DISPLAY="$NESTED_CLIENT_DISPLAY" timeout 3 xdotool \
    windowactivate --sync "$WINDOW_AUTO_ONE" ||
    fail "_NET_ACTIVE_WINDOW no restauro Auto One."
wait_for_frame_state "$WINDOW_AUTO_ONE" IsViewable
if state_has "$WINDOW_AUTO_ONE" _NET_WM_STATE_HIDDEN ||
   ! wm_state_has "$WINDOW_AUTO_ONE" Normal; then
    fail "Restaurar no limpio el estado minimizado."
fi

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+2
wait_for_desktop 1
wait_for_frame_state "$WINDOW_ONE" IsViewable
if [ "$(frame_field "$WINDOW_ONE" X)" != 0 ] ||
   [ "$(frame_field "$WINDOW_ONE" Y)" != 0 ] ||
   [ "$(frame_field "$WINDOW_ONE" WIDTH)" != 1024 ] ||
   [ "$(frame_field "$WINDOW_ONE" HEIGHT)" != 740 ]; then
    fail "El maximizado no sobrevivio al reinicio."
fi
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+m
sleep 0.2
if state_has "$WINDOW_ONE" _NET_WM_STATE_MAXIMIZED_VERT ||
   [ "$(frame_field "$WINDOW_ONE" X)" != "$ONE_RESTORE_X" ] ||
   [ "$(frame_field "$WINDOW_ONE" Y)" != "$ONE_RESTORE_Y" ] ||
   [ "$(frame_field "$WINDOW_ONE" WIDTH)" != "$ONE_RESTORE_WIDTH" ] ||
   [ "$(frame_field "$WINDOW_ONE" HEIGHT)" != "$ONE_RESTORE_HEIGHT" ]; then
    fail "La restauracion tras reiniciar perdio geometria."
fi

if [ "$XTERM_TESTED" -eq 1 ]; then
    TERMINAL_RESULT=" y Super+Enter/xterm"
else
    TERMINAL_RESULT=" (xterm no disponible: Super+Enter omitido)"
fi
echo "Prueba Xephyr FASE 3 superada: regresion FASES 1/2, reparenting, decoraciones, botones, barra, minimizacion ICCCM/EWMH, fullscreen, escritorios, cascada y reinicio${TERMINAL_RESULT}."
