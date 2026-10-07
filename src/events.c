#include "wm.h"
#include "config.h"

static Bool destroy_for_client(Display *display, XEvent *queued, XPointer data)
{
    Window window = *(const Window *)data;

    (void)display;
    return queued->type == DestroyNotify &&
           queued->xdestroywindow.window == window;
}

static void handle_map_request(const XMapRequestEvent *event)
{
    Client *client = client_find(event->window);

    if (client != NULL) {
        client_show(client);
    } else {
        client_manage(event->window, true);
    }
}

static void handle_unmap_notify(const XUnmapEvent *event)
{
    Client *client = client_find(event->window);

    if (client != NULL) {
        XEvent destroyed_event;

        if (client->ignore_unmap > 0U) {
            --client->ignore_unmap;
            return;
        }
        /* A killed application's UnmapNotify precedes DestroyNotify.  If
         * the latter is already queued, the client XID no longer exists. */
        XSync(wm.display, False);
        if (XCheckIfEvent(wm.display,
                          &destroyed_event,
                          destroy_for_client,
                          (XPointer)&client->window)) {
            client_unmanage(client, true);
            return;
        }
        client_unmanage(client, false);
        return;
    }

    client = client_find_any(event->window);
    if (client != NULL && event->window == client->frame) {
        client->visible = false;
    }
}

static void handle_destroy_notify(const XDestroyWindowEvent *event)
{
    Client *client = client_find(event->window);

    if (client != NULL) {
        client_unmanage(client, true);
    }
}

static void handle_enter_notify(const XCrossingEvent *event)
{
    Client *client;

    wm.last_event_time = event->time;
    (void)taskbar_handle_enter(event);
    decor_handle_enter(event);
    if (!wm.config.focus_follows_mouse || event->mode != NotifyNormal ||
        event->detail == NotifyInferior) {
        return;
    }

    client = client_find_any(event->window);
    if (client != NULL && client->visible) {
        client_focus(client, false);
    }
}

static void handle_leave_notify(const XCrossingEvent *event)
{
    wm.last_event_time = event->time;
    (void)taskbar_handle_leave(event);
    decor_handle_leave(event);
}

static void handle_normal_hints(Client *client)
{
    int width;
    int height;

    if (client->maximized) {
        width = client->old_width;
        height = client->old_height;
        client_apply_size_hints(client, &width, &height);
        client->old_width = width;
        client->old_height = height;
        return;
    }
    if (client->fullscreen) {
        width = client->fullscreen_width;
        height = client->fullscreen_height;
        client_apply_size_hints(client, &width, &height);
        client->fullscreen_width = width;
        client->fullscreen_height = height;
        return;
    }

    width = client->width;
    height = client->height;
    client_apply_size_hints(client, &width, &height);
    if (width != client->width || height != client->height) {
        client->width = width;
        client->height = height;
        decor_apply_geometry(client);
    }
}

static void handle_property_notify(const XPropertyEvent *event)
{
    Client *client = client_find(event->window);

    wm.last_event_time = event->time;
    if (client != NULL &&
        (event->atom == XA_WM_NAME || event->atom == wm.atoms.net_wm_name)) {
        client_update_title(client);
    } else if (client != NULL && event->atom == XA_WM_HINTS) {
        hints_refresh_focus_model(client);
        hints_refresh_urgency(client);
        client_reconsider_focus(client);
        taskbar_update();
    } else if (client != NULL && event->atom == wm.atoms.wm_protocols) {
        hints_refresh_focus_model(client);
        client_reconsider_focus(client);
    } else if (client != NULL && event->atom == XA_WM_NORMAL_HINTS) {
        handle_normal_hints(client);
    } else if (client != NULL &&
               (event->atom == XA_WM_TRANSIENT_FOR ||
                event->atom == wm.atoms.net_wm_window_type ||
                event->atom == wm.atoms.motif_wm_hints)) {
        Client *parent;

        client_refresh_decor(client);
        parent = client_find(client->transient_for);
        if (parent != NULL && !client->sticky &&
            client->desktop != parent->desktop) {
            desktop_move_client(client, parent->desktop);
        }
    } else if (client != NULL &&
               (event->atom == wm.atoms.net_wm_strut ||
                event->atom == wm.atoms.net_wm_strut_partial)) {
        ewmh_refresh_strut(client);
        ewmh_recalculate_workarea();
        stacking_enforce();
    }
}

