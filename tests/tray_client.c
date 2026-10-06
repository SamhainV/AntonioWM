#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    SYSTEM_TRAY_REQUEST_DOCK = 0,
    XEMBED_EMBEDDED_NOTIFY = 0,
    XEMBED_MAPPED = 1
};

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t toggle_mapping_requested;

static void stop_client(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static void toggle_mapping(int signal_number)
{
    (void)signal_number;
    toggle_mapping_requested = 1;
}

static bool parse_size(const char *text, unsigned int *size)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1L ||
        value > 1000L) {
        return false;
    }
    *size = (unsigned int)value;
    return true;
}

static bool write_window_id(const char *path, Window window)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return false;
    }
    (void)fprintf(file, "0x%lx\n", window);
    return fclose(file) == 0;
}

static void append_log(FILE *log, const char *name, unsigned long value)
{
    if (log == NULL) {
        return;
    }
    (void)fprintf(log, "%s=0x%lx\n", name, value);
    (void)fflush(log);
}

static Atom tray_selection(Display *display, int screen)
{
    char name[64];

    (void)snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", screen);
    return XInternAtom(display, name, False);
}

static void send_manager(Display *display,
                         Window root,
                         Atom manager,
                         Atom selection,
                         Window owner)
{
    XEvent event;

    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = root;
    event.xclient.message_type = manager;
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)CurrentTime;
    event.xclient.data.l[1] = (long)selection;
    event.xclient.data.l[2] = (long)owner;
    XSendEvent(display, root, False, StructureNotifyMask, &event);
    XFlush(display);
}

static int run_manager(Display *display,
                       int screen,
                       const char *id_path)
{
    Window root = RootWindow(display, screen);
    Atom selection = tray_selection(display, screen);
    Atom manager = XInternAtom(display, "MANAGER", False);
    Window owner;

    if (XGetSelectionOwner(display, selection) != None) {
        (void)fprintf(stderr, "selection already owned\n");
        return EXIT_FAILURE;
    }
    owner = XCreateSimpleWindow(display, root, 0, 0, 1U, 1U, 0U, 0UL, 0UL);
    if (owner == None || !write_window_id(id_path, owner)) {
        return EXIT_FAILURE;
    }
    XSetSelectionOwner(display, selection, owner, CurrentTime);
    XSync(display, False);
    if (XGetSelectionOwner(display, selection) != owner) {
        XDestroyWindow(display, owner);
        return EXIT_FAILURE;
    }
    send_manager(display, root, manager, selection, owner);

    while (running != 0) {
        (void)poll(NULL, 0U, 50);
    }
    if (XGetSelectionOwner(display, selection) == owner) {
        XSetSelectionOwner(display, selection, None, CurrentTime);
    }
    XDestroyWindow(display, owner);
    XSync(display, False);
    return EXIT_SUCCESS;
}

static void set_xembed_info(Display *display,
                            Window window,
                            Atom xembed_info,
                            bool mapped)
{
    unsigned long values[2];

    values[0] = 0UL;
    values[1] = mapped ? XEMBED_MAPPED : 0UL;
    XChangeProperty(display,
                    window,
                    xembed_info,
                    xembed_info,
                    32,
                    PropModeReplace,
                    (const unsigned char *)values,
                    2);
}

static bool request_dock(Display *display,
                         Atom selection,
                         Atom opcode,
                         Window icon)
{
    Window owner = XGetSelectionOwner(display, selection);
    XEvent event;

    if (owner == None) {
        return false;
    }
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = owner;
    event.xclient.message_type = opcode;
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)CurrentTime;
    event.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
    event.xclient.data.l[2] = (long)icon;
    XSendEvent(display, owner, False, NoEventMask, &event);
    XFlush(display);
    return true;
}

static void draw_icon(Display *display, Window window, GC gc)
{
    XWindowAttributes attributes;
    int inset;

    if (XGetWindowAttributes(display, window, &attributes) == 0) {
        return;
    }
    XSetForeground(display, gc, 0x46698aUL);
    XFillRectangle(display,
                   window,
                   gc,
                   0,
                   0,
                   (unsigned int)attributes.width,
                   (unsigned int)attributes.height);
    inset = attributes.width < attributes.height ? attributes.width / 4
                                                  : attributes.height / 4;
    XSetForeground(display, gc, 0xe6edf3UL);
    XFillRectangle(display,
                   window,
                   gc,
                   inset,
                   inset,
                   (unsigned int)(attributes.width - 2 * inset),
                   (unsigned int)(attributes.height - 2 * inset));
}

static Window toggle_popup(Display *display,
                           int screen,
                           Window icon,
                           Window popup)
{
    Window root = RootWindow(display, screen);
    Window child;
    int root_x = 0;
    int root_y = 0;
    XSetWindowAttributes attributes;
    Atom window_type;
    Atom popup_type;

    if (popup != None) {
        XDestroyWindow(display, popup);
        return None;
    }
    (void)XTranslateCoordinates(display,
                                icon,
                                root,
                                0,
                                0,
                                &root_x,
                                &root_y,
                                &child);
    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = 0x30363cUL;
    attributes.event_mask = ExposureMask | ButtonPressMask;
    popup = XCreateWindow(display,
                          root,
                          root_x - 110,
                          root_y - 74,
                          132U,
                          68U,
                          1U,
                          CopyFromParent,
                          InputOutput,
                          CopyFromParent,
                          CWOverrideRedirect | CWBackPixel | CWEventMask,
                          &attributes);
    XStoreName(display, popup, "AWM Tray Test Popup");
    window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    popup_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    XChangeProperty(display,
                    popup,
                    window_type,
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&popup_type,
                    1);
    XMapRaised(display, popup);
    return popup;
}

