#include "wm.h"
#include "config.h"

#include <X11/X.h>

static Client *client_from_button_event(const XButtonEvent *event)
{
    Client *client = client_find_any(event->window);

    if (client == NULL && event->subwindow != None) {
        client = client_find_any(event->subwindow);
    }
    return client;
}

static void grab_modified_button(Client *client,
                                 unsigned int button,
                                 Cursor cursor,
                                 unsigned int extra_modifiers)
{
    XGrabButton(wm.display,
                button,
                AWM_ALT_MOD | extra_modifiers,
                client->window,
                False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                cursor);
}

void mouse_grab_buttons(Client *client, bool focused)
{
    unsigned int variants[4];
    size_t index;

    variants[0] = 0U;
    variants[1] = LockMask;
    variants[2] = wm.numlock_mask;
    variants[3] = LockMask | wm.numlock_mask;

    XUngrabButton(wm.display, AnyButton, AnyModifier, client->window);

    if (!focused) {
        XGrabButton(wm.display,
                    AnyButton,
                    AnyModifier,
                    client->window,
                    False,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeSync,
                    GrabModeAsync,
                    None,
                    None);
        return;
    }

    for (index = 0U; index < sizeof(variants) / sizeof(variants[0]); ++index) {
        bool duplicate = false;
        size_t previous;

        for (previous = 0U; previous < index; ++previous) {
            if (variants[previous] == variants[index]) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        grab_modified_button(client, Button1, wm.move_cursor, variants[index]);
        grab_modified_button(client, Button3, wm.resize_cursor, variants[index]);
    }
}

void mouse_regrab_all(void)
{
    Client *client;

    for (client = wm.clients; client != NULL; client = client->next) {
        mouse_grab_buttons(client, client == wm.focused);
    }
}

static void begin_drag(Client *client,
                       DragMode mode,
                       const XButtonEvent *event,
                       bool request_pointer_grab)
{
    int result;

    if (client->fullscreen) {
        return;
    }
    if (client->maximized) {
        client_set_maximized(client, false);
    }

    wm.drag.mode = mode;
    wm.drag.client = client;
    wm.drag.pointer_x = event->x_root;
    wm.drag.pointer_y = event->y_root;
    wm.drag.window_x = client->x;
    wm.drag.window_y = client->y;
    wm.drag.window_width = client->width;
    wm.drag.window_height = client->height;
    wm.drag.resize_left = event->x_root < client->x + decor_outer_width(client) / 2;
    wm.drag.resize_top = event->y_root < client->y + decor_outer_height(client) / 2;
    wm.drag.resize_horizontal = true;
    wm.drag.resize_vertical = true;
    wm.drag.pointer_grabbed = false;

    if (!request_pointer_grab) {
        return;
    }

    result = XGrabPointer(wm.display,
                          wm.root,
                          False,
                          ButtonReleaseMask | PointerMotionMask,
                          GrabModeAsync,
                          GrabModeAsync,
                          None,
                          mode == DRAG_MOVE ? wm.move_cursor : wm.resize_cursor,
                          event->time);
    wm.drag.pointer_grabbed = result == GrabSuccess;
}

void mouse_handle_button_press(XButtonEvent *event)
{
    Client *client = client_from_button_event(event);
    ClientPart part;
    unsigned int modifiers = keyboard_clean_modifiers(event->state);
    bool modified_drag;

    wm.last_event_time = event->time;
    if (client == NULL) {
        XAllowEvents(wm.display, ReplayPointer, event->time);
        return;
    }

    part = client_window_part(client, event->window);
    if (part == CLIENT_PART_NONE && event->subwindow != None) {
        part = client_window_part(client, event->subwindow);
    }

    modified_drag = modifiers == AWM_ALT_MOD &&
                    (event->button == Button1 || event->button == Button3);
    client_focus(client, true);

    if (part == CLIENT_PART_MINIMIZE || part == CLIENT_PART_MAXIMIZE ||
        part == CLIENT_PART_CLOSE) {
        if (event->button == Button1) {
            if (part == CLIENT_PART_MINIMIZE) {
                client_minimize(client);
            } else if (part == CLIENT_PART_MAXIMIZE) {
                client_toggle_maximized(client);
            } else {
                client_close(client);
            }
        }
        return;
    }

    if (modified_drag) {
        XAllowEvents(wm.display, AsyncPointer, event->time);
        begin_drag(client,
                   event->button == Button1 ? DRAG_MOVE : DRAG_RESIZE,
                   event,
                   part != CLIENT_PART_WINDOW);
        return;
    }

    if (part == CLIENT_PART_TITLE && event->button == Button1) {
        begin_drag(client, DRAG_MOVE, event, true);
        return;
    }
    if (part == CLIENT_PART_FRAME && event->button == Button1) {
        begin_drag(client, DRAG_RESIZE, event, true);
        return;
    }

    if (part == CLIENT_PART_WINDOW) {
        XAllowEvents(wm.display, ReplayPointer, event->time);
    }
}

void mouse_handle_button_release(XButtonEvent *event)
{
    wm.last_event_time = event->time;
    if (wm.drag.pointer_grabbed) {
        XUngrabPointer(wm.display, event->time);
    }
    wm.drag.mode = DRAG_NONE;
    wm.drag.client = NULL;
    wm.drag.pointer_grabbed = false;
}

static int constrained_x(const Client *client, int x, int width)
{
    WorkArea area;

    ewmh_get_workarea(client->sticky ? wm.current_desktop : client->desktop,
                      &area);
    if (x > area.x + area.width - AWM_MIN_VISIBLE_PIXELS) {
        x = area.x + area.width - AWM_MIN_VISIBLE_PIXELS;
    }
    if (x + width < area.x + AWM_MIN_VISIBLE_PIXELS) {
        x = area.x + AWM_MIN_VISIBLE_PIXELS - width;
    }
    return x;
}

static int constrained_y(const Client *client, int y, int height)
{
    WorkArea area;

    ewmh_get_workarea(client->sticky ? wm.current_desktop : client->desktop,
                      &area);
    if (y > area.y + area.height - AWM_MIN_VISIBLE_PIXELS) {
        y = area.y + area.height - AWM_MIN_VISIBLE_PIXELS;
    }
    if (y + height < area.y + AWM_MIN_VISIBLE_PIXELS) {
        y = area.y + AWM_MIN_VISIBLE_PIXELS - height;
    }
    return y;
}

void mouse_handle_motion(XMotionEvent *event)
{
    XMotionEvent current = *event;
    XEvent pending;
    Client *client = wm.drag.client;
    int delta_x;
    int delta_y;

    if (wm.drag.mode == DRAG_NONE || client == NULL) {
        return;
    }

    while (XCheckTypedEvent(wm.display, MotionNotify, &pending) != 0) {
        current = pending.xmotion;
    }

    wm.last_event_time = current.time;
    delta_x = current.x_root - wm.drag.pointer_x;
    delta_y = current.y_root - wm.drag.pointer_y;

    if (wm.drag.mode == DRAG_MOVE) {
        int x = constrained_x(client,
                              wm.drag.window_x + delta_x,
                              decor_outer_width(client));
        int y = constrained_y(client,
                              wm.drag.window_y + delta_y,
                              decor_outer_height(client));

        decor_move(client, x, y);
        return;
    }

    if (wm.drag.mode == DRAG_RESIZE) {
        int width = !wm.drag.resize_horizontal
                        ? wm.drag.window_width
                        : (wm.drag.resize_left
                               ? wm.drag.window_width - delta_x
                               : wm.drag.window_width + delta_x);
        int height = !wm.drag.resize_vertical
                         ? wm.drag.window_height
                         : (wm.drag.resize_top
                                ? wm.drag.window_height - delta_y
                                : wm.drag.window_height + delta_y);
        int x;
        int y;

        client_apply_size_hints(client, &width, &height);
        x = wm.drag.resize_left
                ? wm.drag.window_x + wm.drag.window_width - width
                : wm.drag.window_x;
        y = wm.drag.resize_top
                ? wm.drag.window_y + wm.drag.window_height - height
                : wm.drag.window_y;

        client->x = x;
        client->y = y;
        client->width = width;
        client->height = height;
        decor_apply_geometry(client);
    }
}

void mouse_begin_moveresize(Client *client,
                           int root_x,
                           int root_y,
                           int direction,
                           Time time)
{
    XButtonEvent event;

    if (client == NULL) {
        return;
    }
    if (direction == 11) {
        mouse_cancel_drag(client);
        return;
    }
    if (direction < 0 || direction > 10) {
        return;
    }

    event.type = ButtonPress;
    event.serial = 0UL;
    event.send_event = True;
    event.display = wm.display;
    event.window = wm.root;
    event.root = wm.root;
    event.subwindow = client->frame;
    event.time = time;
    event.x = root_x;
    event.y = root_y;
    event.x_root = root_x;
    event.y_root = root_y;
    event.state = 0U;
    event.button = direction == 8 || direction == 10 ? Button1 : Button3;
    event.same_screen = True;

    begin_drag(client,
               direction == 8 || direction == 10 ? DRAG_MOVE : DRAG_RESIZE,
               &event,
               true);
    if (wm.drag.client != client || direction == 8 || direction == 10) {
        return;
    }

    wm.drag.resize_left = direction == 0 || direction == 6 || direction == 7;
    wm.drag.resize_top = direction == 0 || direction == 1 || direction == 2;
    wm.drag.resize_horizontal = direction == 9 ||
                                (direction != 1 && direction != 5);
    wm.drag.resize_vertical = direction == 9 ||
                              (direction != 3 && direction != 7);
}

void mouse_cancel_drag(Client *client)
{
    if (wm.drag.client == client) {
        if (wm.drag.pointer_grabbed) {
            XUngrabPointer(wm.display, CurrentTime);
        }
        wm.drag.mode = DRAG_NONE;
        wm.drag.client = NULL;
        wm.drag.pointer_grabbed = false;
    }
}
