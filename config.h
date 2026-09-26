#ifndef ANTONIOWM_CONFIG_H
#define ANTONIOWM_CONFIG_H

#include <X11/X.h>
#include <X11/keysym.h>

/*
 * AntonioWM configuration.
 * Rebuild with `make` after changing this file.
 */

#define AWM_NAME "AntonioWM"
#define AWM_VERSION "0.4.11-panel-menu"

/* Commands */
/* Command line; ANTONIOWM_TERMINAL can override it at runtime. */
#define AWM_TERMINAL "xterm"
#define AWM_SCREENSHOT_COMMAND \
    "maim | xclip -selection clipboard -t image/png -i"
#define AWM_SYSTEM_LOGOUT_COMMAND ""
#define AWM_SYSTEM_SWITCH_USER_COMMAND ""
#define AWM_SYSTEM_REBOOT_COMMAND "systemctl reboot"
#define AWM_SYSTEM_POWEROFF_COMMAND "systemctl poweroff"

/* Optional classic bottom bar and launcher defaults (icon empty = text). */
#define AWM_TASKBAR_ENABLED 1
#define AWM_TASKBAR_HEIGHT 28U
#define AWM_TASKBAR_GAP 4
#define AWM_TASKBAR_PADDING 4
#define AWM_TASKBAR_LAUNCHER_WIDTH 78
#define AWM_TASKBAR_WINDOW_MAX_WIDTH 200

#define AWM_LAUNCHER_CHROME_LABEL "Chrome"
#define AWM_LAUNCHER_CHROME_COMMAND "google-chrome"
#define AWM_LAUNCHER_CHROME_ICON ""
#define AWM_LAUNCHER_TERMINAL_LABEL "Terminal"
#define AWM_LAUNCHER_TERMINAL_COMMAND "xterm"
#define AWM_LAUNCHER_TERMINAL_ICON ""
#define AWM_LAUNCHER_FILES_LABEL "Archivos"
#define AWM_LAUNCHER_FILES_COMMAND "pcmanfm"
#define AWM_LAUNCHER_FILES_ICON ""
#define AWM_LAUNCHER_GIMP_LABEL "GIMP"
#define AWM_LAUNCHER_GIMP_COMMAND "gimp"
#define AWM_LAUNCHER_GIMP_ICON ""

#define AWM_TASKBAR_LAUNCHERS                                                \
    {                                                                       \
        { AWM_LAUNCHER_CHROME_LABEL, AWM_LAUNCHER_CHROME_COMMAND,           \
          AWM_LAUNCHER_CHROME_ICON },                                       \
        { AWM_LAUNCHER_TERMINAL_LABEL, AWM_LAUNCHER_TERMINAL_COMMAND,       \
          AWM_LAUNCHER_TERMINAL_ICON },                                     \
        { AWM_LAUNCHER_FILES_LABEL, AWM_LAUNCHER_FILES_COMMAND,             \
          AWM_LAUNCHER_FILES_ICON },                                        \
        { AWM_LAUNCHER_GIMP_LABEL, AWM_LAUNCHER_GIMP_COMMAND,               \
          AWM_LAUNCHER_GIMP_ICON }                                          \
    }

/* Compact, classic decorations.  All dimensions are in pixels. */
#define AWM_BORDER_WIDTH 2U
#define AWM_TITLE_HEIGHT 22U
#define AWM_BUTTON_SIZE 16U
#define AWM_ELEMENT_GAP 4
#define AWM_TITLE_TEXT_PADDING 8
#define AWM_BAR_PADDING 5

/* A small, deliberately muted palette. */
#define AWM_COLOR_BORDER_ACTIVE "#71889b"
#define AWM_COLOR_BORDER_INACTIVE "#30363b"
#define AWM_COLOR_TITLE_ACTIVE "#435767"
#define AWM_COLOR_TITLE_INACTIVE "#343a40"
#define AWM_COLOR_TITLE_TEXT_ACTIVE "#f2f4f5"
#define AWM_COLOR_TITLE_TEXT_INACTIVE "#b4bcc2"
#define AWM_COLOR_BUTTON "#4a535b"
#define AWM_COLOR_BUTTON_HOVER "#616d77"
#define AWM_COLOR_BUTTON_PRESSED "#2c3237"
#define AWM_COLOR_BUTTON_SYMBOL "#edf0f2"
#define AWM_COLOR_BUTTON_OUTLINE "#242a2f"
#define AWM_COLOR_BAR_BACKGROUND "#252a2f"
#define AWM_COLOR_BAR_FOREGROUND "#dce1e4"
#define AWM_COLOR_BAR_ITEM "#30363c"
#define AWM_COLOR_BAR_SELECTED "#536b7c"
#define AWM_COLOR_BAR_ACTIVE_WINDOW "#405666"
#define AWM_COLOR_BAR_MINIMIZED "#4b4239"
#define AWM_COLOR_BAR_URGENT "#7a4545"
#define AWM_COLOR_BAR_SEPARATOR "#41484e"

/* Xft uses an ordinary system sans-serif family and enables antialiasing. */
#define AWM_FONT_FAMILY "sans"
#define AWM_FONT_SIZE 10U
#define AWM_FONT_FALLBACK "fixed"
#define AWM_CLOCK_FORMAT "%H:%M"

/* Unified bottom panel and native menu dimensions. */
#define AWM_PANEL_MENU_WIDTH 92
#define AWM_PANEL_DESKTOP_WIDTH 26
#define AWM_PANEL_CLOCK_WIDTH 60
#define AWM_MENU_WIDTH 250
#define AWM_MENU_APPLICATION_WIDTH 310
#define AWM_MENU_ROW_HEIGHT 24
#define AWM_MENU_SEPARATOR_HEIGHT 8
#define AWM_MENU_BORDER_WIDTH 1
#define AWM_DESKTOP_COUNT 4U
#define AWM_DESKTOP_NAMES { "1", "2", "3", "4" }

/* Automatic placement for windows without an explicit useful position. */
#define AWM_CASCADE_MARGIN 24
#define AWM_CASCADE_STEP 32

/* Focus: 0 = click to focus, 1 = focus follows the pointer. */
#define AWM_FOCUS_FOLLOWS_MOUSE 0

/* Interactive resize safety limits. */
#define AWM_MIN_CLIENT_WIDTH 80
#define AWM_MIN_CLIENT_HEIGHT 50
#define AWM_MIN_VISIBLE_PIXELS 32
/* Invisible input zone; the visual border remains AWM_BORDER_WIDTH. */
#define AWM_RESIZE_GRAB_WIDTH 7U

/* Keyboard and mouse modifiers. */
#define AWM_ALT_MOD Mod1Mask
#define AWM_SUPER_MOD Mod4Mask

/* Keyboard bindings. Super + 1..9 uses the configured desktop count. */
#define AWM_CYCLE_KEY XK_Tab
#define AWM_CLOSE_KEY XK_F4
#define AWM_TERMINAL_KEY XK_Return
#define AWM_MAXIMIZE_KEY XK_m
#define AWM_FULLSCREEN_KEY XK_f
#define AWM_RESTART_KEY XK_r
#define AWM_RESTART_MOD (AWM_SUPER_MOD | ShiftMask)
#define AWM_SCREENSHOT_KEY XK_Print
#define AWM_SCREENSHOT_MOD (ControlMask | ShiftMask)
#define AWM_DESKTOP_MOD AWM_SUPER_MOD
#define AWM_SEND_DESKTOP_MOD (AWM_SUPER_MOD | ShiftMask)

#endif
