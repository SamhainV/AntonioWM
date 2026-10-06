#include "wm.h"
#include "config.h"

#include <X11/Xatom.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SYSTEM_TRAY_REQUEST_DOCK = 0,
    SYSTEM_TRAY_BEGIN_MESSAGE = 1,
    SYSTEM_TRAY_CANCEL_MESSAGE = 2
};

enum {
    XEMBED_EMBEDDED_NOTIFY = 0,
    XEMBED_WINDOW_ACTIVATE = 1,
    XEMBED_WINDOW_DEACTIVATE = 2,
    XEMBED_REQUEST_FOCUS = 3,
    XEMBED_FOCUS_IN = 4,
    XEMBED_FOCUS_CURRENT = 0,
    XEMBED_MAPPED = 1
};

#define XEMBED_VERSION 0UL

typedef struct TrayIcon TrayIcon;

struct TrayIcon {
    Window window;
    Window container;
    Colormap colormap;
    bool owns_colormap;
    bool mapped;
    unsigned int ignore_unmap;
    unsigned long xembed_version;
    TrayIcon *previous;
    TrayIcon *next;
};

typedef struct {
    Atom selection;
    Atom opcode;
    Atom message_data;
    Atom orientation;
    Atom visual;
    Atom xembed;
    Atom xembed_info;
    Atom timestamp;
    Window owner;
    TrayIcon *icons;
    TrayIcon *icons_tail;
    bool active;
    bool cleaning;
} TrayState;

static TrayState tray;

static TrayIcon *find_icon(Window window)
{
    TrayIcon *icon;

    for (icon = tray.icons; icon != NULL; icon = icon->next) {
        if (icon->window == window || icon->container == window) {
            return icon;
        }
    }
    return NULL;
}

static TrayIcon *find_client_icon(Window window)
{
    TrayIcon *icon = find_icon(window);

    return icon != NULL && icon->window == window ? icon : NULL;
}

static void append_icon(TrayIcon *icon)
{
    icon->previous = tray.icons_tail;
    icon->next = NULL;
    if (tray.icons_tail != NULL) {
        tray.icons_tail->next = icon;
    } else {
        tray.icons = icon;
    }
    tray.icons_tail = icon;
}

static void detach_icon(TrayIcon *icon)
{
    if (icon->previous != NULL) {
        icon->previous->next = icon->next;
    } else {
        tray.icons = icon->next;
    }
    if (icon->next != NULL) {
        icon->next->previous = icon->previous;
    } else {
        tray.icons_tail = icon->previous;
    }
}

static size_t mapped_icon_count(void)
{
    const TrayIcon *icon;
    size_t count = 0U;

    for (icon = tray.icons; icon != NULL; icon = icon->next) {
        if (icon->mapped) {
            ++count;
        }
    }
    return count;
}

static int natural_icon_size(int panel_height)
{
    int available = panel_height - 2 * wm.config.taskbar_padding;

    if (available < 1) {
        available = 1;
    }
    if (available > AWM_TRAY_ICON_MAX_SIZE) {
        available = AWM_TRAY_ICON_MAX_SIZE;
    }
    return available;
}

int tray_preferred_width(int panel_height)
{
    size_t count = mapped_icon_count();
    long long width;

    if (!tray.active || count == 0U) {
        return 0;
    }
    width = (long long)count * natural_icon_size(panel_height) +
            (long long)(count - 1U) * AWM_TRAY_ICON_GAP;
    return width > INT_MAX ? INT_MAX : (int)width;
}

static bool read_xembed_info(Window window,
                             unsigned long *version,
                             unsigned long *flags)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *data = NULL;
    bool valid = false;

    if (XGetWindowProperty(wm.display,
                           window,
                           tray.xembed_info,
                           0L,
                           2L,
                           False,
                           tray.xembed_info,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &data) == Success &&
        actual_type == tray.xembed_info && actual_format == 32 &&
        item_count >= 2UL && data != NULL) {
        const unsigned long *values = (const unsigned long *)data;

        *version = values[0] & 0xffffffffUL;
        *flags = values[1] & 0xffffffffUL;
        valid = true;
    }
    if (data != NULL) {
        XFree(data);
    }
    return valid;
}

