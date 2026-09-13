#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NESTED_SERVER_DISPLAY=${ANTONIOWM_PHASE4_TEST_DISPLAY:-:97}
NESTED_CLIENT_DISPLAY=${ANTONIOWM_PHASE4_TEST_CLIENT_DISPLAY:-$NESTED_SERVER_DISPLAY}
TEST_TMP=$(mktemp -d)
XEPHYR_PID=
WM_PID=
CLIENT_PIDS=

cleanup()
{
    for process in $CLIENT_PIDS "$WM_PID" "$XEPHYR_PID"; do
        if [ -n "$process" ]; then
            kill "$process" 2>/dev/null || true
        fi
    done
    for process in $CLIENT_PIDS "$WM_PID" "$XEPHYR_PID"; do
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
        sed -n '1,320p' "$TEST_TMP/wm.log" >&2
    fi
    if [ -s "$TEST_TMP/xephyr.log" ]; then
        echo "Ultimas lineas de Xephyr:" >&2
        tail -n 50 "$TEST_TMP/xephyr.log" >&2
    fi
    exit 1
}

start_client()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-test-client" "$@" \
        >"$TEST_TMP/client-$1.log" 2>&1 &
    LAST_CLIENT_PID=$!
    CLIENT_PIDS="$CLIENT_PIDS $LAST_CLIENT_PID"
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
        [ "$attempt" -le 100 ] || fail "No aparecio '$search_name'."
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

frame_of()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" -tree 2>/dev/null |
        sed -n 's/^  Parent window id: \(0x[0-9a-fA-F]*\).*/\1/p' |
        head -n 1
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

map_state()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -id "$1" 2>/dev/null |
        sed -n 's/^  Map State: //p'
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

window_desktop()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$1" _NET_WM_DESKTOP 2>/dev/null |
        sed -n 's/.* = //p'
}

current_desktop()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_CURRENT_DESKTOP 2>/dev/null |
        sed -n 's/.* = //p'
}

wait_for_desktop()
{
    expected=$1
    attempt=0
    while [ "$(current_desktop)" != "$expected" ]; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "No se activo el escritorio $expected."
        sleep 0.1
    done
}

state_has()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$1" _NET_WM_STATE 2>/dev/null |
        grep -q "$2"
}

active_window()
{
    active_hex=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xprop -root _NET_ACTIVE_WINDOW 2>/dev/null |
        sed -n 's/.*# \(0x[0-9a-fA-F]*\).*/\1/p')
    if [ -z "$active_hex" ] || [ "$active_hex" = "0x0" ]; then
        printf '0\n'
    else
        printf '%d\n' "$active_hex"
    fi
}

stack_index()
{
    target_hex=$(printf '0x%x' "$1")
    stacking=$(DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xprop -root _NET_CLIENT_LIST_STACKING 2>/dev/null |
        sed -n 's/.*# //p' | tr ',' ' ')
    index=0
    for candidate in $stacking; do
        candidate_lower=$(printf '%s' "$candidate" | tr 'A-F' 'a-f')
        if [ "$candidate_lower" = "$target_hex" ]; then
            printf '%s\n' "$index"
            return
        fi
        index=$((index + 1))
    done
    printf '%s\n' -1
}

wait_for_property()
{
    property_window=$1
    property_name=$2
    property_text=$3
    attempt=0
    while ! DISPLAY="$NESTED_CLIENT_DISPLAY" \
        xprop -id "$property_window" "$property_name" 2>/dev/null |
        grep -q "$property_text"; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] ||
            fail "$property_name de $property_window no contiene $property_text."
        sleep 0.1
    done
}

wait_for_workarea()
{
    expected=$1
    attempt=0
    while ! DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_WORKAREA 2>/dev/null |
        grep -q "$expected"; do
        attempt=$((attempt + 1))
        [ "$attempt" -le 100 ] || fail "_NET_WORKAREA no alcanzo $expected."
        sleep 0.1
    done
}

