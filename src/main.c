#include "wm.h"
#include "config.h"

#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>

#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

WM wm;

static bool another_wm_detected;
static volatile sig_atomic_t stop_requested;

static void request_clean_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int startup_xerror(Display *display, XErrorEvent *error)
{
    (void)display;

    if (error->error_code == BadAccess) {
        another_wm_detected = true;
    }

    return 0;
}

static int runtime_xerror(Display *display, XErrorEvent *error)
{
    char message[256];

    /* Windows can disappear between an event and its asynchronous request. */
    if (error->error_code == BadWindow || error->error_code == BadDrawable ||
        error->error_code == BadMatch || error->error_code == BadAccess) {
        return 0;
    }

    XGetErrorText(display, error->error_code, message, (int)sizeof(message));
    fprintf(stderr,
            "%s: error X11: %s (peticion=%u, codigo_menor=%u, recurso=0x%lx)\n",
            AWM_NAME,
            message,
            (unsigned int)error->request_code,
            (unsigned int)error->minor_code,
            error->resourceid);
    return 0;
}

static unsigned long allocate_color(const char *name, unsigned long fallback)
{
    XColor exact;
    XColor screen;
    Colormap colormap = DefaultColormap(wm.display, wm.screen);

    if (XAllocNamedColor(wm.display, colormap, name, &screen, &exact) == 0) {
        fprintf(stderr,
                "%s: no se pudo reservar el color '%s'; se usara el color de respaldo\n",
                AWM_NAME,
                name);
        return fallback;
    }

    return screen.pixel;
}

static void prime_randr_state(void)
{
    int event_base;
    int error_base;
    int major = 1;
    int minor = 5;
    int monitor_count = 0;
    XRRMonitorInfo *monitors;

    /*
     * Xephyr can defer creation of its RandR monitor objects until the first
     * RandR 1.5 monitor query.  If a GTK application is that first client,
     * the resulting screen/output notifications may invalidate the borrowed
     * GdkMonitor selected during its startup.  Query once here so clients see
     * a stable monitor topology from their first connection onward.
     */
    if (XRRQueryExtension(wm.display, &event_base, &error_base) == 0 ||
        XRRQueryVersion(wm.display, &major, &minor) == 0 ||
        major < 1 || (major == 1 && minor < 5)) {
        return;
    }

    monitors = XRRGetMonitors(wm.display, wm.root, True, &monitor_count);
    if (monitors != NULL) {
        XRRFreeMonitors(monitors);
    }
    XSync(wm.display, False);
}

static void scan_existing_windows(void)
{
    Window root_return;
    Window parent_return;
    Window *children = NULL;
    unsigned int child_count = 0U;
    unsigned int index;

    XGrabServer(wm.display);

    if (XQueryTree(wm.display,
                   wm.root,
                   &root_return,
                   &parent_return,
                   &children,
                   &child_count) != 0) {
        for (index = 0U; index < child_count; ++index) {
            XWindowAttributes attributes;

            if (XGetWindowAttributes(wm.display, children[index], &attributes) == 0) {
                continue;
            }

            if (!attributes.override_redirect && attributes.class != InputOnly &&
                attributes.map_state == IsViewable) {
                client_manage(children[index], false);
            }
        }
    }

    if (children != NULL) {
        XFree(children);
    }

    {
        Client *client;

        /* A transient can precede its parent in the root stacking list after a
         * hot restart.  Resolve the complete family once every client exists. */
        for (client = wm.clients; client != NULL; client = client->next) {
            Client *parent;

            hints_refresh_transient(client);
            parent = client_find(client->transient_for);
            if (parent != NULL && !client->sticky &&
                client->desktop != parent->desktop) {
                client->desktop = parent->desktop;
                ewmh_set_client_desktop(client);
            }
        }
        stacking_enforce();
    }

    XUngrabServer(wm.display);
}

static bool claim_root_window(void)
{
    const long root_events = SubstructureRedirectMask |
                             SubstructureNotifyMask |
                             PropertyChangeMask |
                             StructureNotifyMask;

    another_wm_detected = false;
    XSetErrorHandler(startup_xerror);
    XSelectInput(wm.display, wm.root, root_events);
    XSync(wm.display, False);
    XSetErrorHandler(runtime_xerror);

    if (another_wm_detected) {
        fprintf(stderr,
                "%s: ya existe otro gestor de ventanas controlando la pantalla '%s'.\n",
                AWM_NAME,
                DisplayString(wm.display));
        return false;
    }

    return true;
}

