#include "wm.h"
#include "config.h"

#include <X11/Xatom.h>

#include <limits.h>
#include <string.h>

enum {
    MOTIF_HINTS_DECORATIONS = 1L << 1,
    MOTIF_DECOR_ALL = 1L << 0,
    MOTIF_DECOR_BORDER = 1L << 1,
    MOTIF_DECOR_TITLE = 1L << 3
};

static bool get_atom_list(Window window,
                          Atom property,
                          Atom **atoms,
                          unsigned long *count)
{
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;
    unsigned char *data = NULL;

    *atoms = NULL;
    *count = 0UL;
    if (XGetWindowProperty(wm.display,
                           window,
                           property,
                           0L,
                           64L,
                           False,
                           XA_ATOM,
                           &actual_type,
                           &actual_format,
                           count,
                           &bytes_after,
                           &data) != Success ||
        actual_type != XA_ATOM || actual_format != 32 || data == NULL) {
        if (data != NULL) {
            XFree(data);
        }
        *count = 0UL;
        return false;
    }

    *atoms = (Atom *)data;
    return true;
}

static bool protocol_is_supported(const Atom *protocols,
                                  int count,
                                  Atom wanted)
{
    int index;

    for (index = 0; index < count; ++index) {
        if (protocols[index] == wanted) {
            return true;
        }
    }
    return false;
}

void hints_refresh_transient(Client *client)
{
    Window parent = None;

    if (client == NULL) {
        return;
    }
    if (XGetTransientForHint(wm.display, client->window, &parent) == 0) {
        parent = None;
    }
    client->transient_for = parent;
}

static ClientWindowType atom_to_window_type(Atom atom)
{
    if (atom == wm.atoms.net_wm_window_type_dialog) {
        return CLIENT_TYPE_DIALOG;
    }
    if (atom == wm.atoms.net_wm_window_type_utility) {
        return CLIENT_TYPE_UTILITY;
    }
    if (atom == wm.atoms.net_wm_window_type_toolbar) {
        return CLIENT_TYPE_TOOLBAR;
    }
    if (atom == wm.atoms.net_wm_window_type_menu) {
        return CLIENT_TYPE_MENU;
    }
    if (atom == wm.atoms.net_wm_window_type_dropdown_menu) {
        return CLIENT_TYPE_DROPDOWN_MENU;
    }
    if (atom == wm.atoms.net_wm_window_type_popup_menu) {
        return CLIENT_TYPE_POPUP_MENU;
    }
    if (atom == wm.atoms.net_wm_window_type_tooltip) {
        return CLIENT_TYPE_TOOLTIP;
    }
    if (atom == wm.atoms.net_wm_window_type_notification) {
        return CLIENT_TYPE_NOTIFICATION;
    }
    if (atom == wm.atoms.net_wm_window_type_splash) {
        return CLIENT_TYPE_SPLASH;
    }
    if (atom == wm.atoms.net_wm_window_type_dock) {
        return CLIENT_TYPE_DOCK;
    }
    if (atom == wm.atoms.net_wm_window_type_desktop) {
        return CLIENT_TYPE_DESKTOP;
    }
    return CLIENT_TYPE_NORMAL;
}

void hints_refresh_window_type(Client *client)
{
    Atom *types = NULL;
    unsigned long count = 0UL;
    unsigned long index;

    if (client == NULL) {
        return;
    }

    client->window_type = client->transient_for != None
                              ? CLIENT_TYPE_DIALOG
                              : CLIENT_TYPE_NORMAL;
    if (!get_atom_list(client->window,
                       wm.atoms.net_wm_window_type,
                       &types,
                       &count)) {
        return;
    }

    for (index = 0UL; index < count; ++index) {
        Atom type = types[index];

        if (type == wm.atoms.net_wm_window_type_normal ||
            type == wm.atoms.net_wm_window_type_dialog ||
            type == wm.atoms.net_wm_window_type_utility ||
            type == wm.atoms.net_wm_window_type_toolbar ||
            type == wm.atoms.net_wm_window_type_menu ||
            type == wm.atoms.net_wm_window_type_dropdown_menu ||
            type == wm.atoms.net_wm_window_type_popup_menu ||
            type == wm.atoms.net_wm_window_type_tooltip ||
            type == wm.atoms.net_wm_window_type_notification ||
            type == wm.atoms.net_wm_window_type_splash ||
            type == wm.atoms.net_wm_window_type_dock ||
            type == wm.atoms.net_wm_window_type_desktop) {
            client->window_type = atom_to_window_type(type);
            break;
        }
    }
    XFree(types);
}