static int run_icon(Display *display,
                    int screen,
                    const char *id_path,
                    const char *log_path,
                    const char *name,
                    unsigned int size)
{
    Window root = RootWindow(display, screen);
    Atom selection = tray_selection(display, screen);
    Atom manager = XInternAtom(display, "MANAGER", False);
    Atom opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    Atom xembed = XInternAtom(display, "_XEMBED", False);
    Atom xembed_info = XInternAtom(display, "_XEMBED_INFO", False);
    Window icon;
    Window popup = None;
    XClassHint class_hint;
    GC gc;
    FILE *log;
    bool mapped = true;
    bool docked = false;

    icon = XCreateSimpleWindow(display,
                               root,
                               0,
                               0,
                               size,
                               size,
                               0U,
                               BlackPixel(display, screen),
                               0x46698aUL);
    if (icon == None || !write_window_id(id_path, icon)) {
        return EXIT_FAILURE;
    }
    XStoreName(display, icon, name);
    class_hint.res_name = (char *)"awm-tray-test";
    class_hint.res_class = (char *)"AntonioWMTrayTest";
    XSetClassHint(display, icon, &class_hint);
    set_xembed_info(display, icon, xembed_info, mapped);
    XSelectInput(display,
                 root,
                 StructureNotifyMask);
    XSelectInput(display,
                 icon,
                 ExposureMask | ButtonPressMask | StructureNotifyMask |
                     PropertyChangeMask);
    gc = XCreateGC(display, icon, 0UL, NULL);
    log = fopen(log_path, "a");
    if (log == NULL) {
        XFreeGC(display, gc);
        XDestroyWindow(display, icon);
        return EXIT_FAILURE;
    }
    append_log(log,
               "initial-owner",
               (unsigned long)XGetSelectionOwner(display, selection));
    if (request_dock(display, selection, opcode, icon)) {
        append_log(log,
                   "dock-request",
                   (unsigned long)XGetSelectionOwner(display, selection));
    }

    while (running != 0) {
        struct pollfd descriptor;

        while (XPending(display) > 0) {
            XEvent event;

            XNextEvent(display, &event);
            if (event.type == Expose && event.xexpose.window == icon &&
                event.xexpose.count == 0) {
                draw_icon(display, icon, gc);
            } else if (event.type == ButtonPress &&
                       event.xbutton.window == icon) {
                append_log(log, "button", event.xbutton.button);
                if (event.xbutton.button == Button1) {
                    popup = toggle_popup(display, screen, icon, popup);
                    XFlush(display);
                }
            } else if (event.type == ButtonPress &&
                       event.xbutton.window == popup) {
                XDestroyWindow(display, popup);
                popup = None;
            } else if (event.type == ReparentNotify &&
                       event.xreparent.window == icon) {
                docked = event.xreparent.parent != root;
                append_log(log,
                           docked ? "reparent-container" : "reparent-root",
                           event.xreparent.parent);
            } else if (event.type == ClientMessage &&
                       event.xclient.message_type == manager &&
                       (Atom)event.xclient.data.l[1] == selection) {
                append_log(log,
                           "manager-owner",
                           (unsigned long)event.xclient.data.l[2]);
                if (!docked) {
                    if (request_dock(display, selection, opcode, icon)) {
                        append_log(log,
                                   "dock-request",
                                   (unsigned long)XGetSelectionOwner(display,
                                                                     selection));
                    }
                }
            } else if (event.type == ClientMessage &&
                       event.xclient.window == icon &&
                       event.xclient.message_type == xembed &&
                       event.xclient.data.l[1] == XEMBED_EMBEDDED_NOTIFY) {
                append_log(log,
                           "embedded-container",
                           (unsigned long)event.xclient.data.l[3]);
            } else if (event.type == DestroyNotify &&
                       event.xdestroywindow.window == popup) {
                popup = None;
            }
        }
        if (toggle_mapping_requested != 0) {
            toggle_mapping_requested = 0;
            mapped = !mapped;
            set_xembed_info(display, icon, xembed_info, mapped);
            XFlush(display);
        }
        descriptor.fd = ConnectionNumber(display);
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        if (poll(&descriptor, 1U, 100) < 0 && errno != EINTR) {
            break;
        }
    }

    if (popup != None) {
        XDestroyWindow(display, popup);
    }
    (void)fclose(log);
    XFreeGC(display, gc);
    XDestroyWindow(display, icon);
    XSync(display, False);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    Display *display;
    int screen;
    int result;
    struct sigaction action;

    if (argc < 2) {
        (void)fprintf(stderr,
                      "usage: %s query | manager IDFILE | "
                      "icon IDFILE LOG NAME SIZE\n",
                      argv[0]);
        return EXIT_FAILURE;
    }
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        return EXIT_FAILURE;
    }
    screen = DefaultScreen(display);

    if (strcmp(argv[1], "query") == 0 && argc == 2) {
        Window owner = XGetSelectionOwner(display,
                                          tray_selection(display, screen));

        if (owner != None) {
            (void)printf("0x%lx\n", owner);
        }
        XCloseDisplay(display);
        return owner != None ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_client;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    action.sa_handler = toggle_mapping;
    (void)sigaction(SIGHUP, &action, NULL);

    if (strcmp(argv[1], "manager") == 0 && argc == 3) {
        result = run_manager(display, screen, argv[2]);
    } else if (strcmp(argv[1], "icon") == 0 && argc == 6) {
        unsigned int size;

        if (!parse_size(argv[5], &size)) {
            XCloseDisplay(display);
            return EXIT_FAILURE;
        }
        result = run_icon(display,
                          screen,
                          argv[2],
                          argv[3],
                          argv[4],
                          size);
    } else {
        result = EXIT_FAILURE;
    }
    XCloseDisplay(display);
    return result;
}