bool wm_initialize(int argc, char **argv)
{
    struct sigaction child_action;
    struct sigaction stop_action;

    memset(&wm, 0, sizeof(wm));
    wm.argc = argc;
    wm.argv = argv;
    wm.last_event_time = CurrentTime;
    stop_requested = 0;

    (void)setlocale(LC_CTYPE, "");
    runtime_config_load();

    wm.display = XOpenDisplay(NULL);
    if (wm.display == NULL) {
        fprintf(stderr,
                "%s: no se pudo abrir la pantalla X '%s'. Comprueba la variable DISPLAY.\n",
                AWM_NAME,
                XDisplayName(NULL));
        return false;
    }

    wm.screen = DefaultScreen(wm.display);
    wm.root = RootWindow(wm.display, wm.screen);
    wm.screen_width = DisplayWidth(wm.display, wm.screen);
    wm.screen_height = DisplayHeight(wm.display, wm.screen);

    if (!claim_root_window()) {
        XCloseDisplay(wm.display);
        wm.display = NULL;
        return false;
    }

    prime_randr_state();
    monitor_initialize();

    if (!desktop_initialize()) {
        monitor_cleanup();
        XCloseDisplay(wm.display);
        wm.display = NULL;
        return false;
    }
    ewmh_initialize();

    wm.focused_border = allocate_color(AWM_COLOR_BORDER_ACTIVE,
                                       WhitePixel(wm.display, wm.screen));
    wm.unfocused_border = allocate_color(AWM_COLOR_BORDER_INACTIVE,
                                         BlackPixel(wm.display, wm.screen));
    wm.title_active = allocate_color(AWM_COLOR_TITLE_ACTIVE,
                                     wm.focused_border);
    wm.title_inactive = allocate_color(AWM_COLOR_TITLE_INACTIVE,
                                       wm.unfocused_border);
    wm.title_text_active = allocate_color(AWM_COLOR_TITLE_TEXT_ACTIVE,
                                          WhitePixel(wm.display, wm.screen));
    wm.title_text_inactive = allocate_color(AWM_COLOR_TITLE_TEXT_INACTIVE,
                                            WhitePixel(wm.display, wm.screen));
    wm.button_background = allocate_color(AWM_COLOR_BUTTON,
                                          wm.title_inactive);
    wm.button_hover = allocate_color(AWM_COLOR_BUTTON_HOVER,
                                     wm.button_background);
    wm.button_pressed = allocate_color(AWM_COLOR_BUTTON_PRESSED,
                                       wm.title_inactive);
    wm.button_symbol = allocate_color(AWM_COLOR_BUTTON_SYMBOL,
                                      WhitePixel(wm.display, wm.screen));
    wm.button_outline = allocate_color(AWM_COLOR_BUTTON_OUTLINE,
                                       wm.unfocused_border);
    wm.bar_background = allocate_color(AWM_COLOR_BAR_BACKGROUND,
                                       BlackPixel(wm.display, wm.screen));
    wm.bar_foreground = allocate_color(AWM_COLOR_BAR_FOREGROUND,
                                       WhitePixel(wm.display, wm.screen));
    wm.bar_item = allocate_color(AWM_COLOR_BAR_ITEM,
                                 wm.bar_background);
    wm.bar_selected = allocate_color(AWM_COLOR_BAR_SELECTED,
                                     wm.title_active);
    wm.bar_active_window = allocate_color(AWM_COLOR_BAR_ACTIVE_WINDOW,
                                          wm.title_active);
    wm.bar_minimized = allocate_color(AWM_COLOR_BAR_MINIMIZED,
                                      wm.title_inactive);
    wm.bar_urgent = allocate_color(AWM_COLOR_BAR_URGENT,
                                   wm.title_active);
    wm.bar_separator = allocate_color(AWM_COLOR_BAR_SEPARATOR,
                                      wm.unfocused_border);
    wm.normal_cursor = XCreateFontCursor(wm.display, XC_left_ptr);
    wm.move_cursor = XCreateFontCursor(wm.display, XC_fleur);
    wm.resize_cursor = XCreateFontCursor(wm.display, XC_bottom_right_corner);
    wm.resize_left_cursor = XCreateFontCursor(wm.display, XC_left_side);
    wm.resize_right_cursor = XCreateFontCursor(wm.display, XC_right_side);
    wm.resize_bottom_cursor = XCreateFontCursor(wm.display, XC_bottom_side);
    wm.resize_bottom_left_cursor =
        XCreateFontCursor(wm.display, XC_bottom_left_corner);

    if (!draw_initialize()) {
        ewmh_cleanup();
        desktop_cleanup();
        monitor_cleanup();
        XCloseDisplay(wm.display);
        wm.display = NULL;
        return false;
    }
    clock_initialize();
    if (!desktop_entries_initialize()) {
        fprintf(stderr,
                "%s: no se pudo cargar el catalogo de aplicaciones; "
                "el menu se iniciara vacio\n",
                AWM_NAME);
    }
    if (!taskbar_initialize()) {
        desktop_entries_cleanup();
        draw_cleanup();
        ewmh_cleanup();
        desktop_cleanup();
        monitor_cleanup();
        XCloseDisplay(wm.display);
        wm.display = NULL;
        return false;
    }
    if (!menu_initialize()) {
        taskbar_cleanup();
        desktop_entries_cleanup();
        draw_cleanup();
        ewmh_cleanup();
        desktop_cleanup();
        monitor_cleanup();
        XCloseDisplay(wm.display);
        wm.display = NULL;
        return false;
    }

    memset(&child_action, 0, sizeof(child_action));
    child_action.sa_handler = SIG_IGN;
    child_action.sa_flags = SA_NOCLDWAIT;
    sigemptyset(&child_action.sa_mask);
    (void)sigaction(SIGCHLD, &child_action, NULL);

    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = request_clean_stop;
    sigemptyset(&stop_action.sa_mask);
    if (sigaction(SIGINT, &stop_action, NULL) != 0) {
        perror(AWM_NAME ": sigaction(SIGINT)");
    }
    if (sigaction(SIGTERM, &stop_action, NULL) != 0) {
        perror(AWM_NAME ": sigaction(SIGTERM)");
    }

    keyboard_grab_keys();
    scan_existing_windows();
    ewmh_update_client_list();
    taskbar_update();
    taskbar_raise();
    stacking_enforce();

    wm.running = true;
    XSync(wm.display, False);

    fprintf(stdout,
            "%s %s iniciado en %s (%dx%d)\n",
            AWM_NAME,
            AWM_VERSION,
            DisplayString(wm.display),
            wm.screen_width,
            wm.screen_height);
    fflush(stdout);
    return true;
}

