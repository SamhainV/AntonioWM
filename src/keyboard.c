#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void update_numlock_mask(void)
{
    XModifierKeymap *mapping;
    KeyCode numlock;
    int modifier;
    int key;

    wm.numlock_mask = 0U;
    mapping = XGetModifierMapping(wm.display);
    if (mapping == NULL) {
        return;
    }

    numlock = XKeysymToKeycode(wm.display, XK_Num_Lock);
    if (numlock == 0U) {
        XFreeModifiermap(mapping);
        return;
    }

    for (modifier = 0; modifier < 8; ++modifier) {
        for (key = 0; key < mapping->max_keypermod; ++key) {
            int offset = modifier * mapping->max_keypermod + key;

            if (mapping->modifiermap[offset] == numlock) {
                wm.numlock_mask = 1U << (unsigned int)modifier;
            }
        }
    }

    XFreeModifiermap(mapping);
}

static void grab_key(KeySym symbol, unsigned int modifiers)
{
    static const unsigned int lock_variants[] = {
        0U,
        LockMask
    };
    KeyCode keycode = XKeysymToKeycode(wm.display, symbol);
    size_t lock_index;
    size_t numlock_index;

    if (keycode == 0U) {
        fprintf(stderr, "%s: no existe keycode para keysym 0x%lx\n", AWM_NAME, symbol);
        return;
    }

    for (lock_index = 0U;
         lock_index < sizeof(lock_variants) / sizeof(lock_variants[0]);
         ++lock_index) {
        for (numlock_index = 0U; numlock_index < 2U; ++numlock_index) {
            unsigned int extra = lock_variants[lock_index];

            if (numlock_index != 0U) {
                extra |= wm.numlock_mask;
            }

            XGrabKey(wm.display,
                     (int)keycode,
                     modifiers | extra,
                     wm.root,
                     False,
                     GrabModeAsync,
                     GrabModeAsync);
        }
    }
}

static void spawn_terminal(void)
{
    const char *command = getenv("ANTONIOWM_TERMINAL");
    pid_t child = fork();

    if (command == NULL || command[0] == '\0') {
        command = AWM_TERMINAL;
    }

    if (child < 0) {
        perror(AWM_NAME ": fork");
        return;
    }

    if (child == 0) {
        if (wm.display != NULL) {
            close(ConnectionNumber(wm.display));
        }
        (void)setsid();
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror(AWM_NAME ": no se pudo abrir el terminal");
        _exit(127);
    }
}

unsigned int keyboard_clean_modifiers(unsigned int state)
{
    return state & ~(LockMask | wm.numlock_mask);
}

void keyboard_grab_keys(void)
{
    unsigned int desktop;
    unsigned int keyboard_desktops;

    update_numlock_mask();
    XUngrabKey(wm.display, AnyKey, AnyModifier, wm.root);

    grab_key(AWM_CYCLE_KEY, AWM_ALT_MOD);
    grab_key(AWM_CYCLE_KEY, AWM_ALT_MOD | ShiftMask);
    grab_key(AWM_CLOSE_KEY, AWM_ALT_MOD);
    grab_key(AWM_TERMINAL_KEY, AWM_SUPER_MOD);
    grab_key(AWM_MAXIMIZE_KEY, AWM_SUPER_MOD);
    grab_key(AWM_FULLSCREEN_KEY, AWM_SUPER_MOD);
    grab_key(AWM_RESTART_KEY, AWM_RESTART_MOD);

    keyboard_desktops = wm.desktop_count < 9U ? wm.desktop_count : 9U;
    for (desktop = 0U; desktop < keyboard_desktops; ++desktop) {
        KeySym symbol = XK_1 + (KeySym)desktop;

        grab_key(symbol, AWM_DESKTOP_MOD);
        grab_key(symbol, AWM_SEND_DESKTOP_MOD);
    }
}

void keyboard_ungrab_keys(void)
{
    XUngrabKey(wm.display, AnyKey, AnyModifier, wm.root);
}

void keyboard_handle_keypress(XKeyEvent *event)
{
    KeySym symbol = XLookupKeysym(event, 0);
    unsigned int modifiers = keyboard_clean_modifiers(event->state);
    unsigned int desktop;
    unsigned int keyboard_desktops;

    wm.last_event_time = event->time;

    keyboard_desktops = wm.desktop_count < 9U ? wm.desktop_count : 9U;
    for (desktop = 0U; desktop < keyboard_desktops; ++desktop) {
        if (symbol == XK_1 + (KeySym)desktop && modifiers == AWM_DESKTOP_MOD) {
            desktop_switch(desktop);
            return;
        }
        if (symbol == XK_1 + (KeySym)desktop && modifiers == AWM_SEND_DESKTOP_MOD) {
            desktop_move_client(wm.focused, desktop);
            return;
        }
    }

    if ((symbol == AWM_CYCLE_KEY || symbol == XK_ISO_Left_Tab) &&
        modifiers == AWM_ALT_MOD) {
        client_cycle_focus(1);
    } else if ((symbol == AWM_CYCLE_KEY || symbol == XK_ISO_Left_Tab) &&
               modifiers == (AWM_ALT_MOD | ShiftMask)) {
        client_cycle_focus(-1);
    } else if (symbol == AWM_CLOSE_KEY && modifiers == AWM_ALT_MOD) {
        client_close(wm.focused);
    } else if (symbol == AWM_TERMINAL_KEY && modifiers == AWM_SUPER_MOD) {
        spawn_terminal();
    } else if (symbol == AWM_MAXIMIZE_KEY && modifiers == AWM_SUPER_MOD) {
        client_toggle_maximized(wm.focused);
    } else if (symbol == AWM_FULLSCREEN_KEY && modifiers == AWM_SUPER_MOD) {
        client_toggle_fullscreen(wm.focused);
    } else if (symbol == AWM_RESTART_KEY && modifiers == AWM_RESTART_MOD) {
        wm_restart();
    }
}

void keyboard_refresh_mapping(XMappingEvent *event)
{
    XRefreshKeyboardMapping(event);
    keyboard_grab_keys();
    mouse_regrab_all();
}
