#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NET_WM_STATE_REMOVE = 0,
    NET_WM_STATE_ADD = 1,
    NET_WM_STATE_TOGGLE = 2
};

static Atom intern(const char *name)
{
    return XInternAtom(wm.display, name, False);
}

static bool property_matches(Window window,
                             Atom property,
                             Atom type,
                             int format,
                             const unsigned char *expected,
                             unsigned long expected_count)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *data = NULL;
    unsigned long request_length;
    size_t byte_count;
    bool matches = false;

    if (format == 8) {
        request_length = (expected_count + 3UL) / 4UL;
        byte_count = (size_t)expected_count;
    } else if (format == 32) {
        request_length = expected_count;
        byte_count = (size_t)expected_count * sizeof(unsigned long);
    } else {
        return false;
    }

    if (XGetWindowProperty(wm.display,
                           window,
                           property,
                           0L,
                           (long)request_length,
                           False,
                           type,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &data) == Success &&
        actual_type == type && actual_format == format &&
        item_count == expected_count && bytes_after == 0UL &&
        (byte_count == 0U ||
         (data != NULL && memcmp(data, expected, byte_count) == 0))) {
        matches = true;
    }

    if (data != NULL) {
        XFree(data);
    }
    return matches;
}

static void replace_property_if_changed(Window window,
                                        Atom property,
                                        Atom type,
                                        int format,
                                        const unsigned char *data,
                                        int item_count)
{
    if (item_count < 0 ||
        property_matches(window,
                         property,
                         type,
                         format,
                         data,
                         (unsigned long)item_count)) {
        return;
    }

    XChangeProperty(wm.display,
                    window,
                    property,
                    type,
                    format,
                    PropModeReplace,
                    data,
                    item_count);
}

static void set_cardinal(Window window, Atom property, unsigned long value)
{
    replace_property_if_changed(window,
                                property,
                                XA_CARDINAL,
                                32,
                                (const unsigned char *)&value,
                                1);
}

static bool get_cardinal(Window window, Atom property, unsigned long *value)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *data = NULL;
    bool valid = false;

    if (XGetWindowProperty(wm.display,
                           window,
                           property,
                           0L,
                           1L,
                           False,
                           XA_CARDINAL,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &data) == Success &&
        actual_type == XA_CARDINAL && actual_format == 32 && item_count >= 1UL &&
        data != NULL) {
        *value = *(const unsigned long *)data;
        valid = true;
    }

    if (data != NULL) {
        XFree(data);
    }
    return valid;
}

