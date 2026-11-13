#!/bin/sh
# Manual hardware validation. No install and no changes to the normal session.
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SELF="$PROJECT/tests/physical_session.sh"
BASE="$PROJECT/tests/evidence/physical"
fail() { echo "$*" >&2; exit 1; }
load_run() {
    [ -f "$BASE/current" ] || fail 'No physical test has been prepared.'
    RUN=$(cat "$BASE/current")
    case "$RUN" in "$BASE"/run-*) ;; *) fail 'Invalid run directory.';; esac
    [ -d "$RUN" ] || fail 'Missing run directory.'
}
check_display() {
    [ "${DISPLAY:-}" = :1 ] || fail 'This command only operates on test DISPLAY=:1.'
    [ "${XAUTHORITY:-}" = "$RUN/Xauthority" ] || fail 'Use the test session authorization.'
    [ -r "$RUN/wm.pid" ] || fail 'No test WM PID recorded.'
    WM_PID=$(cat "$RUN/wm.pid")
    case "$WM_PID" in ''|*[!0-9]*) fail 'Invalid WM PID.';; esac
    [ "$(readlink "/proc/$WM_PID/exe" 2>/dev/null || true)" = "$PROJECT/antoniowm" ] ||
        fail 'The recorded PID is not the working-tree WM.'
    tr '\000' '\n' <"/proc/$WM_PID/environ" |
        grep -Fx "ANTONIOWM_PHYSICAL_RUN=$RUN" >/dev/null || fail 'WM run identity differs.'
}
checkpoint() {
    label=$1
    case "$label" in ''|*[!A-Za-z0-9_-]*) fail 'Use a short alphanumeric checkpoint label.';; esac
    check_display
    OUT="$RUN/$label"
    mkdir -p "$OUT"
    date -Is >"$OUT/time.txt"
    cat /sys/class/tty/tty0/active >"$OUT/active-vt.txt"
    xrandr --query >"$OUT/randr.txt"
    xrandr --listmonitors >"$OUT/monitors.txt"
    xprop -root _NET_SUPPORTING_WM_CHECK _NET_ACTIVE_WINDOW _NET_CLIENT_LIST \
        _NET_CLIENT_LIST_STACKING _NET_NUMBER_OF_DESKTOPS _NET_CURRENT_DESKTOP \
        _NET_DESKTOP_NAMES _NET_WORKAREA >"$OUT/ewmh.txt"
    xwininfo -root -tree >"$OUT/tree.txt"
    grep -E 'VmRSS|VmSize|Threads' "/proc/$WM_PID/status" >"$OUT/process-memory.txt"
    grep -E 'X11 diagnostic|error X11' "$RUN/wm.log" >"$OUT/xerrors.txt" || true
    xwd -root -silent -out "$OUT/root.xwd"
    convert "$OUT/root.xwd" "$OUT/root.png"
    rm "$OUT/root.xwd"
    printf '%s %s\n' "$(date -Is)" "$label" >>"$RUN/checkpoints.log"
    echo "Saved $OUT; X11 diagnostic lines: $(wc -l <"$OUT/xerrors.txt")"
}
case "${1:-}" in
start)
    [ "$(tty)" = /dev/tty3 ] || fail 'Log in locally on tty3 first (Ctrl+Alt+F3).'
    [ -z "${DISPLAY:-}" ] || fail 'Run from the text console, not an existing X session.'
    [ "$(cat /sys/class/tty/tty0/active)" = tty3 ] || fail 'tty3 must be active.'
    [ ! -e /tmp/.X1-lock ] && [ ! -S /tmp/.X11-unix/X1 ] || fail ':1 is occupied; do not remove its lock.'
    [ -x "$PROJECT/antoniowm" ] || fail 'Compile the working-tree binary first.'
    for tool in startx xrandr xprop xwininfo xwd convert xterm cc pkg-config; do
        command -v "$tool" >/dev/null || fail "Missing tool: $tool"
    done
    umask 077
    mkdir -p "$BASE"
    RUN=$(mktemp -d "$BASE/run-XXXXXXXX")
    printf '%s\n' "$RUN" >"$BASE/current"
    touch "$RUN/Xauthority"
    sha256sum "$PROJECT/antoniowm" >"$RUN/binary.sha256"
    git -C "$PROJECT" status --short >"$RUN/git-status.txt"
    git -C "$PROJECT" diff >"$RUN/source.diff"
    cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror \
        "$PROJECT/tests/tray_client.c" -o "$RUN/tray-client" -lX11
    cc -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Wpedantic -Werror \
        "$PROJECT/tests/physical_randr_watch.c" -o "$RUN/randr-watch" -lX11 -lXrandr
    cat >"$RUN/config" <<CONFIG
terminal=xterm
desktop.count=6
desktop.1.name=Trabajo
desktop.2.name=Web
desktop.6.name=Correo
taskbar.launcher_count=2
launcher.1.label=Terminal
launcher.1.command=xterm
launcher.2.label=Calculadora
launcher.2.command=xcalc
autostart.count=0
system.logout.command=
system.switch_user.command=
system.reboot.command=
system.poweroff.command=
CONFIG
    cat >"$RUN/instructions.txt" <<GUIDE