static void send_xembed(TrayIcon *icon,
                        long message,
                        long detail,
                        long data1,
                        long data2)
{
    XEvent event;

    if (icon == NULL || icon->window == None) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = wm.display;
    event.xclient.window = icon->window;
    event.xclient.message_type = tray.xembed;
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)wm.last_event_time;
    event.xclient.data.l[1] = message;
    event.xclient.data.l[2] = detail;
    event.xclient.data.l[3] = data1;
    event.xclient.data.l[4] = data2;
    XSendEvent(wm.display, icon->window, False, NoEventMask, &event);
}

static void set_icon_mapping(TrayIcon *icon, bool mapped)
{
    if (icon == NULL || icon->mapped == mapped) {
        return;
    }
    icon->mapped = mapped;
    taskbar_update();
}

static void refresh_xembed_info(TrayIcon *icon)
{
    unsigned long version = XEMBED_VERSION;
    unsigned long flags = XEMBED_MAPPED;
    bool valid;

    if (icon == NULL) {
        return;
    }
    valid = read_xembed_info(icon->window, &version, &flags);
    icon->xembed_version = valid ? version : XEMBED_VERSION;
    set_icon_mapping(icon, !valid || (flags & XEMBED_MAPPED) != 0UL);
}

static void release_icon(TrayIcon *icon, bool destroyed, bool reparent)
{
    Window client;
    Window container;
    Colormap colormap;
    bool owns_colormap;

    if (icon == NULL) {
        return;
    }
    client = icon->window;
    container = icon->container;
    colormap = icon->colormap;
    owns_colormap = icon->owns_colormap;
    detach_icon(icon);
    free(icon);

    if (!destroyed && client != None) {
        TrayIcon temporary;

        memset(&temporary, 0, sizeof(temporary));
        temporary.window = client;
        send_xembed(&temporary, XEMBED_WINDOW_DEACTIVATE, 0L, 0L, 0L);
        XSelectInput(wm.display, client, NoEventMask);
        XUnmapWindow(wm.display, client);
        XRemoveFromSaveSet(wm.display, client);
        if (reparent) {
            XReparentWindow(wm.display, client, wm.root, 0, 0);
        }
    }
    if (container != None) {
        XDestroyWindow(wm.display, container);
    }
    if (owns_colormap && colormap != None) {
        XFreeColormap(wm.display, colormap);
    }
    if (!tray.cleaning) {
        taskbar_update();
    }
}

static void release_all_icons(void)
{
    TrayIcon *icon = tray.icons;

    while (icon != NULL) {
        TrayIcon *next = icon->next;

        release_icon(icon, false, true);
        icon = next;
    }
}

static Time server_timestamp(void)
{
    XEvent event;
    unsigned char value = 0U;

    XChangeProperty(wm.display,
                    tray.owner,
                    tray.timestamp,
                    XA_INTEGER,
                    8,
                    PropModeAppend,
                    &value,
                    1);
    XWindowEvent(wm.display, tray.owner, PropertyChangeMask, &event);
    return event.xproperty.time;
}

static void publish_manager(Time timestamp)
{
    XEvent event;

    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = wm.display;
    event.xclient.window = wm.root;
    event.xclient.message_type = wm.atoms.manager;
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)timestamp;
    event.xclient.data.l[1] = (long)tray.selection;
    event.xclient.data.l[2] = (long)tray.owner;
    XSendEvent(wm.display,
               wm.root,
               False,
               StructureNotifyMask,
               &event);
}

static void disable_tray(bool release_selection)
{
    Window owner = tray.owner;

    tray.cleaning = true;
    release_all_icons();
    tray.active = false;
    tray.owner = None;
    if (owner != None) {
        if (release_selection &&
            XGetSelectionOwner(wm.display, tray.selection) == owner) {
            XSetSelectionOwner(wm.display,
                               tray.selection,
                               None,
                               CurrentTime);
        }
        XDestroyWindow(wm.display, owner);
    }
    tray.cleaning = false;
    if (wm.taskbar_window != None) {
        taskbar_update();
    }
}