static void initialize_atoms(void)
{
    char selection_name[32];

    wm.atoms.wm_protocols = intern("WM_PROTOCOLS");
    wm.atoms.wm_delete_window = intern("WM_DELETE_WINDOW");
    wm.atoms.wm_take_focus = intern("WM_TAKE_FOCUS");
    wm.atoms.wm_state = intern("WM_STATE");
    wm.atoms.wm_change_state = intern("WM_CHANGE_STATE");
    (void)snprintf(selection_name,
                   sizeof(selection_name),
                   "WM_S%d",
                   wm.screen);
    wm.atoms.wm_selection = intern(selection_name);
    wm.atoms.manager = intern("MANAGER");
    wm.atoms.net_supported = intern("_NET_SUPPORTED");
    wm.atoms.net_supporting_wm_check = intern("_NET_SUPPORTING_WM_CHECK");
    wm.atoms.net_active_window = intern("_NET_ACTIVE_WINDOW");
    wm.atoms.net_client_list = intern("_NET_CLIENT_LIST");
    wm.atoms.net_client_list_stacking = intern("_NET_CLIENT_LIST_STACKING");
    wm.atoms.net_number_of_desktops = intern("_NET_NUMBER_OF_DESKTOPS");
    wm.atoms.net_current_desktop = intern("_NET_CURRENT_DESKTOP");
    wm.atoms.net_desktop_names = intern("_NET_DESKTOP_NAMES");
    wm.atoms.net_desktop_geometry = intern("_NET_DESKTOP_GEOMETRY");
    wm.atoms.net_desktop_viewport = intern("_NET_DESKTOP_VIEWPORT");
    wm.atoms.net_workarea = intern("_NET_WORKAREA");
    wm.atoms.net_frame_extents = intern("_NET_FRAME_EXTENTS");
    wm.atoms.net_request_frame_extents = intern("_NET_REQUEST_FRAME_EXTENTS");
    wm.atoms.net_wm_desktop = intern("_NET_WM_DESKTOP");
    wm.atoms.net_wm_state = intern("_NET_WM_STATE");
    wm.atoms.net_wm_state_fullscreen = intern("_NET_WM_STATE_FULLSCREEN");
    wm.atoms.net_wm_state_maximized_vert = intern("_NET_WM_STATE_MAXIMIZED_VERT");
    wm.atoms.net_wm_state_maximized_horz = intern("_NET_WM_STATE_MAXIMIZED_HORZ");
    wm.atoms.net_wm_state_hidden = intern("_NET_WM_STATE_HIDDEN");
    wm.atoms.net_wm_state_above = intern("_NET_WM_STATE_ABOVE");
    wm.atoms.net_wm_state_below = intern("_NET_WM_STATE_BELOW");
    wm.atoms.net_wm_state_sticky = intern("_NET_WM_STATE_STICKY");
    wm.atoms.net_wm_state_skip_taskbar = intern("_NET_WM_STATE_SKIP_TASKBAR");
    wm.atoms.net_wm_state_skip_pager = intern("_NET_WM_STATE_SKIP_PAGER");
    wm.atoms.net_wm_state_demands_attention =
        intern("_NET_WM_STATE_DEMANDS_ATTENTION");
    wm.atoms.net_wm_state_modal = intern("_NET_WM_STATE_MODAL");
    wm.atoms.net_wm_window_type = intern("_NET_WM_WINDOW_TYPE");
    wm.atoms.net_wm_window_type_normal = intern("_NET_WM_WINDOW_TYPE_NORMAL");
    wm.atoms.net_wm_window_type_dialog = intern("_NET_WM_WINDOW_TYPE_DIALOG");
    wm.atoms.net_wm_window_type_utility = intern("_NET_WM_WINDOW_TYPE_UTILITY");
    wm.atoms.net_wm_window_type_toolbar = intern("_NET_WM_WINDOW_TYPE_TOOLBAR");
    wm.atoms.net_wm_window_type_menu = intern("_NET_WM_WINDOW_TYPE_MENU");
    wm.atoms.net_wm_window_type_dropdown_menu =
        intern("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU");
    wm.atoms.net_wm_window_type_popup_menu =
        intern("_NET_WM_WINDOW_TYPE_POPUP_MENU");
    wm.atoms.net_wm_window_type_tooltip = intern("_NET_WM_WINDOW_TYPE_TOOLTIP");
    wm.atoms.net_wm_window_type_notification =
        intern("_NET_WM_WINDOW_TYPE_NOTIFICATION");
    wm.atoms.net_wm_window_type_splash = intern("_NET_WM_WINDOW_TYPE_SPLASH");
    wm.atoms.net_wm_window_type_dock = intern("_NET_WM_WINDOW_TYPE_DOCK");
    wm.atoms.net_wm_window_type_desktop = intern("_NET_WM_WINDOW_TYPE_DESKTOP");
    wm.atoms.net_wm_strut = intern("_NET_WM_STRUT");
    wm.atoms.net_wm_strut_partial = intern("_NET_WM_STRUT_PARTIAL");
    wm.atoms.net_wm_user_time = intern("_NET_WM_USER_TIME");
    wm.atoms.net_wm_allowed_actions = intern("_NET_WM_ALLOWED_ACTIONS");
    wm.atoms.net_wm_action_close = intern("_NET_WM_ACTION_CLOSE");
    wm.atoms.net_wm_action_minimize = intern("_NET_WM_ACTION_MINIMIZE");
    wm.atoms.net_wm_action_maximized_vert = intern("_NET_WM_ACTION_MAXIMIZE_VERT");
    wm.atoms.net_wm_action_maximized_horz = intern("_NET_WM_ACTION_MAXIMIZE_HORZ");
    wm.atoms.net_wm_action_fullscreen = intern("_NET_WM_ACTION_FULLSCREEN");
    wm.atoms.net_wm_action_move = intern("_NET_WM_ACTION_MOVE");
    wm.atoms.net_wm_action_resize = intern("_NET_WM_ACTION_RESIZE");
    wm.atoms.net_wm_action_change_desktop = intern("_NET_WM_ACTION_CHANGE_DESKTOP");
    wm.atoms.net_wm_action_above = intern("_NET_WM_ACTION_ABOVE");
    wm.atoms.net_wm_action_below = intern("_NET_WM_ACTION_BELOW");
    wm.atoms.net_close_window = intern("_NET_CLOSE_WINDOW");
    wm.atoms.net_moveresize_window = intern("_NET_MOVERESIZE_WINDOW");
    wm.atoms.net_wm_moveresize = intern("_NET_WM_MOVERESIZE");
    wm.atoms.net_restack_window = intern("_NET_RESTACK_WINDOW");
    wm.atoms.net_wm_name = intern("_NET_WM_NAME");
    wm.atoms.motif_wm_hints = intern("_MOTIF_WM_HINTS");
    wm.atoms.utf8_string = intern("UTF8_STRING");
}

static void publish_supported_atoms(void)
{
    Atom supported[] = {
        wm.atoms.net_supported,
        wm.atoms.net_supporting_wm_check,
        wm.atoms.net_active_window,
        wm.atoms.net_client_list,
        wm.atoms.net_client_list_stacking,
        wm.atoms.net_number_of_desktops,
        wm.atoms.net_current_desktop,
        wm.atoms.net_desktop_names,
        wm.atoms.net_desktop_geometry,
        wm.atoms.net_desktop_viewport,
        wm.atoms.net_workarea,
        wm.atoms.net_frame_extents,
        wm.atoms.net_request_frame_extents,
        wm.atoms.net_wm_desktop,
        wm.atoms.net_wm_state,
        wm.atoms.net_wm_state_fullscreen,
        wm.atoms.net_wm_state_maximized_vert,
        wm.atoms.net_wm_state_maximized_horz,
        wm.atoms.net_wm_state_hidden,
        wm.atoms.net_wm_state_above,
        wm.atoms.net_wm_state_below,
        wm.atoms.net_wm_state_sticky,
        wm.atoms.net_wm_state_skip_taskbar,
        wm.atoms.net_wm_state_skip_pager,
        wm.atoms.net_wm_state_demands_attention,
        wm.atoms.net_wm_state_modal,
        wm.atoms.net_wm_window_type,
        wm.atoms.net_wm_window_type_normal,
        wm.atoms.net_wm_window_type_dialog,
        wm.atoms.net_wm_window_type_utility,
        wm.atoms.net_wm_window_type_toolbar,
        wm.atoms.net_wm_window_type_menu,
        wm.atoms.net_wm_window_type_dropdown_menu,
        wm.atoms.net_wm_window_type_popup_menu,
        wm.atoms.net_wm_window_type_tooltip,
        wm.atoms.net_wm_window_type_notification,
        wm.atoms.net_wm_window_type_splash,
        wm.atoms.net_wm_window_type_dock,
        wm.atoms.net_wm_window_type_desktop,
        wm.atoms.net_wm_strut,
        wm.atoms.net_wm_strut_partial,
        wm.atoms.net_wm_allowed_actions,
        wm.atoms.net_wm_action_close,
        wm.atoms.net_wm_action_minimize,
        wm.atoms.net_wm_action_maximized_vert,
        wm.atoms.net_wm_action_maximized_horz,
        wm.atoms.net_wm_action_fullscreen,
        wm.atoms.net_wm_action_move,
        wm.atoms.net_wm_action_resize,
        wm.atoms.net_wm_action_change_desktop,
        wm.atoms.net_wm_action_above,
        wm.atoms.net_wm_action_below,
        wm.atoms.net_close_window,
        wm.atoms.net_moveresize_window,
        wm.atoms.net_wm_moveresize,
        wm.atoms.net_restack_window,
        wm.atoms.net_wm_name
    };

    XChangeProperty(wm.display,
                    wm.root,
                    wm.atoms.net_supported,
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (const unsigned char *)supported,
                    (int)(sizeof(supported) / sizeof(supported[0])));
}