ewmh_request()
{
    DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/tests/awm-ewmh-request" "$@"
}

trap cleanup EXIT INT TERM

for command in Xephyr xdpyinfo xdotool xprop xwininfo stdbuf timeout; do
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

DISPLAY="$NESTED_CLIENT_DISPLAY" "$PROJECT_DIR/antoniowm" \
    >"$TEST_TMP/wm.log" 2>&1 &
WM_PID=$!
sleep 0.4
kill -0 "$WM_PID" 2>/dev/null || fail "AntonioWM termino al arrancar."

SUPPORTED=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_SUPPORTED)
for atom in _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_DIALOG \
            _NET_WM_WINDOW_TYPE_UTILITY _NET_WM_WINDOW_TYPE_TOOLBAR \
            _NET_WM_WINDOW_TYPE_MENU _NET_WM_WINDOW_TYPE_DROPDOWN_MENU \
            _NET_WM_WINDOW_TYPE_POPUP_MENU _NET_WM_WINDOW_TYPE_TOOLTIP \
            _NET_WM_WINDOW_TYPE_NOTIFICATION _NET_WM_WINDOW_TYPE_SPLASH \
            _NET_WM_WINDOW_TYPE_DOCK \
            _NET_WM_WINDOW_TYPE_DESKTOP _NET_WM_STATE_ABOVE \
            _NET_WM_STATE_BELOW _NET_WM_STATE_STICKY \
            _NET_WM_STATE_SKIP_TASKBAR _NET_WM_STATE_SKIP_PAGER \
            _NET_WM_STATE_DEMANDS_ATTENTION _NET_WM_STATE_MODAL \
            _NET_WM_STRUT _NET_WM_STRUT_PARTIAL _NET_CLOSE_WINDOW \
            _NET_MOVERESIZE_WINDOW _NET_WM_MOVERESIZE _NET_RESTACK_WINDOW; do
    printf '%s\n' "$SUPPORTED" | grep -q "$atom" ||
        fail "Falta $atom en _NET_SUPPORTED."
done
DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -root _NET_DESKTOP_NAMES |
    grep -q '"1", "2", "3", "4"' ||
    fail "_NET_DESKTOP_NAMES no publica los nombres configurados."

WORKAREA_SPY_LOG="$TEST_TMP/workarea-spy.log"
DISPLAY="$NESTED_CLIENT_DISPLAY" stdbuf -oL \
    xprop -spy -root _NET_WORKAREA >"$WORKAREA_SPY_LOG" 2>&1 &
WORKAREA_SPY_PID=$!
CLIENT_PIDS="$CLIENT_PIDS $WORKAREA_SPY_PID"
attempt=0
while [ ! -s "$WORKAREA_SPY_LOG" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 50 ] || fail "xprop -spy no observo _NET_WORKAREA."
    sleep 0.1
done
start_client "P4 Stable Workarea" "180x90+760+650" none --min 1x1 --inc 1x1
STABLE_WORKAREA=$(wait_for_window "P4 Stable Workarea")
wait_for_managed "$STABLE_WORKAREA"
sleep 0.2
kill "$WORKAREA_SPY_PID" 2>/dev/null || true
wait "$WORKAREA_SPY_PID" 2>/dev/null || true
[ "$(grep -c '^_NET_WORKAREA' "$WORKAREA_SPY_LOG")" -eq 1 ] ||
    fail "Gestionar una ventana sin strut republico _NET_WORKAREA sin cambios."
ewmh_request close "$STABLE_WORKAREA"
wait_for_gone "$STABLE_WORKAREA"

ROOT_HEX=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xwininfo -root 2>/dev/null |
    sed -n 's/^xwininfo: Window id: \(0x[0-9a-fA-F]*\).*/\1/p')

start_client "P4 Parent" "400x300+100+100" none
PARENT_PID=$LAST_CLIENT_PID
PARENT=$(wait_for_window "P4 Parent")
wait_for_managed "$PARENT"