static void handle_focus_in(const XFocusChangeEvent *event)
{
    Client *client;
    Window actual_focus;
    int revert_to;

    if (event->mode != NotifyNormal && event->mode != NotifyWhileGrabbed) {
        return;
    }
    if (event->detail == NotifyInferior || event->detail == NotifyPointer) {
        return;
    }

    client = client_find(event->window);
    if (client == NULL) {
        return;
    }

    XGetInputFocus(wm.display, &actual_focus, &revert_to);
    (void)revert_to;
    if (actual_focus == event->window) {
        client_note_focus(client);
    }
}

static void refresh_screen_layout(bool refresh_monitors)
{
    XWindowAttributes attributes;
    Client *client;

    task_preview_cancel();
    if (XGetWindowAttributes(wm.display, wm.root, &attributes) != 0) {
        wm.screen_width = attributes.width;
        wm.screen_height = attributes.height;
    } else {
        wm.screen_width = DisplayWidth(wm.display, wm.screen);
        wm.screen_height = DisplayHeight(wm.display, wm.screen);
    }
    if (refresh_monitors) {
        monitor_refresh();
    }
    for (client = wm.clients; client != NULL; client = client->next) {
        ewmh_refresh_strut(client);
    }
    ewmh_recalculate_workarea();
    stacking_enforce();
    taskbar_update();
    menu_reconfigure();
}

static void handle_configure_notify(const XConfigureEvent *event)
{
    if (event->window != wm.root) {
        return;
    }

    wm.screen_width = event->width;
    wm.screen_height = event->height;
    refresh_screen_layout(true);
}

static void handle_client_message(const XClientMessageEvent *event)
{
    Client *client;

    if (ewmh_handle_client_message(event)) {
        return;
    }

    if (event->message_type != wm.atoms.wm_change_state || event->format != 32 ||
        event->data.l[0] != IconicState) {
        return;
    }

    client = client_find(event->window);
    if (client != NULL) {
        client_minimize(client);
    }
}

static void handle_map_notify(const XMapEvent *event)
{
    Client *client = client_find(event->window);

    if (client != NULL) {
        client->mapped = true;
        return;
    }

    client = client_find_any(event->window);
    if (client != NULL && event->window == client->frame) {
        client->visible = desktop_is_current(client) && !client->minimized;
    }
}

void events_dispatch(XEvent *event)
{
    if (monitor_handle_event(event)) {
        refresh_screen_layout(true);
        return;
    }
    if (tray_handle_event(event)) {
        return;
    }

    switch (event->type) {
    case MapRequest:
        handle_map_request(&event->xmaprequest);
        break;
    case ConfigureRequest:
        client_configure_request(&event->xconfigurerequest);
        break;
    case DestroyNotify:
        handle_destroy_notify(&event->xdestroywindow);
        break;
    case UnmapNotify:
        handle_unmap_notify(&event->xunmap);
        break;
    case ButtonPress:
        task_preview_cancel();
        if (!menu_handle_button_press(&event->xbutton) &&
            !taskbar_handle_button_press(&event->xbutton)) {
            mouse_handle_button_press(&event->xbutton);
        }
        break;
    case ButtonRelease:
        if (!menu_handle_button_release(&event->xbutton) &&
            !taskbar_handle_button_release(&event->xbutton)) {
            mouse_handle_button_release(&event->xbutton);
        }
        break;
    case MotionNotify:
        if (!menu_handle_motion(&event->xmotion) &&
            !taskbar_handle_motion(&event->xmotion)) {
            mouse_handle_motion(&event->xmotion);
        }
        break;
    case KeyPress:
        if (!menu_handle_keypress(&event->xkey)) {
            keyboard_handle_keypress(&event->xkey);
        }
        break;
    case EnterNotify:
        handle_enter_notify(&event->xcrossing);
        break;
    case LeaveNotify:
        handle_leave_notify(&event->xcrossing);
        break;
    case PropertyNotify:
        handle_property_notify(&event->xproperty);
        break;
    case ClientMessage:
        handle_client_message(&event->xclient);
        break;
    case ConfigureNotify:
        handle_configure_notify(&event->xconfigure);
        break;
    case FocusIn:
        handle_focus_in(&event->xfocus);
        break;
    case Expose:
        task_preview_handle_expose(&event->xexpose);
        menu_handle_expose(&event->xexpose);
        taskbar_handle_expose(&event->xexpose);
        decor_handle_expose(&event->xexpose);
        break;
    case MappingNotify:
        keyboard_refresh_mapping(&event->xmapping);
        break;
    case MapNotify:
        handle_map_notify(&event->xmap);
        break;
    default:
        break;
    }
}