static void create_supporting_window(void)
{
    XSetWindowAttributes attributes;

    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.event_mask = PropertyChangeMask;
    wm.supporting_window = XCreateWindow(wm.display,
                                         wm.root,
                                         -1,
                                         -1,
                                         1U,
                                         1U,
                                         0U,
                                         CopyFromParent,
                                         InputOutput,
                                         CopyFromParent,
                                         CWOverrideRedirect | CWEventMask,
                                         &attributes);

    XChangeProperty(wm.display,
                    wm.root,
                    wm.atoms.net_supporting_wm_check,
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&wm.supporting_window,
                    1);
    XChangeProperty(wm.display,
                    wm.supporting_window,
                    wm.atoms.net_supporting_wm_check,
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&wm.supporting_window,
                    1);
    XChangeProperty(wm.display,
                    wm.supporting_window,
                    wm.atoms.net_wm_name,
                    wm.atoms.utf8_string,
                    8,
                    PropModeReplace,
                    (const unsigned char *)AWM_NAME,
                    (int)strlen(AWM_NAME));
}

static Time supporting_window_timestamp(void)
{
    Atom timestamp_atom = intern("_ANTONIOWM_TIMESTAMP");
    const unsigned char marker = 0U;
    XEvent event;

    XChangeProperty(wm.display,
                    wm.supporting_window,
                    timestamp_atom,
                    timestamp_atom,
                    8,
                    PropModeReplace,
                    &marker,
                    1);
    do {
        XWindowEvent(wm.display,
                     wm.supporting_window,
                     PropertyChangeMask,
                     &event);
    } while (event.type != PropertyNotify ||
             event.xproperty.atom != timestamp_atom ||
             event.xproperty.state != PropertyNewValue);
    XDeleteProperty(wm.display, wm.supporting_window, timestamp_atom);
    return event.xproperty.time;
}

static void claim_wm_selection(void)
{
    XEvent manager_message;
    Time timestamp = supporting_window_timestamp();

    XSetSelectionOwner(wm.display,
                       wm.atoms.wm_selection,
                       wm.supporting_window,
                       timestamp);
    XSync(wm.display, False);
    if (XGetSelectionOwner(wm.display, wm.atoms.wm_selection) !=
        wm.supporting_window) {
        fprintf(stderr,
                "%s: no se pudo adquirir la seleccion ICCCM WM_S%d\n",
                AWM_NAME,
                wm.screen);
        return;
    }

    memset(&manager_message, 0, sizeof(manager_message));
    manager_message.xclient.type = ClientMessage;
    manager_message.xclient.window = wm.root;
    manager_message.xclient.message_type = wm.atoms.manager;
    manager_message.xclient.format = 32;
    manager_message.xclient.data.l[0] = (long)timestamp;
    manager_message.xclient.data.l[1] = (long)wm.atoms.wm_selection;
    manager_message.xclient.data.l[2] = (long)wm.supporting_window;
    XSendEvent(wm.display,
               wm.root,
               False,
               StructureNotifyMask,
               &manager_message);
}

void ewmh_initialize(void)
{
    unsigned long previous_desktop;

    initialize_atoms();
    if (get_cardinal(wm.root, wm.atoms.net_current_desktop, &previous_desktop) &&
        previous_desktop < (unsigned long)wm.desktop_count) {
        wm.current_desktop = (unsigned int)previous_desktop;
    }

    create_supporting_window();
    claim_wm_selection();
    publish_supported_atoms();
    ewmh_update_desktop_properties();
    ewmh_update_active_window();
    ewmh_update_client_list();
}

void ewmh_cleanup(void)
{
    Atom properties[] = {
        wm.atoms.net_supported,
        wm.atoms.net_supporting_wm_check,
        wm.atoms.net_active_window,
        wm.atoms.net_client_list,
        wm.atoms.net_client_list_stacking,
        wm.atoms.net_number_of_desktops,
        wm.atoms.net_desktop_names,
        wm.atoms.net_desktop_geometry,
        wm.atoms.net_desktop_viewport,
        wm.atoms.net_workarea
    };
    size_t index;

    for (index = 0U; index < sizeof(properties) / sizeof(properties[0]); ++index) {
        XDeleteProperty(wm.display, wm.root, properties[index]);
    }
    if (!wm.restarting) {
        XDeleteProperty(wm.display, wm.root, wm.atoms.net_current_desktop);
    }
    if (wm.supporting_window != None &&
        XGetSelectionOwner(wm.display, wm.atoms.wm_selection) ==
            wm.supporting_window) {
        XSetSelectionOwner(wm.display,
                           wm.atoms.wm_selection,
                           None,
                           CurrentTime);
    }
    if (wm.supporting_window != None) {
        XDestroyWindow(wm.display, wm.supporting_window);
        wm.supporting_window = None;
    }
}

