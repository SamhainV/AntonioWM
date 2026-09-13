#ifndef ANTONIOWM_CONFIG_H
#define ANTONIOWM_CONFIG_H

#include <X11/X.h>
#include <X11/keysym.h>

/*
 * AntonioWM configuration.
 * Rebuild with `make` after changing this file.
 */

#define AWM_NAME "AntonioWM"
#define AWM_VERSION "0.4.0-phase4"

/* Commands */
/* Command line; ANTONIOWM_TERMINAL can override it at runtime. */
#define AWM_TERMINAL "xterm"

/* Lightweight classic decorations. */
#define AWM_BORDER_WIDTH 2U
#define AWM_TITLE_HEIGHT 24U
#define AWM_BAR_HEIGHT 28U
#define AWM_COLOR_FOCUSED "#d7b04c"
#define AWM_COLOR_UNFOCUSED "#3a3a3a"
#define AWM_COLOR_TITLE_ACTIVE "#40566f"
#define AWM_COLOR_TITLE_INACTIVE "#45484c"
#define AWM_COLOR_TITLE_TEXT_ACTIVE "#ffffff"
#define AWM_COLOR_TITLE_TEXT_INACTIVE "#d2d4d7"
#define AWM_COLOR_BUTTON "#697582"
#define AWM_COLOR_BUTTON_SYMBOL "#ffffff"
#define AWM_COLOR_BAR_BACKGROUND "#20252b"
#define AWM_COLOR_BAR_FOREGROUND "#e8e8e8"
#define AWM_COLOR_BAR_SELECTED "#405f7d"
#define AWM_COLOR_BAR_ACTIVE_WINDOW "#526a81"
#define AWM_COLOR_BAR_MINIMIZED "#806448"
#define AWM_COLOR_BAR_URGENT "#9a3f3f"
#define AWM_FONT "fixed"
#define AWM_CLOCK_FORMAT "%H:%M"

/* Fixed-width sections keep the bar predictable and easy to click. */
#define AWM_BAR_BRAND_WIDTH 104
#define AWM_BAR_DESKTOP_WIDTH 30
#define AWM_BAR_ACTIVE_TITLE_WIDTH 220
#define AWM_BAR_CLOCK_WIDTH 64
#define AWM_BAR_ITEM_GAP 2
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
#define AWM_DESKTOP_MOD AWM_SUPER_MOD
#define AWM_SEND_DESKTOP_MOD (AWM_SUPER_MOD | ShiftMask)

#endif