void hints_refresh_focus_model(Client *client)
{
    XWMHints *hints;
    Atom *protocols = NULL;
    int protocol_count = 0;

    if (client == NULL) {
        return;
    }

    client->accepts_input = true;
    hints = XGetWMHints(wm.display, client->window);
    if (hints != NULL) {
        if ((hints->flags & InputHint) != 0L) {
            client->accepts_input = hints->input != False;
        }
        XFree(hints);
    }

    client->takes_focus = false;
    if (XGetWMProtocols(wm.display,
                        client->window,
                        &protocols,
                        &protocol_count) != 0) {
        client->takes_focus = protocol_is_supported(protocols,
                                                    protocol_count,
                                                    wm.atoms.wm_take_focus);
    }
    if (protocols != NULL) {
        XFree(protocols);
    }
}

void hints_refresh_urgency(Client *client)
{
    XWMHints *hints;

    if (client == NULL) {
        return;
    }

    client->urgent = false;
    hints = XGetWMHints(wm.display, client->window);
    if (hints != NULL) {
        client->urgent = (hints->flags & XUrgencyHint) != 0L;
        XFree(hints);
    }
}

static ClientDecor default_decor_for_type(ClientWindowType type)
{
    switch (type) {
    case CLIENT_TYPE_NORMAL:
    case CLIENT_TYPE_DIALOG:
    case CLIENT_TYPE_UTILITY:
        return CLIENT_DECOR_FULL;
    case CLIENT_TYPE_TOOLBAR:
        return CLIENT_DECOR_BORDER;
    case CLIENT_TYPE_MENU:
    case CLIENT_TYPE_DROPDOWN_MENU:
    case CLIENT_TYPE_POPUP_MENU:
    case CLIENT_TYPE_TOOLTIP:
    case CLIENT_TYPE_NOTIFICATION:
    case CLIENT_TYPE_SPLASH:
    case CLIENT_TYPE_DOCK:
    case CLIENT_TYPE_DESKTOP:
        return CLIENT_DECOR_NONE;
    }
    return CLIENT_DECOR_FULL;
}

ClientDecor hints_requested_decor(const Client *client)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *data = NULL;
    ClientDecor requested;

    if (client == NULL) {
        return CLIENT_DECOR_FULL;
    }
    requested = default_decor_for_type(client->window_type);

    if (XGetWindowProperty(wm.display,
                           client->window,
                           wm.atoms.motif_wm_hints,
                           0L,
                           5L,
                           False,
                           wm.atoms.motif_wm_hints,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &data) == Success &&
        actual_type == wm.atoms.motif_wm_hints && actual_format == 32 &&
        item_count >= 3UL && data != NULL) {
        const unsigned long *motif = (const unsigned long *)data;
        unsigned long flags = motif[0];
        unsigned long decorations = motif[2];

        if ((flags & MOTIF_HINTS_DECORATIONS) != 0UL) {
            if (decorations == 0UL) {
                requested = CLIENT_DECOR_NONE;
            } else if ((decorations & (MOTIF_DECOR_ALL | MOTIF_DECOR_TITLE)) != 0UL) {
                requested = CLIENT_DECOR_FULL;
            } else if ((decorations & MOTIF_DECOR_BORDER) != 0UL) {
                requested = CLIENT_DECOR_BORDER;
            }
        }
    }
    if (data != NULL) {
        XFree(data);
    }
    return requested;
}

bool hints_position_is_explicit(const Client *client)
{
    XSizeHints hints;
    long supplied = 0L;

    if (client == NULL) {
        return false;
    }
    return XGetWMNormalHints(wm.display,
                             client->window,
                             &hints,
                             &supplied) != 0 &&
           (hints.flags & (USPosition | PPosition)) != 0L;
}