unsigned int ewmh_get_window_desktop(Window window, unsigned int fallback)
{
    unsigned long desktop;

    if (get_cardinal(window, wm.atoms.net_wm_desktop, &desktop) &&
        desktop < (unsigned long)wm.desktop_count) {
        return (unsigned int)desktop;
    }
    return fallback;
}

void ewmh_read_client_state(Client *client)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *data = NULL;
    unsigned long index;
    bool vertical = false;
    bool horizontal = false;
    unsigned long requested_desktop;

    client->maximized = false;
    client->fullscreen = false;
    client->minimized = false;
    client->above = false;
    client->below = false;
    client->sticky = false;
    client->skip_taskbar = false;
    client->skip_pager = false;
    client->demands_attention = false;
    client->modal = false;
    if (get_cardinal(client->window,
                     wm.atoms.net_wm_desktop,
                     &requested_desktop) &&
        requested_desktop == 0xffffffffUL) {
        client->sticky = true;
    }

    if (XGetWindowProperty(wm.display,
                           client->window,
                           wm.atoms.net_wm_state,
                           0L,
                           32L,
                           False,
                           XA_ATOM,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &data) != Success ||
        actual_type != XA_ATOM || actual_format != 32 || data == NULL) {
        if (data != NULL) {
            XFree(data);
        }
        return;
    }

    for (index = 0UL; index < item_count; ++index) {
        Atom state = ((const Atom *)data)[index];

        if (state == wm.atoms.net_wm_state_fullscreen) {
            client->fullscreen = true;
        } else if (state == wm.atoms.net_wm_state_hidden) {
            client->minimized = true;
        } else if (state == wm.atoms.net_wm_state_maximized_vert) {
            vertical = true;
        } else if (state == wm.atoms.net_wm_state_maximized_horz) {
            horizontal = true;
        } else if (state == wm.atoms.net_wm_state_above) {
            client->above = true;
        } else if (state == wm.atoms.net_wm_state_below) {
            client->below = true;
        } else if (state == wm.atoms.net_wm_state_sticky) {
            client->sticky = true;
        } else if (state == wm.atoms.net_wm_state_skip_taskbar) {
            client->skip_taskbar = true;
        } else if (state == wm.atoms.net_wm_state_skip_pager) {
            client->skip_pager = true;
        } else if (state == wm.atoms.net_wm_state_demands_attention) {
            client->demands_attention = true;
        } else if (state == wm.atoms.net_wm_state_modal) {
            client->modal = true;
        }
    }
    client->maximized = vertical || horizontal;
    if (client->above && client->below) {
        client->below = false;
    }
    XFree(data);
}

void ewmh_set_client_desktop(const Client *client)
{
    set_cardinal(client->window,
                 wm.atoms.net_wm_desktop,
                 client->sticky ? 0xffffffffUL
                                : (unsigned long)client->desktop);
}

void ewmh_set_allowed_actions(const Client *client)
{
    Atom actions[12];
    int count = 0;

    if (client_is_taskbar_visible(client)) {
        actions[count++] = wm.atoms.net_wm_action_close;
        actions[count++] = wm.atoms.net_wm_action_minimize;
        actions[count++] = wm.atoms.net_wm_action_maximized_vert;
        actions[count++] = wm.atoms.net_wm_action_maximized_horz;
        actions[count++] = wm.atoms.net_wm_action_fullscreen;
        actions[count++] = wm.atoms.net_wm_action_move;
        actions[count++] = wm.atoms.net_wm_action_resize;
        actions[count++] = wm.atoms.net_wm_action_change_desktop;
        actions[count++] = wm.atoms.net_wm_action_above;
        actions[count++] = wm.atoms.net_wm_action_below;
    }

    if (count == 0) {
        XDeleteProperty(wm.display,
                        client->window,
                        wm.atoms.net_wm_allowed_actions);
    } else {
        XChangeProperty(wm.display,
                        client->window,
                        wm.atoms.net_wm_allowed_actions,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (const unsigned char *)actions,
                        count);
    }
}

static void set_frame_extents(Window window,
                              bool fullscreen,
                              ClientDecor decor)
{
    unsigned long extents[4];

    if (fullscreen || decor == CLIENT_DECOR_NONE) {
        extents[0] = 0UL;
        extents[1] = 0UL;
        extents[2] = 0UL;
        extents[3] = 0UL;
    } else if (decor == CLIENT_DECOR_BORDER) {
        extents[0] = (unsigned long)AWM_BORDER_WIDTH;
        extents[1] = (unsigned long)AWM_BORDER_WIDTH;
        extents[2] = (unsigned long)AWM_BORDER_WIDTH;
        extents[3] = (unsigned long)AWM_BORDER_WIDTH;
    } else {
        extents[0] = (unsigned long)AWM_BORDER_WIDTH;
        extents[1] = (unsigned long)AWM_BORDER_WIDTH;
        extents[2] = (unsigned long)(AWM_BORDER_WIDTH + AWM_TITLE_HEIGHT);
        extents[3] = (unsigned long)AWM_BORDER_WIDTH;
    }

    XChangeProperty(wm.display,
                    window,
                    wm.atoms.net_frame_extents,
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (const unsigned char *)extents,
                    4);
}

