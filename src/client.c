#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *duplicate_string(const char *text)
{
    size_t length = strlen(text) + 1U;
    char *copy = malloc(length);

    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

static void append_client(Client *client)
{
    client->previous = wm.clients_tail;
    client->next = NULL;

    if (wm.clients_tail != NULL) {
        wm.clients_tail->next = client;
    } else {
        wm.clients = client;
    }
    wm.clients_tail = client;
}

static void detach_client(Client *client)
{
    if (client->previous != NULL) {
        client->previous->next = client->next;
    } else {
        wm.clients = client->next;
    }

    if (client->next != NULL) {
        client->next->previous = client->previous;
    } else {
        wm.clients_tail = client->previous;
    }
}

static bool supports_protocol(const Client *client, Atom protocol)
{
    Atom *protocols = NULL;
    int protocol_count = 0;
    int index;
    bool supported = false;

    if (XGetWMProtocols(wm.display,
                        client->window,
                        &protocols,
                        &protocol_count) == 0) {
        return false;
    }

    for (index = 0; index < protocol_count; ++index) {
        if (protocols[index] == protocol) {
            supported = true;
            break;
        }
    }

    if (protocols != NULL) {
        XFree(protocols);
    }
    return supported;
}

static void send_protocol(Client *client, Atom protocol)
{
    XEvent message;

    memset(&message, 0, sizeof(message));
    message.xclient.type = ClientMessage;
    message.xclient.window = client->window;
    message.xclient.message_type = wm.atoms.wm_protocols;
    message.xclient.format = 32;
    message.xclient.data.l[0] = (long)protocol;
    message.xclient.data.l[1] = (long)wm.last_event_time;

    XSendEvent(wm.display, client->window, False, NoEventMask, &message);
}

static void select_client_events(Client *client)
{
    const long events = EnterWindowMask |
                        FocusChangeMask |
                        PropertyChangeMask;

    XSelectInput(wm.display, client->window, events);
}

bool client_is_focus_candidate(const Client *client)
{
    if (client == NULL || (!client->accepts_input && !client->takes_focus)) {
        return false;
    }

    return client->window_type == CLIENT_TYPE_NORMAL ||
           client->window_type == CLIENT_TYPE_DIALOG ||
           client->window_type == CLIENT_TYPE_UTILITY ||
           client->window_type == CLIENT_TYPE_TOOLBAR;
}

bool client_is_taskbar_visible(const Client *client)
{
    if (client == NULL || client->skip_taskbar) {
        return false;
    }
    return client->window_type == CLIENT_TYPE_NORMAL ||
           client->window_type == CLIENT_TYPE_DIALOG ||
           client->window_type == CLIENT_TYPE_UTILITY;
}

static bool can_receive_focus(const Client *client)
{
    return client != NULL && client->mapped && client->visible &&
           !client->minimized && desktop_is_current(client) &&
           client_is_focus_candidate(client);
}

static bool should_focus_new_client(const Client *client)
{
    Client *parent;

    if (!can_receive_focus(client)) {
        return false;
    }
    parent = client_find(client->transient_for);
    if (parent != NULL) {
        return client->modal || wm.focused == parent || wm.focused == NULL;
    }
    return client->window_type == CLIENT_TYPE_NORMAL ||
           client->window_type == CLIENT_TYPE_DIALOG || client->modal ||
           wm.focused == NULL;
}

static size_t client_count(void)
{
    Client *client;
    size_t count = 0U;

    for (client = wm.clients; client != NULL; client = client->next) {
        ++count;
    }
    return count;
}

static Client *next_mapped_client(Client *from, int direction)
{
    Client *candidate = from;
    size_t remaining = client_count();

    while (remaining > 0U) {
        if (candidate == NULL) {
            candidate = direction >= 0 ? wm.clients : wm.clients_tail;
        } else {
            candidate = direction >= 0 ? candidate->next : candidate->previous;
            if (candidate == NULL) {
                candidate = direction >= 0 ? wm.clients : wm.clients_tail;
            }
        }

        if (can_receive_focus(candidate)) {
            return candidate;
        }
        --remaining;
    }
    return NULL;
}

ClientPart client_window_part(const Client *client, Window window)
{
    if (client == NULL || window == None) {
        return CLIENT_PART_NONE;
    }
    if (window == client->window) {
        return CLIENT_PART_WINDOW;
    }
    if (window == client->frame) {
        return CLIENT_PART_FRAME;
    }
    if (window == client->titlebar) {
        return CLIENT_PART_TITLE;
    }
    if (window == client->button_minimize) {
        return CLIENT_PART_MINIMIZE;
    }
    if (window == client->button_maximize) {
        return CLIENT_PART_MAXIMIZE;
    }
    if (window == client->button_close) {
        return CLIENT_PART_CLOSE;
    }
    return CLIENT_PART_NONE;
}

Client *client_find(Window window)
{
    Client *client;

    for (client = wm.clients; client != NULL; client = client->next) {
        if (client->window == window) {
            return client;
        }
    }
    return NULL;
}

Client *client_find_any(Window window)
{
    Client *client;

    for (client = wm.clients; client != NULL; client = client->next) {
        if (client_window_part(client, window) != CLIENT_PART_NONE) {
            return client;
        }
    }
    return NULL;
}

static Client *client_from_descendant(Window window)
{
    while (window != None && window != wm.root) {
        Window root_return;
        Window parent_return;
        Window *children = NULL;
        unsigned int child_count = 0U;
        Client *client = client_find_any(window);

        if (client != NULL) {
            return client;
        }

        if (XQueryTree(wm.display,
                       window,
                       &root_return,
                       &parent_return,
                       &children,
                       &child_count) == 0) {
            return NULL;
        }
        if (children != NULL) {
            XFree(children);
        }
        if (parent_return == window) {
            return NULL;
        }
        window = parent_return;
    }
    return NULL;
}

void client_set_wm_state(Client *client, long state)
{
    long data[2];

    data[0] = state;
    data[1] = (long)None;
    XChangeProperty(wm.display,
                    client->window,
                    wm.atoms.wm_state,
                    wm.atoms.wm_state,
                    32,
                    PropModeReplace,
                    (const unsigned char *)data,
                    2);
}

void client_update_title(Client *client)
{
    XTextProperty property;
    char *new_title = NULL;

    if (client == NULL) {
        return;
    }

    memset(&property, 0, sizeof(property));
    if (XGetTextProperty(wm.display,
                         client->window,
                         &property,
                         wm.atoms.net_wm_name) != 0 &&
        property.value != NULL) {
        if (property.nitems > 0UL && property.format == 8) {
            size_t bytes = (size_t)property.nitems;

            new_title = malloc(bytes + 1U);
            if (new_title != NULL) {
                memcpy(new_title, property.value, bytes);
                new_title[bytes] = '\0';
            }
        }
        XFree(property.value);
    }

    if (new_title == NULL) {
        char *legacy_title = NULL;

        if (XFetchName(wm.display, client->window, &legacy_title) != 0 &&
            legacy_title != NULL) {
            new_title = duplicate_string(legacy_title);
            XFree(legacy_title);
        }
    }

    if (new_title == NULL) {
        new_title = duplicate_string("(sin titulo)");
    }

    free(client->title);
    client->title = new_title;
    decor_redraw(client);
    bar_update();
}

void client_send_configure(Client *client)
{
    XEvent notification;

    if (client == NULL || !client->reparented) {
        return;
    }

    memset(&notification, 0, sizeof(notification));
    notification.xconfigure.type = ConfigureNotify;
    notification.xconfigure.display = wm.display;
    notification.xconfigure.event = client->window;
    notification.xconfigure.window = client->window;
    notification.xconfigure.x = decor_client_root_x(client);
    notification.xconfigure.y = decor_client_root_y(client);
    notification.xconfigure.width = client->width;
    notification.xconfigure.height = client->height;
    notification.xconfigure.border_width = 0;
    notification.xconfigure.above = None;
    notification.xconfigure.override_redirect = False;
    XSendEvent(wm.display,
               client->window,
               False,
               StructureNotifyMask,
               &notification);
}

void client_manage(Window window, bool map_requested)
{
    XWindowAttributes attributes;
    Client *client;
    bool was_viewable;
    Client *transient_parent;

    if (client_find(window) != NULL) {
        return;
    }
    if (XGetWindowAttributes(wm.display, window, &attributes) == 0) {
        return;
    }
    if (attributes.override_redirect || attributes.class == InputOnly) {
        if (map_requested) {
            XMapWindow(wm.display, window);
        }
        return;
    }

    client = calloc(1U, sizeof(*client));
    if (client == NULL) {
        fprintf(stderr, "%s: memoria insuficiente para gestionar una ventana\n", AWM_NAME);
        return;
    }

    was_viewable = attributes.map_state == IsViewable;
    client->window = window;
    client->x = attributes.x;
    client->y = attributes.y;
    client->width = attributes.width;
    client->height = attributes.height;
    client->old_x = attributes.x;
    client->old_y = attributes.y;
    client->old_width = attributes.width;
    client->old_height = attributes.height;
    client->fullscreen_x = attributes.x;
    client->fullscreen_y = attributes.y;
    client->fullscreen_width = attributes.width;
    client->fullscreen_height = attributes.height;
    client->original_border_width = attributes.border_width;
    client->mapped = attributes.map_state != IsUnmapped || map_requested;
    client->visible = false;
    client->desktop = ewmh_get_window_desktop(window, wm.current_desktop);

    hints_initialize_client(client);
    ewmh_read_client_state(client);
    transient_parent = client_find(client->transient_for);
    if (transient_parent != NULL) {
        client->desktop = transient_parent->desktop;
    }
    if (client->window_type == CLIENT_TYPE_DOCK ||
        client->window_type == CLIENT_TYPE_DESKTOP) {
        client->sticky = true;
        client->skip_taskbar = true;
        client->skip_pager = true;
    }
    client_apply_size_hints(client, &client->width, &client->height);

    desktop_place_client(client, &attributes, map_requested);
    if (transient_parent != NULL && !client->position_explicit && map_requested) {
        WorkArea area;
        int parent_width = decor_outer_width(transient_parent);
        int parent_height = decor_outer_height(transient_parent);
        int outer_width = decor_outer_width(client);
        int outer_height = decor_outer_height(client);

        ewmh_get_workarea(client->desktop, &area);
        client->x = transient_parent->x + (parent_width - outer_width) / 2;
        client->y = transient_parent->y + (parent_height - outer_height) / 2;
        if (client->x < area.x) {
            client->x = area.x;
        }
        if (client->y < area.y) {
            client->y = area.y;
        }
        if (client->x + outer_width > area.x + area.width) {
            client->x = area.x + area.width - outer_width;
        }
        if (client->y + outer_height > area.y + area.height) {
            client->y = area.y + area.height - outer_height;
        }
    }
    client->old_x = client->x;
    client->old_y = client->y;
    append_client(client);
    select_client_events(client);
    client_update_title(client);
    if (!decor_create(client, was_viewable)) {
        detach_client(client);
        free(client->title);
        free(client);
        return;
    }
    mouse_grab_buttons(client, false);
    ewmh_set_client_desktop(client);
    ewmh_set_allowed_actions(client);
    ewmh_set_frame_extents(client);

    if (client->maximized) {
        client->maximized = false;
        client_set_maximized(client, true);
    }
    if (client->fullscreen) {
        client->fullscreen = false;
        client_set_fullscreen(client, true);
    }
    if (client->minimized || !desktop_is_current(client)) {
        client_set_wm_state(client, IconicState);
    } else {
        client_set_wm_state(client, NormalState);
    }
    ewmh_update_client_state(client);
    ewmh_update_client_list();
    ewmh_refresh_strut(client);
    ewmh_recalculate_workarea();

    if (client->mapped && !client->minimized && desktop_is_current(client)) {
        client_set_visible(client, true);
        if (should_focus_new_client(client)) {
            client_focus(client, true);
        } else {
            stacking_enforce();
        }
    } else {
        decor_redraw(client);
        bar_update();
    }
}

void client_show(Client *client)
{
    if (client == NULL) {
        return;
    }

    client->mapped = true;
    if (client->minimized) {
        client_restore(client);
        return;
    }

    client_set_wm_state(client, NormalState);
    if (desktop_is_current(client)) {
        client_set_visible(client, true);
        if (should_focus_new_client(client)) {
            client_focus(client, true);
        } else {
            stacking_enforce();
        }
    }
}

static void clear_focused_client(void)
{
    if (wm.focused != NULL) {
        decor_set_active(wm.focused, false);
        mouse_grab_buttons(wm.focused, false);
    }
    wm.focused = NULL;
}

void client_focus(Client *client, bool raise)
{
    bool accepts_input = true;

    if (!can_receive_focus(client)) {
        if (client != NULL) {
            return;
        }
        clear_focused_client();
        XSetInputFocus(wm.display,
                       wm.root,
                       RevertToPointerRoot,
                       wm.last_event_time);
        ewmh_update_active_window();
        bar_update();
        stacking_enforce();
        return;
    }

    if (wm.focused != client) {
        clear_focused_client();
        wm.focused = client;
        decor_set_active(client, true);
        mouse_grab_buttons(client, true);
    }

    if (raise) {
        stacking_raise(client);
    }

    accepts_input = client->accepts_input;

    if (accepts_input) {
        XSetInputFocus(wm.display,
                       client->window,
                       RevertToPointerRoot,
                       wm.last_event_time);
    }
    if (client->takes_focus) {
        send_protocol(client, wm.atoms.wm_take_focus);
    }

    if (client->demands_attention || client->urgent) {
        client->demands_attention = false;
        client->urgent = false;
        ewmh_update_client_state(client);
    }

    desktop_remember_focus(client);
    ewmh_update_active_window();
    bar_update();
    stacking_enforce();
}

void client_note_focus(Client *client)
{
    if (!can_receive_focus(client) || client == wm.focused) {
        return;
    }

    clear_focused_client();
    wm.focused = client;
    decor_set_active(client, true);
    mouse_grab_buttons(client, true);
    if (client->demands_attention || client->urgent) {
        client->demands_attention = false;
        client->urgent = false;
        ewmh_update_client_state(client);
    }
    desktop_remember_focus(client);
    ewmh_update_active_window();
    bar_update();
    stacking_enforce();
}

void client_sync_focus(void)
{
    Window focus_window;
    int revert_to;
    Client *actual_client;

    XGetInputFocus(wm.display, &focus_window, &revert_to);
    (void)revert_to;
    actual_client = client_from_descendant(focus_window);

    if (can_receive_focus(actual_client) && actual_client != wm.focused) {
        client_note_focus(actual_client);
    }
}

void client_cycle_focus(int direction)
{
    Client *next;

    client_sync_focus();
    next = next_mapped_client(wm.focused, direction);
    if (next != NULL) {
        client_focus(next, true);
    }
}

void client_close(Client *client)
{
    if (client == NULL) {
        return;
    }

    if (supports_protocol(client, wm.atoms.wm_delete_window)) {
        send_protocol(client, wm.atoms.wm_delete_window);
    } else {
        XKillClient(wm.display, client->window);
    }
    XFlush(wm.display);
}

static void restore_normal_geometry(Client *client)
{
    if (client->fullscreen) {
        client->x = client->fullscreen_x;
        client->y = client->fullscreen_y;
        client->width = client->fullscreen_width;
        client->height = client->fullscreen_height;
    }
    if (client->maximized) {
        client->x = client->old_x;
        client->y = client->old_y;
        client->width = client->old_width;
        client->height = client->old_height;
    }
}

void client_unmanage(Client *client, bool destroyed)
{
    Client *replacement;
    bool was_focused;

    if (client == NULL) {
        return;
    }

    replacement = next_mapped_client(client, 1);
    if (replacement == client) {
        replacement = NULL;
    }
    was_focused = wm.focused == client;

    mouse_cancel_drag(client);
    XUngrabButton(wm.display, AnyButton, AnyModifier, client->window);
    desktop_forget_client(client);

    if (!destroyed) {
        restore_normal_geometry(client);
        XDeleteProperty(wm.display, client->window, wm.atoms.wm_state);
        ewmh_clear_client_properties(client);
    }
    decor_release(client, destroyed, false);

    if (was_focused) {
        wm.focused = NULL;
    }
    detach_client(client);
    free(client->title);
    free(client);
    ewmh_update_client_list();
    ewmh_recalculate_workarea();

    if (was_focused) {
        client_focus(replacement, true);
    } else {
        bar_update();
    }
}

void client_cleanup_all(void)
{
    Client *client = wm.clients;

    wm.focused = NULL;
    wm.drag.mode = DRAG_NONE;
    wm.drag.client = NULL;

    while (client != NULL) {
        Client *next = client->next;

        XUngrabButton(wm.display, AnyButton, AnyModifier, client->window);
        restore_normal_geometry(client);
        decor_release(client, false, client->mapped);
        XDeleteProperty(wm.display, client->window, wm.atoms.wm_state);
        free(client->title);
        free(client);
        client = next;
    }

    wm.clients = NULL;
    wm.clients_tail = NULL;
}

void client_set_visible(Client *client, bool visible)
{
    if (client == NULL || client->visible == visible) {
        return;
    }

    if (visible) {
        if (!client->mapped || client->minimized) {
            return;
        }
        /* Map the frame first.  Mapping a reparented client while its frame is
         * still unmapped delivers MapNotify for an IsUnviewable window; GTK
         * can then fail to associate the surface with an X11 monitor. */
        XMapWindow(wm.display, client->frame);
        XMapWindow(wm.display, client->window);
        client->visible = true;
        client_set_wm_state(client, NormalState);
        decor_redraw(client);
    } else {
        XUnmapWindow(wm.display, client->frame);
        client->visible = false;
        if (client->mapped) {
            client_set_wm_state(client, IconicState);
        }
        if (wm.focused == client) {
            ewmh_update_active_window();
        }
    }
    stacking_enforce();
    bar_update();
}

static void set_client_geometry(Client *client,
                                int x,
                                int y,
                                int width,
                                int height)
{
    client->x = x;
    client->y = y;
    client->width = width > 0 ? width : 1;
    client->height = height > 0 ? height : 1;
    decor_apply_geometry(client);
}

static void apply_maximized_geometry(Client *client)
{
    WorkArea area;
    int border = decor_border_width(client);
    int title = decor_title_height(client);
    int width;
    int height;

    ewmh_get_workarea(client->sticky ? wm.current_desktop : client->desktop,
                      &area);
    width = area.width - 2 * border;
    height = area.height - title - 2 * border;

    set_client_geometry(client,
                        area.x,
                        area.y,
                        width > 0 ? width : 1,
                        height > 0 ? height : 1);
}

static void apply_fullscreen_geometry(Client *client)
{
    set_client_geometry(client,
                        0,
                        0,
                        wm.screen_width,
                        wm.screen_height);
}

void client_set_maximized(Client *client, bool maximized)
{
    if (client == NULL || client->maximized == maximized) {
        return;
    }

    if (maximized) {
        if (client->fullscreen) {
            client->old_x = client->fullscreen_x;
            client->old_y = client->fullscreen_y;
            client->old_width = client->fullscreen_width;
            client->old_height = client->fullscreen_height;
        } else {
            client->old_x = client->x;
            client->old_y = client->y;
            client->old_width = client->width;
            client->old_height = client->height;
        }
        client->maximized = true;
        if (!client->fullscreen) {
            apply_maximized_geometry(client);
        }
    } else {
        client->maximized = false;
        if (!client->fullscreen) {
            set_client_geometry(client,
                                client->old_x,
                                client->old_y,
                                client->old_width,
                                client->old_height);
        }
    }

    ewmh_update_client_state(client);
    decor_redraw(client);
    bar_update();
    if (client->visible) {
        client_focus(client, true);
    }
    stacking_enforce();
}

void client_toggle_maximized(Client *client)
{
    if (client != NULL) {
        client_set_maximized(client, !client->maximized);
    }
}

void client_set_fullscreen(Client *client, bool fullscreen)
{
    if (client == NULL || client->fullscreen == fullscreen) {
        return;
    }

    if (fullscreen) {
        client->fullscreen_x = client->x;
        client->fullscreen_y = client->y;
        client->fullscreen_width = client->width;
        client->fullscreen_height = client->height;
        client->fullscreen = true;
        apply_fullscreen_geometry(client);
    } else {
        client->fullscreen = false;
        if (client->maximized) {
            apply_maximized_geometry(client);
        } else {
            set_client_geometry(client,
                                client->fullscreen_x,
                                client->fullscreen_y,
                                client->fullscreen_width,
                                client->fullscreen_height);
        }
    }

    ewmh_update_client_state(client);
    ewmh_set_frame_extents(client);
    decor_redraw(client);
    bar_update();
    if (client->visible) {
        client_focus(client, true);
    }
    stacking_enforce();
}

void client_toggle_fullscreen(Client *client)
{
    if (client != NULL) {
        client_set_fullscreen(client, !client->fullscreen);
    }
}

void client_set_minimized(Client *client, bool minimized)
{
    Client *replacement;
    bool was_focused;

    if (client == NULL || client->minimized == minimized) {
        return;
    }

    if (minimized) {
        was_focused = wm.focused == client;
        client->minimized = true;
        client_set_wm_state(client, IconicState);
        ewmh_update_client_state(client);
        if (client->visible) {
            client_set_visible(client, false);
        }
        if (was_focused) {
            replacement = next_mapped_client(client, 1);
            client_focus(replacement, true);
        }
    } else {
        client->minimized = false;
        client_set_wm_state(client, NormalState);
        ewmh_update_client_state(client);
        if (client->mapped && desktop_is_current(client)) {
            client_set_visible(client, true);
            client_focus(client, true);
        }
    }

    bar_update();
}

void client_minimize(Client *client)
{
    client_set_minimized(client, true);
}

void client_restore(Client *client)
{
    client_set_minimized(client, false);
}

void client_reapply_screen_state(Client *client)
{
    if (client == NULL) {
        return;
    }

    if (client->fullscreen) {
        apply_fullscreen_geometry(client);
    } else if (client->maximized) {
        apply_maximized_geometry(client);
    }
}

void client_apply_size_hints(Client *client, int *width, int *height)
{
    hints_apply_size(client, width, height);
}

void client_configure_request(const XConfigureRequestEvent *event)
{
    Client *client = client_find(event->window);
    XWindowChanges changes;
    unsigned long requested_mask = event->value_mask;

    memset(&changes, 0, sizeof(changes));
    changes.x = event->x;
    changes.y = event->y;
    changes.width = event->width;
    changes.height = event->height;
    changes.border_width = event->border_width;
    changes.sibling = event->above;
    changes.stack_mode = event->detail;

    if (client == NULL) {
        XConfigureWindow(wm.display,
                         event->window,
                         (unsigned int)requested_mask,
                         &changes);
        return;
    }

    if (!client->maximized && !client->fullscreen) {
        int width = client->width;
        int height = client->height;

        if ((requested_mask & CWX) != 0UL) {
            client->x = event->x;
        }
        if ((requested_mask & CWY) != 0UL) {
            client->y = event->y;
        }
        if ((requested_mask & CWWidth) != 0UL) {
            width = event->width;
        }
        if ((requested_mask & CWHeight) != 0UL) {
            height = event->height;
        }
        if ((requested_mask & (CWWidth | CWHeight)) != 0UL) {
            client_apply_size_hints(client, &width, &height);
            client->width = width;
            client->height = height;
        }
        if ((requested_mask & (CWX | CWY | CWWidth | CWHeight)) != 0UL) {
            decor_apply_geometry(client);
        }
    }

    if ((requested_mask & (CWStackMode | CWSibling)) != 0UL) {
        stacking_restack_request(client, event->above, event->detail);
    }

    if ((requested_mask & (CWX | CWY | CWWidth | CWHeight | CWBorderWidth)) != 0UL ||
        client->maximized || client->fullscreen) {
        client_send_configure(client);
    }
}

void client_raise(Client *client)
{
    stacking_raise(client);
}

void client_refresh_decor(Client *client)
{
    ClientDecor previous;

    if (client == NULL) {
        return;
    }
    previous = client->decor;
    hints_refresh_transient(client);
    hints_refresh_window_type(client);
    client->decor = hints_requested_decor(client);
    if (client->decor != previous) {
        decor_rebuild(client, previous);
    }
    ewmh_set_allowed_actions(client);
    ewmh_set_frame_extents(client);
    ewmh_refresh_strut(client);
    ewmh_recalculate_workarea();
    stacking_enforce();
    bar_update();
}