start_client "P4 Dialog" "220x120" none --type DIALOG \
    --transient "$PARENT" --initial-state MODAL
DIALOG_PID=$LAST_CLIENT_PID
DIALOG=$(wait_for_window "P4 Dialog")
wait_for_managed "$DIALOG"
wait_for_property "$DIALOG" WM_TRANSIENT_FOR "$(printf '0x%x' "$PARENT")"
wait_for_property "$DIALOG" _NET_WM_STATE _NET_WM_STATE_MODAL
[ "$(window_desktop "$DIALOG")" = "$(window_desktop "$PARENT")" ] ||
    fail "El dialogo no heredo el escritorio del padre."
[ "$(frame_field "$DIALOG" X)" = 190 ] &&
[ "$(frame_field "$DIALOG" Y)" = 190 ] ||
    fail "El dialogo no se centro respecto a su padre."
[ "$(active_window)" = "$DIALOG" ] || fail "El dialogo no recibio el foco."
[ "$(stack_index "$DIALOG")" -gt "$(stack_index "$PARENT")" ] ||
    fail "El dialogo no permanece por encima de su padre."

ewmh_request desktop "$PARENT" 1
attempt=0
while [ "$(window_desktop "$PARENT")" != 1 ] ||
      [ "$(window_desktop "$DIALOG")" != 1 ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || fail "El dialogo no siguio al padre de escritorio."
    sleep 0.1
done
wait_for_frame_state "$PARENT" IsUnMapped
wait_for_frame_state "$DIALOG" IsUnMapped
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool set_desktop 1
wait_for_desktop 1
wait_for_frame_state "$PARENT" IsViewable
wait_for_frame_state "$DIALOG" IsViewable
ewmh_request desktop "$PARENT" 0
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool set_desktop 0
wait_for_desktop 0
wait_for_frame_state "$PARENT" IsViewable
wait_for_frame_state "$DIALOG" IsViewable

start_client "P4 Utility" "240x100+20+420" none --type UTILITY \
    --min 1x1 --inc 1x1
UTILITY=$(wait_for_window "P4 Utility")
wait_for_managed "$UTILITY"
[ -n "$(child_named "$(frame_of "$UTILITY")" "AntonioWM Titlebar")" ] ||
    fail "UTILITY deberia conservar decoracion completa."

start_client "P4 Toolbar" "200x80+300+420" none --type TOOLBAR \
    --min 1x1 --inc 1x1
TOOLBAR=$(wait_for_window "P4 Toolbar")
wait_for_managed "$TOOLBAR"
[ -z "$(child_named "$(frame_of "$TOOLBAR")" "AntonioWM Titlebar")" ] ||
    fail "TOOLBAR no deberia tener barra de titulo."
[ "$(frame_field "$TOOLBAR" WIDTH)" = 204 ] &&
[ "$(frame_field "$TOOLBAR" HEIGHT)" = 84 ] ||
    fail "TOOLBAR no usa decoracion de solo borde."
wait_for_property "$TOOLBAR" _NET_FRAME_EXTENTS "2, 2, 2, 2"

for special_type in MENU DROPDOWN_MENU POPUP_MENU TOOLTIP NOTIFICATION SPLASH DESKTOP; do
    special_title="P4 Type $special_type"
    start_client "$special_title" "180x70+700+100" none \
        --type "$special_type" --min 1x1 --inc 1x1
    SPECIAL=$(wait_for_window "$special_title")
    wait_for_managed "$SPECIAL"
    [ -z "$(child_named "$(frame_of "$SPECIAL")" "AntonioWM Titlebar")" ] ||
        fail "$special_type no deberia llevar barra de titulo."
    [ "$(frame_field "$SPECIAL" WIDTH)" = 180 ] &&
    [ "$(frame_field "$SPECIAL" HEIGHT)" = 70 ] ||
        fail "$special_type deberia quedar sin decoracion."
    wait_for_property "$SPECIAL" _NET_FRAME_EXTENTS "0, 0, 0, 0"
    ewmh_request close "$SPECIAL"
    wait_for_gone "$SPECIAL"
done

start_client "P4 Motif None" "210x90+620+430" none --motif none \
    --min 1x1 --inc 1x1
MOTIF_NONE=$(wait_for_window "P4 Motif None")
wait_for_managed "$MOTIF_NONE"
[ -z "$(child_named "$(frame_of "$MOTIF_NONE")" "AntonioWM Titlebar")" ] ||
    fail "Motif decorations=0 no fue respetado."
[ "$(frame_field "$MOTIF_NONE" WIDTH)" = 210 ] ||
    fail "Motif sin decoracion aun anade extents."

start_client "P4 Motif Border" "210x90+830+430" none --motif border \
    --min 1x1 --inc 1x1
MOTIF_BORDER=$(wait_for_window "P4 Motif Border")
wait_for_managed "$MOTIF_BORDER"
[ -z "$(child_named "$(frame_of "$MOTIF_BORDER")" "AntonioWM Titlebar")" ] ||
    fail "Motif border-only creo una barra de titulo."
[ "$(frame_field "$MOTIF_BORDER" WIDTH)" = 214 ] ||
    fail "Motif border-only no creo el borde esperado."

start_client "P4 Override Popup" "160x60+600+600" none --type POPUP_MENU \
    --override --min 1x1 --inc 1x1
OVERRIDE=$(wait_for_window "P4 Override Popup")
sleep 0.2
[ "$(frame_of "$OVERRIDE")" = "$ROOT_HEX" ] ||
    fail "Una override-redirect fue gestionada indebidamente."
if DISPLAY="$NESTED_CLIENT_DISPLAY" xprop -id "$OVERRIDE" WM_STATE 2>/dev/null |
   grep -q '^WM_STATE(WM_STATE)'; then
    fail "Una override-redirect recibio WM_STATE."
fi

start_client "P4 Take Focus" "260x110+30+580" none --input false \
    --take-focus --min 1x1 --inc 1x1
TAKE_FOCUS=$(wait_for_window "P4 Take Focus")
wait_for_managed "$TAKE_FOCUS"
wait_for_property "$TAKE_FOCUS" _AWM_TEST_TAKE_FOCUS_COUNT "= 1"
[ "$(active_window)" = "$TAKE_FOCUS" ] ||
    fail "WM_TAKE_FOCUS no activo el cliente globalmente activo."

start_client "P4 No Focus Utility" "190x80+330+580" none --type UTILITY \
    --input false --min 1x1 --inc 1x1
NO_FOCUS=$(wait_for_window "P4 No Focus Utility")
wait_for_managed "$NO_FOCUS"
[ "$(active_window)" = "$TAKE_FOCUS" ] ||
    fail "Una ventana InputHint=False sin WM_TAKE_FOCUS robo el foco."

start_client "P4 Urgent" "190x80+540+580" demands_attention --type UTILITY \
    --urgent --input false --min 1x1 --inc 1x1
URGENT=$(wait_for_window "P4 Urgent")
wait_for_managed "$URGENT"
wait_for_property "$URGENT" WM_HINTS "urgency hint"
wait_for_property "$URGENT" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
[ "$(active_window)" = "$TAKE_FOCUS" ] ||
    fail "Una solicitud de atencion cambio el foco automaticamente."

start_client "P4 Size Hints" "333x211+50+300" none --min 200x120 \
    --max 360x260 --base 10x10 --inc 25x20 --aspect 4:3:16:9
SIZE_HINTS=$(wait_for_window "P4 Size Hints")
wait_for_managed "$SIZE_HINTS"
[ "$(window_field "$SIZE_HINTS" WIDTH)" = 310 ] &&
[ "$(window_field "$SIZE_HINTS" HEIGHT)" = 210 ] ||
    fail "El tamano inicial no respeto base size/incrementos/aspect ratio."
ewmh_request moveresize "$SIZE_HINTS" 200 200 800 50
sleep 0.2
[ "$(frame_field "$SIZE_HINTS" X)" = 200 ] &&
[ "$(frame_field "$SIZE_HINTS" Y)" = 200 ] &&
[ "$(window_field "$SIZE_HINTS" WIDTH)" = 360 ] &&
[ "$(window_field "$SIZE_HINTS" HEIGHT)" = 210 ] ||
    fail "_NET_MOVERESIZE_WINDOW no respeto maximo, minimo, incrementos y aspecto."

DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove 250 250
ewmh_request interactive "$SIZE_HINTS" 250 250 8
sleep 0.1
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool mousemove 340 300 mouseup 1
sleep 0.2
[ "$(frame_field "$SIZE_HINTS" X)" -gt 200 ] &&
[ "$(frame_field "$SIZE_HINTS" Y)" -gt 200 ] ||
    fail "_NET_WM_MOVERESIZE no inicio un movimiento interactivo."

start_client "P4 Dock Partial" "1024x40+0+0" none --type DOCK \
    --min 1x1 --inc 1x1 --strut-partial-top 40
DOCK_PARTIAL=$(wait_for_window "P4 Dock Partial")
wait_for_managed "$DOCK_PARTIAL"
wait_for_workarea "0, 68, 1024, 700"
BAR=$(DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool search --name '^AntonioWM Bar$' |
    head -n 1)
[ "$(window_field "$BAR" Y)" = 40 ] ||
    fail "La barra propia no convivio con _NET_WM_STRUT_PARTIAL."

ewmh_request state "$PARENT" add MAXIMIZED_VERT MAXIMIZED_HORZ
sleep 0.2
[ "$(frame_field "$PARENT" X)" = 0 ] &&
[ "$(frame_field "$PARENT" Y)" = 68 ] &&
[ "$(frame_field "$PARENT" WIDTH)" = 1024 ] &&
[ "$(frame_field "$PARENT" HEIGHT)" = 700 ] ||
    fail "El maximizado no respeto strut parcial mas barra propia."
ewmh_request state "$PARENT" remove MAXIMIZED_VERT MAXIMIZED_HORZ
ewmh_request close "$DOCK_PARTIAL"
wait_for_gone "$DOCK_PARTIAL"
wait_for_workarea "0, 28, 1024, 740"
[ "$(window_field "$BAR" Y)" = 0 ] || fail "La barra no volvio a y=0."

start_client "P4 Dock Legacy" "30x768+0+0" none --type DOCK \
    --min 1x1 --inc 1x1 --strut 30,0,0,0
DOCK_LEGACY=$(wait_for_window "P4 Dock Legacy")
wait_for_managed "$DOCK_LEGACY"
wait_for_workarea "30, 28, 994, 740"
[ "$(window_field "$BAR" X)" = 30 ] &&
[ "$(window_field "$BAR" WIDTH)" = 994 ] ||
    fail "La barra no convivio con _NET_WM_STRUT legado."
ewmh_request close "$DOCK_LEGACY"
wait_for_gone "$DOCK_LEGACY"
wait_for_workarea "0, 28, 1024, 740"

start_client "P4 Below" "180x100+400+260" below --min 1x1 --inc 1x1
BELOW=$(wait_for_window "P4 Below")
wait_for_managed "$BELOW"
start_client "P4 Above" "180x100+430+290" above --min 1x1 --inc 1x1
ABOVE=$(wait_for_window "P4 Above")
wait_for_managed "$ABOVE"
ewmh_request active "$PARENT" 2 0
sleep 0.2
[ "$(stack_index "$BELOW")" -lt "$(stack_index "$PARENT")" ] ||
    fail "_NET_WM_STATE_BELOW no mantuvo la capa inferior."
[ "$(stack_index "$ABOVE")" -gt "$(stack_index "$PARENT")" ] ||
    fail "_NET_WM_STATE_ABOVE no mantuvo always-on-top."

start_client "P4 Fullscreen Layer" "240x140+500+350" fullscreen \
    --min 1x1 --inc 1x1
FULLSCREEN=$(wait_for_window "P4 Fullscreen Layer")
wait_for_managed "$FULLSCREEN"
wait_for_property "$FULLSCREEN" _NET_WM_STATE _NET_WM_STATE_FULLSCREEN
[ "$(stack_index "$FULLSCREEN")" -gt "$(stack_index "$ABOVE")" ] ||
    fail "Fullscreen no quedo por encima de always-on-top."
ewmh_request state "$FULLSCREEN" remove FULLSCREEN

ewmh_request state "$MOTIF_NONE" add STICKY SKIP_PAGER
wait_for_property "$MOTIF_NONE" _NET_WM_STATE _NET_WM_STATE_STICKY
[ "$(window_desktop "$MOTIF_NONE")" = 4294967295 ] ||
    fail "STICKY no publico _NET_WM_DESKTOP=0xffffffff."
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool set_desktop 2
wait_for_desktop 2
wait_for_frame_state "$MOTIF_NONE" IsViewable
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool set_desktop 0
wait_for_desktop 0

ewmh_request state "$UTILITY" add SKIP_TASKBAR SKIP_PAGER
wait_for_property "$UTILITY" _NET_WM_STATE _NET_WM_STATE_SKIP_TASKBAR
wait_for_property "$UTILITY" _NET_WM_STATE _NET_WM_STATE_SKIP_PAGER
ewmh_request state "$UTILITY" add HIDDEN
wait_for_frame_state "$UTILITY" IsUnMapped
wait_for_property "$UTILITY" WM_STATE Iconic
ewmh_request state "$UTILITY" remove HIDDEN
wait_for_frame_state "$UTILITY" IsViewable
wait_for_property "$UTILITY" WM_STATE Normal

ewmh_request active "$PARENT" 2 0
sleep 0.1
ewmh_request active "$BELOW" 1 0
sleep 0.2
[ "$(active_window)" = "$PARENT" ] ||
    fail "Una _NET_ACTIVE_WINDOW de aplicacion robo el foco sin contexto."
state_has "$BELOW" _NET_WM_STATE_DEMANDS_ATTENTION ||
    fail "La activacion rechazada no marco DEMANDS_ATTENTION."
ewmh_request active "$BELOW" 2 0
sleep 0.2
[ "$(active_window)" = "$BELOW" ] ||
    fail "Una _NET_ACTIVE_WINDOW de pager no fue atendida."

ewmh_request restack "$PARENT" "$BELOW" 0
sleep 0.2
[ "$(stack_index "$PARENT")" -gt "$(stack_index "$BELOW")" ] ||
    fail "_NET_RESTACK_WINDOW no reordeno ventanas de la misma capa."
[ "$(stack_index "$ABOVE")" -gt "$(stack_index "$PARENT")" ] ||
    fail "_NET_RESTACK_WINDOW rompio la capa ABOVE."

ewmh_request active "$DIALOG" 2 0
DISPLAY="$NESTED_CLIENT_DISPLAY" xdotool key super+shift+r
sleep 0.8
kill -0 "$WM_PID" 2>/dev/null || fail "El reinicio termino AntonioWM."
wait_for_managed "$PARENT"
wait_for_managed "$DIALOG"
[ "$(window_desktop "$DIALOG")" = "$(window_desktop "$PARENT")" ] ||
    fail "El reinicio perdio la asociacion de escritorio transient."
state_has "$DIALOG" _NET_WM_STATE_MODAL ||
    fail "El reinicio perdio el estado modal."
[ "$(stack_index "$DIALOG")" -gt "$(stack_index "$PARENT")" ] ||
    fail "El reinicio perdio el stacking transient."

ewmh_request close "$FULLSCREEN"
wait_for_gone "$FULLSCREEN"

echo "Prueba Xephyr FASE 4 superada: transients, tipos/decoraciones, Motif hints, foco ICCCM, size hints, urgencia, estados, capas, struts y client messages EWMH."