void ewmh_set_frame_extents(const Client *client)
{
    if (client != NULL) {
        set_frame_extents(client->window, client->fullscreen, client->decor);
    }
}

void ewmh_update_client_state(const Client *client)
{
    Atom states[12];
    int count = 0;

    if (client->maximized) {
        states[count++] = wm.atoms.net_wm_state_maximized_vert;
        states[count++] = wm.atoms.net_wm_state_maximized_horz;
    }
    if (client->fullscreen) {
        states[count++] = wm.atoms.net_wm_state_fullscreen;
    }
    if (client->minimized) {
        states[count++] = wm.atoms.net_wm_state_hidden;
    }
    if (client->above) {
        states[count++] = wm.atoms.net_wm_state_above;
    }
    if (client->below) {
        states[count++] = wm.atoms.net_wm_state_below;
    }
    if (client->sticky) {
        states[count++] = wm.atoms.net_wm_state_sticky;
    }
    if (client->skip_taskbar) {
        states[count++] = wm.atoms.net_wm_state_skip_taskbar;
    }
    if (client->skip_pager) {
        states[count++] = wm.atoms.net_wm_state_skip_pager;
    }
    if (client->demands_attention) {
        states[count++] = wm.atoms.net_wm_state_demands_attention;
    }
    if (client->modal) {
        states[count++] = wm.atoms.net_wm_state_modal;
    }

    XChangeProperty(wm.display,
                    client->window,
                    wm.atoms.net_wm_state,
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (const unsigned char *)states,
                    count);
}

void ewmh_clear_client_properties(const Client *client)
{
    XDeleteProperty(wm.display, client->window, wm.atoms.net_wm_desktop);
    XDeleteProperty(wm.display, client->window, wm.atoms.net_wm_state);
    XDeleteProperty(wm.display, client->window, wm.atoms.net_wm_allowed_actions);
    XDeleteProperty(wm.display, client->window, wm.atoms.net_frame_extents);
}

void ewmh_update_active_window(void)
{
    Window active = wm.focused != NULL && wm.focused->visible
                        ? wm.focused->window
                        : None;

    XChangeProperty(wm.display,
                    wm.root,
                    wm.atoms.net_active_window,
                    XA_WINDOW,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&active,
                    1);
}

void ewmh_update_client_list(void)
{
    Client *client;
    Window *clients;
    Window *stacking;
    size_t client_count = 0U;
    size_t index = 0U;
    size_t stacking_count = 0U;
    Window root_return;
    Window parent_return;
    Window *children = NULL;
    unsigned int child_count = 0U;
    unsigned int child_index;

    for (client = wm.clients; client != NULL; client = client->next) {
        ++client_count;
    }

    clients = client_count > 0U ? calloc(client_count, sizeof(*clients)) : NULL;
    stacking = client_count > 0U ? calloc(client_count, sizeof(*stacking)) : NULL;

    for (client = wm.clients; client != NULL && clients != NULL; client = client->next) {
        clients[index++] = client->window;
    }

    if (client_count == 0U || clients == NULL) {
        XDeleteProperty(wm.display, wm.root, wm.atoms.net_client_list);
    } else {
        XChangeProperty(wm.display,
                        wm.root,
                        wm.atoms.net_client_list,
                        XA_WINDOW,
                        32,
                        PropModeReplace,
                        (const unsigned char *)clients,
                        (int)client_count);
    }

    if (stacking != NULL &&
        XQueryTree(wm.display,
                   wm.root,
                   &root_return,
                   &parent_return,
                   &children,
                   &child_count) != 0) {
        for (child_index = 0U; child_index < child_count; ++child_index) {
            Client *stacked = client_find_any(children[child_index]);

            if (stacked != NULL && stacked->frame == children[child_index]) {
                stacking[stacking_count++] = stacked->window;
            }
        }
    }

    if (stacking_count == 0U) {
        XDeleteProperty(wm.display, wm.root, wm.atoms.net_client_list_stacking);
    } else {
        XChangeProperty(wm.display,
                        wm.root,
                        wm.atoms.net_client_list_stacking,
                        XA_WINDOW,
                        32,
                        PropModeReplace,
                        (const unsigned char *)stacking,
                        (int)stacking_count);
    }

    if (children != NULL) {
        XFree(children);
    }
    free(clients);
    free(stacking);
}

static bool get_cardinal_array(Window window,
                               Atom property,
                               unsigned long *values,
                               unsigned long wanted)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *data = NULL;
    bool valid = false;

    if (XGetWindowProperty(wm.display,
                           window,
                           property,
                           0L,
                           (long)wanted,
                           False,
                           XA_CARDINAL,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &data) == Success &&
        actual_type == XA_CARDINAL && actual_format == 32 &&
        item_count >= wanted && data != NULL) {
        memcpy(values, data, (size_t)wanted * sizeof(*values));
        valid = true;
    }
    if (data != NULL) {
        XFree(data);
    }
    return valid;
}

