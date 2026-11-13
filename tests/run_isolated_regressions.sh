#!/bin/sh
set -eu
# Host DISPLAY is only used by Xephyr. Each WM runs in a nested server.
cd "$(dirname "$0")/.."
TEST_TMP=$(mktemp -d)
trap 'rm -rf "$TEST_TMP"' EXIT INT TERM
: >"$TEST_TMP/config"
export ANTONIOWM_CONFIG="$TEST_TMP/config"
export ANTONIOWM_LOG_XERRORS=1 ANTONIOWM_KEEP_TEST_LOGS=1
TEST_SCREEN_BASE=${TEST_SCREEN_BASE:-110}
export ANTONIOWM_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 1))
export ANTONIOWM_PHASE4_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 2))
export ANTONIOWM_TASKBAR_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 3))
export ANTONIOWM_MULTIMONITOR_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 4))
export ANTONIOWM_SCREENSHOT_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 5))
export ANTONIOWM_PANEL_MENU_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 6))
export ANTONIOWM_TRAY_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 7))
export ANTONIOWM_AUTOSTART_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 8))
export ANTONIOWM_PREVIEW_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 9))
export ANTONIOWM_AUDIT_TEST_DISPLAY=:$((TEST_SCREEN_BASE + 10))
# Refuse to attach a test to a pre-existing nested server.
for index in 1 2 3 4 5 6 7 8 9 10; do
    number=$((TEST_SCREEN_BASE + index))
    if DISPLAY=:$number xdpyinfo >/dev/null 2>&1; then
        echo "Test display :$number already in use" >&2; exit 1
    fi
done
# Keep an immutable private checkout for the run: hot restart executes argv[0].
# A concurrent make check in the working tree must not remove that executable.
RUN_DIR="$TEST_TMP/repo"
mkdir -p "$RUN_DIR"
cp -R Makefile config.h include src tests "$RUN_DIR/"
cp "${WM_BINARY:-./antoniowm}" "$RUN_DIR/antoniowm"
mkdir -p "$RUN_DIR/build"
# Tests are built in this private tree; preserve the selected WM binary.
cd "$RUN_DIR"
make test-client
for test in test_desktop_entries.sh smoke_xephyr.sh smoke_phase4_xephyr.sh \
    smoke_taskbar_xephyr.sh smoke_multimonitor_resize_xephyr.sh \
    smoke_screenshot_xephyr.sh smoke_panel_menu_xephyr.sh smoke_systray_xephyr.sh \
    smoke_autostart_xephyr.sh smoke_task_preview_xephyr.sh audit_stress_xephyr.sh; do
    echo "RUN $test"
    "./tests/$test"
done