Sesión de pruebas :1 / VT3. Tu escritorio permanece en VT7 (Ctrl+Alt+F7).
Volver a esta prueba: Ctrl+Alt+F3.
Ejecutable: $PROJECT/antoniowm. No recompiles mientras lo pruebas.

Primero avisa en la conversación de que la sesión ha arrancado.
NO desconectes todavía ningún monitor.

Capturar estado: $SELF checkpoint inicial
Configurar dos salidas de esta sesión: $SELF layout
Cerrar SOLO esta sesión de prueba: $SELF stop
Si no responde: vuelve a tty3 tras terminar X, o a otra TTY; usa
  $SELF rescue-stop
El rescate comprueba PID, ejecutable e identidad de esta prueba antes de SIGTERM.
No uses killall, pkill, systemctl restart lightdm ni Ctrl+Alt+Backspace.
GUIDE
    export ANTONIOWM_PHYSICAL_RUN="$RUN"
    export XAUTHORITY="$RUN/Xauthority"
    export TMPDIR="$RUN"
    echo "Starting :1 on VT3. Normal session: Ctrl+Alt+F7. Logs: $RUN"
    set +e
    startx "$SELF" session -- :1 vt3 -keeptty -nolisten tcp >"$RUN/startx.log" 2>&1
    result=$?
    set -e
    echo "Test X session ended (status $result). Return to desktop: Ctrl+Alt+F7."
    [ "$result" -eq 0 ] || tail -n 60 "$RUN/startx.log"
    exit "$result"
    ;;
session)
    load_run
    [ "${ANTONIOWM_PHYSICAL_RUN:-}" = "$RUN" ] || fail 'Start with the start subcommand.'
    [ "${DISPLAY:-}" = :1 ] || fail 'Refusing to run the WM outside :1.'
    [ "${XAUTHORITY:-}" = "$RUN/Xauthority" ] || fail 'Incorrect test authorization.'
    [ "$(cat /sys/class/tty/tty0/active)" = tty3 ] || fail 'Test VT3 is not active.'
    cd "$PROJECT"
    CHILDREN=
    WM_PID=
    cleanup() {
        for pid in $CHILDREN; do kill "$pid" 2>/dev/null || true; done
        [ -z "$WM_PID" ] || kill "$WM_PID" 2>/dev/null || true
        for pid in $CHILDREN; do wait "$pid" 2>/dev/null || true; done
        [ -z "$WM_PID" ] || wait "$WM_PID" 2>/dev/null || true
    }
    trap cleanup EXIT INT TERM
    ANTONIOWM_CONFIG="$RUN/config" ANTONIOWM_LOG_XERRORS=1 \
        "$PROJECT/antoniowm" >"$RUN/wm.log" 2>&1 &
    WM_PID=$!
    printf '%s\n' "$WM_PID" >"$RUN/wm.pid"
    "$RUN/randr-watch" >"$RUN/randr-events.log" 2>&1 &
    CHILDREN="$CHILDREN $!"
    xterm -title 'AntonioWM prueba física - terminal' \
        -geometry 90x22+40+50 >>"$RUN/apps.log" 2>&1 &
    CHILDREN="$CHILDREN $!"
    xterm -title 'Guía de validación física - no desconectar todavía' \
        -geometry 95x27+650+80 -e sh -c 'cat "$1"; exec /bin/bash' sh \
        "$RUN/instructions.txt" >>"$RUN/apps.log" 2>&1 &
    CHILDREN="$CHILDREN $!"
    xcalc -geometry 240x360+420+100 >>"$RUN/apps.log" 2>&1 &
    CHILDREN="$CHILDREN $!"
    "$RUN/tray-client" icon "$RUN/tray.id" "$RUN/tray.log" 'Physical XEmbed' 22 \
        >>"$RUN/apps.log" 2>&1 &
    CHILDREN="$CHILDREN $!"
    set +e
    wait "$WM_PID"
    result=$?
    set -e
    WM_PID=
    exit "$result"
    ;;
checkpoint)
    load_run
    checkpoint "${2:-manual}"
    ;;
layout)
    load_run
    check_display
    [ "$(cat /sys/class/tty/tty0/active)" = tty3 ] || fail 'Activate test VT3 before changing its layout.'
    xrandr --query | grep '^HDMI-0 connected' >/dev/null || fail 'HDMI-0 is not connected.'
    xrandr --query | grep '^HDMI-1 connected' >/dev/null || fail 'HDMI-1 is not connected.'
    xrandr --output HDMI-0 --auto --primary --pos 0x0 --output HDMI-1 --auto --right-of HDMI-0
    checkpoint two-monitors
    ;;
stop|rescue-stop)
    load_run
    if [ "$1" = rescue-stop ]; then
        export DISPLAY=:1 XAUTHORITY="$RUN/Xauthority"
    fi
    check_display
    kill -TERM "$WM_PID"
    echo 'SIGTERM sent only to the recorded test WM. Return to the normal desktop with Ctrl+Alt+F7.'
    ;;
*)
    echo "Usage: $SELF start | checkpoint LABEL | layout | stop | rescue-stop" >&2
    exit 2
    ;;
esac