void ewmh_refresh_strut(Client *client)
{
    unsigned long legacy[4];

    if (client == NULL) {
        return;
    }
    memset(client->strut, 0, sizeof(client->strut));
    client->has_strut = get_cardinal_array(client->window,
                                           wm.atoms.net_wm_strut_partial,
                                           client->strut,
                                           12UL);
    if (!client->has_strut &&
        get_cardinal_array(client->window,
                           wm.atoms.net_wm_strut,
                           legacy,
                           4UL)) {
        memcpy(client->strut, legacy, sizeof(legacy));
        client->strut[4] = 0UL;
        client->strut[5] = wm.screen_height > 0
                               ? (unsigned long)(wm.screen_height - 1)
                               : 0UL;
        client->strut[6] = client->strut[4];
        client->strut[7] = client->strut[5];
        client->strut[8] = 0UL;
        client->strut[9] = wm.screen_width > 0
                               ? (unsigned long)(wm.screen_width - 1)
                               : 0UL;
        client->strut[10] = client->strut[8];
        client->strut[11] = client->strut[9];
        client->has_strut = true;
    }
}

static unsigned long limited_reservation(unsigned long value, int limit)
{
    if (limit <= 0) {
        return 0UL;
    }
    return value > (unsigned long)limit ? (unsigned long)limit : value;
}

static bool range_intersects(unsigned long start,
                             unsigned long end,
                             int limit)
{
    return limit > 0 && start <= end && start < (unsigned long)limit;
}

static void external_struts(unsigned int desktop,
                            unsigned long *left,
                            unsigned long *right,
                            unsigned long *top,
                            unsigned long *bottom)
{
    Client *client;

    *left = 0UL;
    *right = 0UL;
    *top = 0UL;
    *bottom = 0UL;
    for (client = wm.clients; client != NULL; client = client->next) {
        if (!client->has_strut || !client->mapped || client->minimized ||
            (!client->sticky && client->desktop != desktop)) {
            continue;
        }
        if (range_intersects(client->strut[4],
                             client->strut[5],
                             wm.screen_height) &&
            client->strut[0] > *left) {
            *left = limited_reservation(client->strut[0], wm.screen_width);
        }
        if (range_intersects(client->strut[6],
                             client->strut[7],
                             wm.screen_height) &&
            client->strut[1] > *right) {
            *right = limited_reservation(client->strut[1], wm.screen_width);
        }
        if (range_intersects(client->strut[8],
                             client->strut[9],
                             wm.screen_width) &&
            client->strut[2] > *top) {
            *top = limited_reservation(client->strut[2], wm.screen_height);
        }
        if (range_intersects(client->strut[10],
                             client->strut[11],
                             wm.screen_width) &&
            client->strut[3] > *bottom) {
            *bottom = limited_reservation(client->strut[3], wm.screen_height);
        }
    }
}

void ewmh_get_workarea(unsigned int desktop, WorkArea *area)
{
    unsigned long left;
    unsigned long right;
    unsigned long top;
    unsigned long bottom;
    int usable_width;
    int usable_height;

    if (area == NULL) {
        return;
    }
    external_struts(desktop, &left, &right, &top, &bottom);
    area->x = (int)left;
    area->y = (int)top + (int)AWM_BAR_HEIGHT;
    usable_width = wm.screen_width - (int)left - (int)right;
    usable_height = wm.screen_height - area->y - (int)bottom;
    area->width = usable_width > 0 ? usable_width : 1;
    area->height = usable_height > 0 ? usable_height : 1;
}

void ewmh_get_bar_geometry(int *x, int *y, int *width, int *height)
{
    unsigned long left;
    unsigned long right;
    unsigned long top;
    unsigned long bottom;
    int usable_width;

    external_struts(wm.current_desktop, &left, &right, &top, &bottom);
    (void)bottom;
    usable_width = wm.screen_width - (int)left - (int)right;
    *x = (int)left;
    *y = (int)top;
    *width = usable_width > 0 ? usable_width : 1;
    *height = AWM_BAR_HEIGHT > 0U ? (int)AWM_BAR_HEIGHT : 1;
}

void ewmh_recalculate_workarea(void)
{
    ewmh_update_desktop_properties();
    bar_reconfigure();
    desktop_reapply_screen_states();
}

void ewmh_update_desktop_properties(void)
{
    unsigned long geometry[2];
    unsigned long *viewport;
    unsigned long *workarea;
    char *names;
    static const char *const configured_names[] = AWM_DESKTOP_NAMES;
    size_t names_capacity;
    size_t names_length = 0U;
    unsigned int desktop;

    set_cardinal(wm.root,
                 wm.atoms.net_number_of_desktops,
                 (unsigned long)wm.desktop_count);
    set_cardinal(wm.root,
                 wm.atoms.net_current_desktop,
                 (unsigned long)wm.current_desktop);

    geometry[0] = (unsigned long)wm.screen_width;
    geometry[1] = (unsigned long)wm.screen_height;
    replace_property_if_changed(wm.root,
                                wm.atoms.net_desktop_geometry,
                                XA_CARDINAL,
                                32,
                                (const unsigned char *)geometry,
                                2);

    viewport = calloc((size_t)wm.desktop_count * 2U, sizeof(*viewport));
    workarea = calloc((size_t)wm.desktop_count * 4U, sizeof(*workarea));
    if (viewport != NULL) {
        replace_property_if_changed(wm.root,
                                    wm.atoms.net_desktop_viewport,
                                    XA_CARDINAL,
                                    32,
                                    (const unsigned char *)viewport,
                                    (int)(wm.desktop_count * 2U));
    }
    if (workarea != NULL) {
        for (desktop = 0U; desktop < wm.desktop_count; ++desktop) {
            WorkArea area;

            ewmh_get_workarea(desktop, &area);
            workarea[(size_t)desktop * 4U + 0U] = (unsigned long)area.x;
            workarea[(size_t)desktop * 4U + 1U] = (unsigned long)area.y;
            workarea[(size_t)desktop * 4U + 2U] = (unsigned long)area.width;
            workarea[(size_t)desktop * 4U + 3U] = (unsigned long)area.height;
        }
        replace_property_if_changed(wm.root,
                                    wm.atoms.net_workarea,
                                    XA_CARDINAL,
                                    32,
                                    (const unsigned char *)workarea,
                                    (int)(wm.desktop_count * 4U));
    }
    free(viewport);
    free(workarea);

    names_capacity = (size_t)wm.desktop_count * 64U;
    names = calloc(names_capacity, 1U);
    if (names == NULL) {
        return;
    }
    for (desktop = 0U; desktop < wm.desktop_count; ++desktop) {
        int written;

        if (desktop < sizeof(configured_names) / sizeof(configured_names[0])) {
            written = snprintf(names + names_length,
                               names_capacity - names_length,
                               "%s",
                               configured_names[desktop]);
        } else {
            written = snprintf(names + names_length,
                               names_capacity - names_length,
                               "%u",
                               desktop + 1U);
        }

        if (written < 0 || (size_t)written + 1U > names_capacity - names_length) {
            break;
        }
        names_length += (size_t)written + 1U;
    }
    replace_property_if_changed(wm.root,
                                wm.atoms.net_desktop_names,
                                wm.atoms.utf8_string,
                                8,
                                (const unsigned char *)names,
                                (int)names_length);
    free(names);
}