static int clamp_dimension(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int snap_down(int value, int base, int increment)
{
    if (increment <= 0 || value <= base) {
        return value;
    }
    return base + ((value - base) / increment) * increment;
}

static int snap_up(int value, int base, int increment)
{
    long long distance;
    long long result;

    if (increment <= 0 || value <= base) {
        return value;
    }
    distance = (long long)value - base;
    result = (long long)base +
             ((distance + increment - 1) / increment) * increment;
    return result > INT_MAX ? INT_MAX : (int)result;
}

void hints_apply_size(Client *client, int *width, int *height)
{
    XSizeHints hints;
    long supplied = 0L;
    int minimum_width = AWM_MIN_CLIENT_WIDTH;
    int minimum_height = AWM_MIN_CLIENT_HEIGHT;
    int maximum_width = INT_MAX;
    int maximum_height = INT_MAX;
    int base_width = 0;
    int base_height = 0;
    int adjusted_width;
    int adjusted_height;
    int width_increment = 1;
    int height_increment = 1;

    if (client == NULL || width == NULL || height == NULL) {
        return;
    }
    if (XGetWMNormalHints(wm.display, client->window, &hints, &supplied) == 0) {
        *width = clamp_dimension(*width, minimum_width, maximum_width);
        *height = clamp_dimension(*height, minimum_height, maximum_height);
        return;
    }

    if ((hints.flags & PMinSize) != 0L) {
        if (hints.min_width > minimum_width) {
            minimum_width = hints.min_width;
        }
        if (hints.min_height > minimum_height) {
            minimum_height = hints.min_height;
        }
    }
    if ((hints.flags & PMaxSize) != 0L) {
        if (hints.max_width > 0) {
            maximum_width = hints.max_width;
        }
        if (hints.max_height > 0) {
            maximum_height = hints.max_height;
        }
    }
    if (maximum_width < minimum_width) {
        maximum_width = minimum_width;
    }
    if (maximum_height < minimum_height) {
        maximum_height = minimum_height;
    }

    if ((hints.flags & PBaseSize) != 0L) {
        base_width = hints.base_width;
        base_height = hints.base_height;
    } else if ((hints.flags & PMinSize) != 0L) {
        base_width = hints.min_width;
        base_height = hints.min_height;
    }

    adjusted_width = clamp_dimension(*width, minimum_width, maximum_width);
    adjusted_height = clamp_dimension(*height, minimum_height, maximum_height);

    if ((hints.flags & PResizeInc) != 0L) {
        if (hints.width_inc > 0) {
            width_increment = hints.width_inc;
        }
        if (hints.height_inc > 0) {
            height_increment = hints.height_inc;
        }
        adjusted_width = snap_down(adjusted_width,
                                   base_width,
                                   width_increment);
        adjusted_height = snap_down(adjusted_height,
                                    base_height,
                                    height_increment);
    }

    if ((hints.flags & PAspect) != 0L &&
        hints.min_aspect.x > 0 && hints.min_aspect.y > 0 &&
        hints.max_aspect.x > 0 && hints.max_aspect.y > 0) {
        long long content_width = adjusted_width > base_width
                                      ? (long long)adjusted_width - base_width
                                      : 1LL;
        long long content_height = adjusted_height > base_height
                                       ? (long long)adjusted_height - base_height
                                       : 1LL;

        if (content_width * hints.min_aspect.y <
            content_height * hints.min_aspect.x) {
            long long required_content =
                (content_height * hints.min_aspect.x + hints.min_aspect.y - 1) /
                hints.min_aspect.y;
            long long required_total = required_content + base_width;
            int required = snap_up(required_total > INT_MAX
                                       ? INT_MAX
                                       : (int)required_total,
                                   base_width,
                                   width_increment);

            if (required <= maximum_width) {
                adjusted_width = required;
            } else {
                content_width = (long long)adjusted_width - base_width;
                adjusted_height = snap_down(
                    base_height +
                        (int)(content_width * hints.min_aspect.y /
                              hints.min_aspect.x),
                    base_height,
                    height_increment);
            }
        } else if (content_width * hints.max_aspect.y >
                   content_height * hints.max_aspect.x) {
            long long required_content =
                (content_width * hints.max_aspect.y + hints.max_aspect.x - 1) /
                hints.max_aspect.x;
            long long required_total = required_content + base_height;
            int required = snap_up(required_total > INT_MAX
                                       ? INT_MAX
                                       : (int)required_total,
                                   base_height,
                                   height_increment);

            if (required <= maximum_height) {
                adjusted_height = required;
            } else {
                content_height = (long long)adjusted_height - base_height;
                adjusted_width = snap_down(
                    base_width +
                        (int)(content_height * hints.max_aspect.x /
                              hints.max_aspect.y),
                    base_width,
                    width_increment);
            }
        }
    }

    *width = clamp_dimension(adjusted_width, minimum_width, maximum_width);
    *height = clamp_dimension(adjusted_height, minimum_height, maximum_height);
}

void hints_initialize_client(Client *client)
{
    if (client == NULL) {
        return;
    }

    hints_refresh_transient(client);
    hints_refresh_window_type(client);
    hints_refresh_focus_model(client);
    hints_refresh_urgency(client);
    client->position_explicit = hints_position_is_explicit(client);
    client->decor = hints_requested_decor(client);
}