void wm_run(void)
{
    while (wm.running && stop_requested == 0) {
        struct pollfd descriptor;
        int result;

        while (wm.running && stop_requested == 0 &&
               XPending(wm.display) > 0) {
            XEvent event;

            XNextEvent(wm.display, &event);
            events_dispatch(&event);
        }
        /* Keep the clock current even while clients continuously generate
         * events; clock_tick() redraws only when the minute changes. */
        clock_tick();
        if (!wm.running || stop_requested != 0) {
            break;
        }

        descriptor.fd = ConnectionNumber(wm.display);
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        result = poll(&descriptor, 1U, clock_next_timeout_ms());
        if (result == 0) {
            clock_tick();
        } else if (result < 0 && errno != EINTR) {
            perror(AWM_NAME ": poll");
            wm.running = false;
        }
    }
}

void wm_cleanup(void)
{
    if (wm.display == NULL) {
        return;
    }

    keyboard_ungrab_keys();
    menu_cleanup();
    client_cleanup_all();
    taskbar_cleanup();
    desktop_entries_cleanup();
    ewmh_cleanup();
    desktop_cleanup();
    draw_cleanup();
    monitor_cleanup();

    if (wm.normal_cursor != None) {
        XFreeCursor(wm.display, wm.normal_cursor);
    }
    if (wm.move_cursor != None) {
        XFreeCursor(wm.display, wm.move_cursor);
    }
    if (wm.resize_cursor != None) {
        XFreeCursor(wm.display, wm.resize_cursor);
    }
    if (wm.resize_left_cursor != None) {
        XFreeCursor(wm.display, wm.resize_left_cursor);
    }
    if (wm.resize_right_cursor != None) {
        XFreeCursor(wm.display, wm.resize_right_cursor);
    }
    if (wm.resize_bottom_cursor != None) {
        XFreeCursor(wm.display, wm.resize_bottom_cursor);
    }
    if (wm.resize_bottom_left_cursor != None) {
        XFreeCursor(wm.display, wm.resize_bottom_left_cursor);
    }

    XSync(wm.display, False);
    XCloseDisplay(wm.display);
    wm.display = NULL;
}

void wm_restart(void)
{
    char **arguments = wm.argv;
    const char *program = arguments[0];

    fprintf(stdout, "%s: reiniciando sin cerrar las aplicaciones...\n", AWM_NAME);
    fflush(stdout);
    wm.restarting = true;
    wm_cleanup();

    execvp(program, arguments);
    fprintf(stderr,
            "%s: no se pudo reiniciar '%s': %s\n",
            AWM_NAME,
            program,
            strerror(errno));
    _exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc > 1 &&
        (strcmp(argv[1], "--version") == 0 ||
         strcmp(argv[1], "-v") == 0)) {
        puts(AWM_NAME " " AWM_VERSION);
        return EXIT_SUCCESS;
    }

    if (!wm_initialize(argc, argv)) {
        return EXIT_FAILURE;
    }

    wm_run();
    wm_cleanup();
    return EXIT_SUCCESS;
}