static bool state_after_action(long action, bool current)
{
    if (action == NET_WM_STATE_ADD) {
        return true;
    }
    if (action == NET_WM_STATE_REMOVE) {
        return false;
    }
    if (action == NET_WM_STATE_TOGGLE) {
        return !current;
    }
    return current;
}

static void set_client_attention(Client *client, bool attention)
{
    if (client == NULL) {
        return;
    }
    client->demands_attention = attention && client != wm.focused;
    ewmh_update_client_state(client);
    bar_update();
}

static void apply_simple_state(Client *client, Atom state, long action)
{
    bool value;

    if (state == None || state == wm.atoms.net_wm_state_maximized_vert ||
        state == wm.atoms.net_wm_state_maximized_horz) {
        return;
    }
    if (state == wm.atoms.net_wm_state_fullscreen) {
        client_set_fullscreen(client,
                              state_after_action(action, client->fullscreen));
    } else if (state == wm.atoms.net_wm_state_hidden) {
        client_set_minimized(client,
                             state_after_action(action, client->minimized));
    } else if (state == wm.atoms.net_wm_state_above) {
        client->above = state_after_action(action, client->above);
        if (client->above) {
            client->below = false;
        }
    } else if (state == wm.atoms.net_wm_state_below) {
        client->below = state_after_action(action, client->below);
        if (client->below) {
            client->above = false;
        }
    } else if (state == wm.atoms.net_wm_state_sticky) {
        value = state_after_action(action, client->sticky);
        client->sticky = value;
        ewmh_set_client_desktop(client);
        if (client->mapped && !client->minimized) {
            client_set_visible(client, desktop_is_current(client));
        }
        if (!client->sticky && wm.focused == client &&
            !desktop_is_current(client)) {
            client_focus(NULL, true);
        }
    } else if (state == wm.atoms.net_wm_state_skip_taskbar) {
        client->skip_taskbar = state_after_action(action, client->skip_taskbar);
    } else if (state == wm.atoms.net_wm_state_skip_pager) {
        client->skip_pager = state_after_action(action, client->skip_pager);
    } else if (state == wm.atoms.net_wm_state_demands_attention) {
        set_client_attention(
            client,
            state_after_action(action, client->demands_attention));
    } else if (state == wm.atoms.net_wm_state_modal) {
        client->modal = state_after_action(action, client->modal);
    }
}

static bool activation_is_allowed(const Client *client,
                                  const XClientMessageEvent *event)
{
    long source = event->data.l[0];
    Window requestor = (Window)event->data.l[2];
    Client *parent = client_find(client->transient_for);

    if (source != 1L || wm.focused == NULL) {
        return true;
    }
    return requestor == wm.focused->window || parent == wm.focused ||
           client == wm.focused;
}

static int requested_gravity(const Client *client, unsigned long flags)
{
    int gravity = (int)(flags & 0xffUL);

    if (gravity == 0) {
        XSizeHints hints;
        long supplied = 0L;

        gravity = NorthWestGravity;
        if (XGetWMNormalHints(wm.display,
                              client->window,
                              &hints,
                              &supplied) != 0 &&
            (hints.flags & PWinGravity) != 0L) {
            gravity = hints.win_gravity;
        }
    }
    return gravity;
}

static void apply_moveresize_gravity(const Client *client,
                                     int gravity,
                                     bool x_is_set,
                                     bool y_is_set,
                                     int *x,
                                     int *y)
{
    int horizontal = 2 * decor_border_width(client);
    int vertical = 2 * decor_border_width(client) +
                   decor_title_height(client);

    if (gravity == StaticGravity) {
        if (x_is_set) {
            *x -= decor_border_width(client);
        }
        if (y_is_set) {
            *y -= decor_border_width(client) + decor_title_height(client);
        }
        return;
    }
    if (x_is_set && (gravity == NorthGravity || gravity == CenterGravity ||
                     gravity == SouthGravity)) {
        *x -= horizontal / 2;
    } else if (x_is_set &&
               (gravity == NorthEastGravity || gravity == EastGravity ||
                gravity == SouthEastGravity)) {
        *x -= horizontal;
    }
    if (y_is_set && (gravity == WestGravity || gravity == CenterGravity ||
                     gravity == EastGravity)) {
        *y -= vertical / 2;
    } else if (y_is_set &&
               (gravity == SouthWestGravity || gravity == SouthGravity ||
                gravity == SouthEastGravity)) {
        *y -= vertical;
    }
}