void tray_initialize(void)
{
    char selection_name[64];
    Window current_owner;
    XSetWindowAttributes attributes;
    unsigned long mask;
    unsigned long orientation = 0UL;
    unsigned long visual_id;
    Time timestamp;

    memset(&tray, 0, sizeof(tray));
    if (wm.taskbar_window == None) {
        return;
    }

    (void)snprintf(selection_name,
                   sizeof(selection_name),
                   "_NET_SYSTEM_TRAY_S%d",
                   wm.screen);
    tray.selection = XInternAtom(wm.display, selection_name, False);
    tray.opcode = XInternAtom(wm.display, "_NET_SYSTEM_TRAY_OPCODE", False);
    tray.message_data =
        XInternAtom(wm.display, "_NET_SYSTEM_TRAY_MESSAGE_DATA", False);
    tray.orientation =
        XInternAtom(wm.display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    tray.visual = XInternAtom(wm.display, "_NET_SYSTEM_TRAY_VISUAL", False);
    tray.xembed = XInternAtom(wm.display, "_XEMBED", False);
    tray.xembed_info = XInternAtom(wm.display, "_XEMBED_INFO", False);
    tray.timestamp = XInternAtom(wm.display, "_ANTONIOWM_TRAY_TIMESTAMP", False);

    current_owner = XGetSelectionOwner(wm.display, tray.selection);
    if (current_owner != None) {
        fprintf(stderr,
                "%s: la seleccion %s ya pertenece a 0x%lx; "
                "la bandeja del sistema queda desactivada\n",
                AWM_NAME,
                selection_name,
                current_owner);
        return;
    }

    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.event_mask = PropertyChangeMask | StructureNotifyMask;
    mask = CWOverrideRedirect | CWEventMask;
    tray.owner = XCreateWindow(wm.display,
                               wm.root,
                               -1,
                               -1,
                               1U,
                               1U,
                               0U,
                               CopyFromParent,
                               InputOutput,
                               CopyFromParent,
                               mask,
                               &attributes);
    if (tray.owner == None) {
        fprintf(stderr,
                "%s: no se pudo crear el owner de la bandeja; "
                "AntonioWM continuara sin tray\n",
                AWM_NAME);
        return;
    }
    XStoreName(wm.display, tray.owner, "AntonioWM System Tray");
    XChangeProperty(wm.display,
                    tray.owner,
                    tray.orientation,
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&orientation,
                    1);
    visual_id = (unsigned long)XVisualIDFromVisual(DefaultVisual(wm.display,
                                                                wm.screen));
    XChangeProperty(wm.display,
                    tray.owner,
                    tray.visual,
                    XA_VISUALID,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&visual_id,
                    1);

    timestamp = server_timestamp();
    XSetSelectionOwner(wm.display, tray.selection, tray.owner, timestamp);
    XSync(wm.display, False);
    if (XGetSelectionOwner(wm.display, tray.selection) != tray.owner) {
        fprintf(stderr,
                "%s: no se pudo adquirir %s; AntonioWM continuara sin tray\n",
                AWM_NAME,
                selection_name);
        XDestroyWindow(wm.display, tray.owner);
        tray.owner = None;
        return;
    }

    tray.active = true;
    publish_manager(timestamp);
    XFlush(wm.display);
}

void tray_cleanup(void)
{
    if (tray.owner == None && tray.icons == NULL) {
        memset(&tray, 0, sizeof(tray));
        return;
    }
    disable_tray(true);
    XSync(wm.display, False);
    memset(&tray, 0, sizeof(tray));
}

static Window create_container(const XWindowAttributes *client_attributes,
                               Colormap *colormap,
                               bool *owns_colormap)
{
    XSetWindowAttributes attributes;
    unsigned long mask;
    bool is_default_visual;

    is_default_visual =
        client_attributes->visual == DefaultVisual(wm.display, wm.screen) &&
        client_attributes->depth == DefaultDepth(wm.display, wm.screen);
    if (is_default_visual) {
        *colormap = DefaultColormap(wm.display, wm.screen);
        *owns_colormap = false;
    } else {
        *colormap = XCreateColormap(wm.display,
                                    wm.root,
                                    client_attributes->visual,
                                    AllocNone);
        *owns_colormap = *colormap != None;
        if (!*owns_colormap) {
            return None;
        }
    }

    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = is_default_visual ? wm.bar_background : 0UL;
    attributes.border_pixel = 0UL;
    attributes.colormap = *colormap;
    attributes.event_mask = SubstructureRedirectMask |
                            SubstructureNotifyMask |
                            StructureNotifyMask;
    mask = CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap |
           CWEventMask;
    return XCreateWindow(wm.display,
                         wm.taskbar_window,
                         0,
                         0,
                         1U,
                         1U,
                         0U,
                         client_attributes->depth,
                         InputOutput,
                         client_attributes->visual,
                         mask,
                         &attributes);
}

static void dock_icon(Window window)
{
    XWindowAttributes attributes;
    TrayIcon *icon;
    unsigned long version = XEMBED_VERSION;
    unsigned long flags = XEMBED_MAPPED;
    bool has_xembed_info;

    if (!tray.active || wm.taskbar_window == None || window == None ||
        window == tray.owner || window == wm.taskbar_window ||
        find_icon(window) != NULL) {
        return;
    }
    if (client_find_any(window) != NULL) {
        fprintf(stderr,
                "%s: se ignora la solicitud tray de la ventana gestionada 0x%lx\n",
                AWM_NAME,
                window);
        return;
    }
    if (XGetWindowAttributes(wm.display, window, &attributes) == 0 ||
        attributes.class == InputOnly || attributes.root != wm.root) {
        return;
    }

    icon = calloc(1U, sizeof(*icon));
    if (icon == NULL) {
        fprintf(stderr,
                "%s: memoria insuficiente para alojar el icono tray 0x%lx\n",
                AWM_NAME,
                window);
        return;
    }
    icon->window = window;
    icon->container = create_container(&attributes,
                                       &icon->colormap,
                                       &icon->owns_colormap);
    if (icon->container == None) {
        free(icon);
        return;
    }
    XStoreName(wm.display, icon->container, "AntonioWM Tray Container");

    has_xembed_info = read_xembed_info(window, &version, &flags);
    icon->xembed_version = has_xembed_info ? version : XEMBED_VERSION;
    icon->mapped = !has_xembed_info || (flags & XEMBED_MAPPED) != 0UL;
    if (attributes.map_state != IsUnmapped) {
        icon->ignore_unmap = 1U;
    }
    append_icon(icon);

    XSelectInput(wm.display, window, StructureNotifyMask | PropertyChangeMask);
    XAddToSaveSet(wm.display, window);
    XReparentWindow(wm.display, window, icon->container, 0, 0);
    send_xembed(icon,
                XEMBED_EMBEDDED_NOTIFY,
                0L,
                (long)icon->container,
                (long)XEMBED_VERSION);
    send_xembed(icon, XEMBED_WINDOW_ACTIVATE, 0L, 0L, 0L);
    taskbar_update();
    XSync(wm.display, False);
}

void tray_layout(int x, int width, int panel_height)
{
    TrayIcon *icon;
    size_t count = mapped_icon_count();
    int natural_size = natural_icon_size(panel_height);
    int gap = AWM_TRAY_ICON_GAP;
    long long gaps;
    int icon_size;
    int used;
    int current_x;
    int y;

    for (icon = tray.icons; icon != NULL; icon = icon->next) {
        if (!icon->mapped) {
            XUnmapWindow(wm.display, icon->container);
        }
    }
    if (!tray.active || count == 0U || width <= 0 || panel_height <= 0) {
        return;
    }

    gaps = (long long)(count - 1U) * gap;
    if (gaps >= width) {
        gap = count > 1U ? (width - 1) / (int)(count - 1U) : 0;
        if (gap < 0) {
            gap = 0;
        }
        gaps = (long long)(count - 1U) * gap;
    }
    icon_size = (int)(((long long)width - gaps) / (long long)count);
    if (icon_size > natural_size) {
        icon_size = natural_size;
    }
    if (icon_size < 1) {
        icon_size = 1;
    }
    used = (int)((long long)icon_size * (long long)count + gaps);
    current_x = x + (width > used ? width - used : 0);
    y = (panel_height - icon_size) / 2;

    for (icon = tray.icons; icon != NULL; icon = icon->next) {
        if (!icon->mapped) {
            continue;
        }
        XMoveResizeWindow(wm.display,
                          icon->container,
                          current_x,
                          y,
                          (unsigned int)icon_size,
                          (unsigned int)icon_size);
        XMoveResizeWindow(wm.display,
                          icon->window,
                          0,
                          0,
                          (unsigned int)icon_size,
                          (unsigned int)icon_size);
        XMapWindow(wm.display, icon->window);
        XMapRaised(wm.display, icon->container);
        current_x += icon_size + gap;
    }
}

static bool handle_client_message(const XClientMessageEvent *event)
{
    TrayIcon *icon;

    if (event->message_type == tray.opcode && event->format == 32 &&
        event->window == tray.owner) {
        if (event->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
            dock_icon((Window)event->data.l[2]);
        } else if (event->data.l[1] == SYSTEM_TRAY_BEGIN_MESSAGE ||
                   event->data.l[1] == SYSTEM_TRAY_CANCEL_MESSAGE) {
            /* Balloon messages are optional and intentionally not rendered. */
        }
        return true;
    }
    if (event->message_type != tray.xembed || event->format != 32) {
        return false;
    }
    icon = find_icon(event->window);
    if (icon == NULL || event->data.l[1] != XEMBED_REQUEST_FOCUS) {
        return icon != NULL;
    }
    XSetInputFocus(wm.display,
                   icon->window,
                   RevertToParent,
                   wm.last_event_time != CurrentTime
                       ? wm.last_event_time
                       : CurrentTime);
    send_xembed(icon, XEMBED_FOCUS_IN, XEMBED_FOCUS_CURRENT, 0L, 0L);
    return true;
}

static bool handle_unmap(const XUnmapEvent *event)
{
    TrayIcon *icon = find_client_icon(event->window);
    unsigned long version;
    unsigned long flags;

    if (icon == NULL) {
        return false;
    }
    if (icon->ignore_unmap > 0U) {
        --icon->ignore_unmap;
        return true;
    }
    if (read_xembed_info(icon->window, &version, &flags) &&
        (flags & XEMBED_MAPPED) != 0UL) {
        icon->xembed_version = version;
        XMapWindow(wm.display, icon->window);
        return true;
    }
    set_icon_mapping(icon, false);
    return true;
}

bool tray_handle_event(XEvent *event)
{
    TrayIcon *icon;

    if (event == NULL || tray.cleaning) {
        return false;
    }
    switch (event->type) {
    case ClientMessage:
        return handle_client_message(&event->xclient);
    case MapRequest:
        icon = find_client_icon(event->xmaprequest.window);
        if (icon != NULL) {
            refresh_xembed_info(icon);
            if (icon->mapped) {
                taskbar_update();
            }
            return true;
        }
        break;
    case ConfigureRequest:
        icon = find_client_icon(event->xconfigurerequest.window);
        if (icon != NULL) {
            taskbar_update();
            return true;
        }
        break;
    case DestroyNotify:
        icon = find_icon(event->xdestroywindow.window);
        if (icon != NULL) {
            release_icon(icon, true, false);
            return true;
        }
        break;
    case UnmapNotify:
        return handle_unmap(&event->xunmap);
    case MapNotify:
        icon = find_client_icon(event->xmap.window);
        if (icon != NULL) {
            if (!icon->mapped) {
                XUnmapWindow(wm.display, icon->window);
            }
            return true;
        }
        if (find_icon(event->xmap.window) != NULL) {
            return true;
        }
        break;
    case PropertyNotify:
        icon = find_client_icon(event->xproperty.window);
        if (icon != NULL && event->xproperty.atom == tray.xembed_info) {
            wm.last_event_time = event->xproperty.time;
            refresh_xembed_info(icon);
            return true;
        }
        break;
    case ReparentNotify:
        icon = find_client_icon(event->xreparent.window);
        if (icon != NULL) {
            if (event->xreparent.parent != icon->container) {
                release_icon(icon, false, false);
            }
            return true;
        }
        break;
    case SelectionClear:
        if (event->xselectionclear.selection == tray.selection &&
            event->xselectionclear.window == tray.owner) {
            fprintf(stderr,
                    "%s: se perdio la seleccion de la bandeja; "
                    "AntonioWM continuara sin tray\n",
                    AWM_NAME);
            disable_tray(false);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}