static void handle_moveresize_window(Client *client,
                                     const XClientMessageEvent *event)
{
    unsigned long flags = (unsigned long)event->data.l[0];
    int x = client->x;
    int y = client->y;
    int width = client->width;
    int height = client->height;
    int gravity = requested_gravity(client, flags);

    if (client->maximized || client->fullscreen) {
        return;
    }
    if ((flags & (1UL << 8)) != 0UL) {
        x = (int)event->data.l[1];
    }
    if ((flags & (1UL << 9)) != 0UL) {
        y = (int)event->data.l[2];
    }
    if ((flags & (1UL << 10)) != 0UL) {
        width = (int)event->data.l[3];
    }
    if ((flags & (1UL << 11)) != 0UL) {
        height = (int)event->data.l[4];
    }
    apply_moveresize_gravity(client,
                             gravity,
                             (flags & (1UL << 8)) != 0UL,
                             (flags & (1UL << 9)) != 0UL,
                             &x,
                             &y);
    client_apply_size_hints(client, &width, &height);
    client->x = x;
    client->y = y;
    client->width = width;
    client->height = height;
    decor_apply_geometry(client);
}

bool ewmh_handle_client_message(const XClientMessageEvent *event)
{
    Client *client;

    if (event->format != 32) {
        return false;
    }

    if (event->message_type == wm.atoms.net_current_desktop) {
        unsigned long desktop = (unsigned long)event->data.l[0];

        if (desktop < (unsigned long)wm.desktop_count) {
            desktop_switch((unsigned int)desktop);
        }
        return true;
    }

    if (event->message_type == wm.atoms.net_request_frame_extents) {
        Client *requested = client_find(event->window);

        if (requested != NULL) {
            set_frame_extents(event->window,
                              requested->fullscreen,
                              requested->decor);
        } else {
            Client temporary;

            memset(&temporary, 0, sizeof(temporary));
            temporary.window = event->window;
            hints_initialize_client(&temporary);
            ewmh_read_client_state(&temporary);
            set_frame_extents(event->window,
                              temporary.fullscreen,
                              temporary.decor);
        }
        return true;
    }

    client = client_find(event->window);
    if (client == NULL) {
        return false;
    }

    if (event->message_type == wm.atoms.net_active_window) {
        if (!activation_is_allowed(client, event)) {
            set_client_attention(client, true);
            return true;
        }
        if (!desktop_is_current(client)) {
            desktop_switch(client->desktop);
        }
        if (client->minimized) {
            client_restore(client);
        } else {
            client_focus(client, true);
        }
        return true;
    }

    if (event->message_type == wm.atoms.net_wm_desktop) {
        unsigned long desktop = (unsigned long)event->data.l[0];

        if (desktop == 0xffffffffUL) {
            client->sticky = true;
            ewmh_set_client_desktop(client);
            if (client->mapped && !client->minimized) {
                client_set_visible(client, true);
            }
            ewmh_update_client_state(client);
        } else if (desktop < (unsigned long)wm.desktop_count) {
            client->sticky = false;
            if (client->desktop == (unsigned int)desktop) {
                ewmh_set_client_desktop(client);
                if (!desktop_is_current(client) && client->visible) {
                    client_set_visible(client, false);
                }
                if (wm.focused == client && !desktop_is_current(client)) {
                    client_focus(NULL, true);
                }
            } else {
                desktop_move_client(client, (unsigned int)desktop);
            }
            ewmh_update_client_state(client);
        }
        return true;
    }

    if (event->message_type == wm.atoms.net_close_window) {
        if (event->data.l[0] != 0L) {
            wm.last_event_time = (Time)event->data.l[0];
        }
        client_close(client);
        return true;
    }

    if (event->message_type == wm.atoms.net_moveresize_window) {
        handle_moveresize_window(client, event);
        return true;
    }

    if (event->message_type == wm.atoms.net_wm_moveresize) {
        mouse_begin_moveresize(client,
                               (int)event->data.l[0],
                               (int)event->data.l[1],
                               (int)event->data.l[2],
                               (Time)event->data.l[3]);
        return true;
    }

    if (event->message_type == wm.atoms.net_restack_window) {
        stacking_restack_request(client,
                                 (Window)event->data.l[1],
                                 (int)event->data.l[2]);
        return true;
    }

    if (event->message_type == wm.atoms.net_wm_state) {
        Atom first = (Atom)event->data.l[1];
        Atom second = (Atom)event->data.l[2];
        long action = event->data.l[0];
        bool asks_maximized = first == wm.atoms.net_wm_state_maximized_vert ||
                              first == wm.atoms.net_wm_state_maximized_horz ||
                              second == wm.atoms.net_wm_state_maximized_vert ||
                              second == wm.atoms.net_wm_state_maximized_horz;

        if (asks_maximized) {
            client_set_maximized(client,
                                 state_after_action(action, client->maximized));
        }
        apply_simple_state(client, first, action);
        if (second != first) {
            apply_simple_state(client, second, action);
        }
        ewmh_set_client_desktop(client);
        ewmh_update_client_state(client);
        ewmh_set_allowed_actions(client);
        ewmh_recalculate_workarea();
        stacking_enforce();
        bar_update();
        return true;
    }

    return false;
}
