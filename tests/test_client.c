#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *type_name;
    Window transient_for;
    const char *motif;
    bool input;
    bool take_focus;
    bool urgent;
    bool override_redirect;
    int min_width;
    int min_height;
    int max_width;
    int max_height;
    int base_width;
    int base_height;
    int width_inc;
    int height_inc;
    int min_aspect_x;
    int min_aspect_y;
    int max_aspect_x;
    int max_aspect_y;
    unsigned long strut[4];
    bool has_strut;
    unsigned long strut_partial[12];
    bool has_strut_partial;
    const char *initial_states[16];
    size_t initial_state_count;
} Options;

static Atom state_atom(Display *display, const char *name)
{
    char full_name[128];

    if (snprintf(full_name,
                 sizeof(full_name),
                 "_NET_WM_STATE_%s",
                 name) < 0) {
        return None;
    }
    return XInternAtom(display, full_name, False);
}

static void request_net_wm_state(Display *display,
                                 Window root,
                                 Window window,
                                 Atom first,
                                 Atom second)
{
    XEvent message;

    memset(&message, 0, sizeof(message));
    message.xclient.type = ClientMessage;
    message.xclient.display = display;
    message.xclient.window = window;
    message.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    message.xclient.format = 32;
    message.xclient.data.l[0] = 1L;
    message.xclient.data.l[1] = (long)first;
    message.xclient.data.l[2] = (long)second;
    message.xclient.data.l[3] = 1L;

    XSendEvent(display,
               root,
               False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &message);
    XFlush(display);
}

static bool parse_pair(const char *text, int *first, int *second)
{
    return sscanf(text, "%dx%d", first, second) == 2;
}

static void parse_options(int argc, char **argv, Options *options)
{
    int index;

    memset(options, 0, sizeof(*options));
    options->input = true;
    options->min_width = 120;
    options->min_height = 80;
    options->width_inc = 10;
    options->height_inc = 10;

    for (index = 4; index < argc; ++index) {
        if (strcmp(argv[index], "--type") == 0 && index + 1 < argc) {
            options->type_name = argv[++index];
        } else if (strcmp(argv[index], "--transient") == 0 && index + 1 < argc) {
            options->transient_for = (Window)strtoul(argv[++index], NULL, 0);
        } else if (strcmp(argv[index], "--motif") == 0 && index + 1 < argc) {
            options->motif = argv[++index];
        } else if (strcmp(argv[index], "--input") == 0 && index + 1 < argc) {
            options->input = strcmp(argv[++index], "false") != 0;
        } else if (strcmp(argv[index], "--take-focus") == 0) {
            options->take_focus = true;
        } else if (strcmp(argv[index], "--urgent") == 0) {
            options->urgent = true;
        } else if (strcmp(argv[index], "--override") == 0) {
            options->override_redirect = true;
        } else if (strcmp(argv[index], "--min") == 0 && index + 1 < argc) {
            (void)parse_pair(argv[++index],
                             &options->min_width,
                             &options->min_height);
        } else if (strcmp(argv[index], "--max") == 0 && index + 1 < argc) {
            (void)parse_pair(argv[++index],
                             &options->max_width,
                             &options->max_height);
        } else if (strcmp(argv[index], "--base") == 0 && index + 1 < argc) {
            (void)parse_pair(argv[++index],
                             &options->base_width,
                             &options->base_height);
        } else if (strcmp(argv[index], "--inc") == 0 && index + 1 < argc) {
            (void)parse_pair(argv[++index],
                             &options->width_inc,
                             &options->height_inc);
        } else if (strcmp(argv[index], "--aspect") == 0 && index + 1 < argc) {
            (void)sscanf(argv[++index],
                         "%d:%d:%d:%d",
                         &options->min_aspect_x,
                         &options->min_aspect_y,
                         &options->max_aspect_x,
                         &options->max_aspect_y);
        } else if (strcmp(argv[index], "--strut") == 0 && index + 1 < argc) {
            options->has_strut =
                sscanf(argv[++index],
                       "%lu,%lu,%lu,%lu",
                       &options->strut[0],
                       &options->strut[1],
                       &options->strut[2],
                       &options->strut[3]) == 4;
        } else if (strcmp(argv[index], "--strut-partial-top") == 0 &&
                   index + 1 < argc) {
            options->has_strut_partial = true;
            options->strut_partial[2] = strtoul(argv[++index], NULL, 0);
            options->strut_partial[8] = 0UL;
            options->strut_partial[9] = 0xffffffffUL;
        } else if (strcmp(argv[index], "--initial-state") == 0 &&
                   index + 1 < argc &&
                   options->initial_state_count <
                       sizeof(options->initial_states) /
                           sizeof(options->initial_states[0])) {
            options->initial_states[options->initial_state_count++] = argv[++index];
        }
    }
}

static void set_type(Display *display, Window window, const char *type_name)
{
    char atom_name[128];
    Atom type;

    if (type_name == NULL) {
        return;
    }
    (void)snprintf(atom_name,
                   sizeof(atom_name),
                   "_NET_WM_WINDOW_TYPE_%s",
                   type_name);
    type = XInternAtom(display, atom_name, False);
    XChangeProperty(display,
                    window,
                    XInternAtom(display, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (const unsigned char *)&type,
                    1);
}

static void set_motif_hints(Display *display, Window window, const char *motif)
{
    unsigned long hints[5] = {1UL << 1, 0UL, 0UL, 0UL, 0UL};
    Atom property;

    if (motif == NULL) {
        return;
    }
    if (strcmp(motif, "border") == 0) {
        hints[2] = 1UL << 1;
    } else if (strcmp(motif, "full") == 0) {
        hints[2] = 1UL;
    }
    property = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    XChangeProperty(display,
                    window,
                    property,
                    property,
                    32,
                    PropModeReplace,
                    (const unsigned char *)hints,
                    5);
}

static void set_initial_states(Display *display,
                               Window window,
                               const Options *options)
{
    Atom states[16];
    size_t index;

    if (options->initial_state_count == 0U) {
        return;
    }
    for (index = 0U; index < options->initial_state_count; ++index) {
        states[index] = state_atom(display, options->initial_states[index]);
    }
    XChangeProperty(display,
                    window,
                    XInternAtom(display, "_NET_WM_STATE", False),
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    (const unsigned char *)states,
                    (int)options->initial_state_count);
}

static void send_requested_state(Display *display,
                                 Window root,
                                 Window window,
                                 const char *requested_state)
{
    Atom first = None;
    Atom second = None;

    if (strcmp(requested_state, "none") == 0) {
        return;
    }
    if (strcmp(requested_state, "maximized") == 0) {
        first = state_atom(display, "MAXIMIZED_VERT");
        second = state_atom(display, "MAXIMIZED_HORZ");
    } else {
        char uppercase[64] = "";
        size_t index;

        for (index = 0U;
             requested_state[index] != '\0' && index + 1U < sizeof(uppercase);
             ++index) {
            char character = requested_state[index];

            uppercase[index] = character >= 'a' && character <= 'z'
                                   ? (char)(character - 'a' + 'A')
                                   : character;
            uppercase[index + 1U] = '\0';
        }
        first = state_atom(display, uppercase);
    }
    request_net_wm_state(display, root, window, first, second);
}

int main(int argc, char **argv)
{
    const char *title = argc > 1 ? argv[1] : "AntonioWM test client";
    const char *geometry = argc > 2 ? argv[2] : "320x200";
    const char *requested_state = argc > 3 ? argv[3] : "none";
    Options options;
    Display *display = XOpenDisplay(NULL);
    int screen;
    Window root;
    Window window;
    Atom protocols[2];
    int protocol_count = 1;
    XSizeHints size_hints;
    XWMHints wm_hints;
    XSetWindowAttributes attributes;
    unsigned long attribute_mask = 0UL;
    XEvent event;
    int x = 0;
    int y = 0;
    unsigned int width = 320U;
    unsigned int height = 200U;
    int geometry_mask;
    bool state_sent = false;
    unsigned long take_focus_count = 0UL;

    if (display == NULL) {
        fprintf(stderr, "test_client: no se pudo abrir DISPLAY\n");
        return EXIT_FAILURE;
    }
    parse_options(argc, argv, &options);
    geometry_mask = XParseGeometry(geometry, &x, &y, &width, &height);

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    memset(&attributes, 0, sizeof(attributes));
    if (options.override_redirect) {
        attributes.override_redirect = True;
        attribute_mask |= CWOverrideRedirect;
    }
    window = XCreateWindow(display,
                           root,
                           x,
                           y,
                           width,
                           height,
                           0U,
                           CopyFromParent,
                           InputOutput,
                           CopyFromParent,
                           attribute_mask,
                           &attributes);

    XStoreName(display, window, title);
    protocols[0] = XInternAtom(display, "WM_DELETE_WINDOW", False);
    if (options.take_focus) {
        protocols[protocol_count++] = XInternAtom(display,
                                                   "WM_TAKE_FOCUS",
                                                   False);
    }
    XSetWMProtocols(display, window, protocols, protocol_count);

    memset(&size_hints, 0, sizeof(size_hints));
    size_hints.flags = PMinSize | PResizeInc;
    if ((geometry_mask & (XValue | YValue)) == (XValue | YValue)) {
        size_hints.flags |= USPosition;
        size_hints.x = x;
        size_hints.y = y;
    }
    size_hints.min_width = options.min_width;
    size_hints.min_height = options.min_height;
    size_hints.width_inc = options.width_inc;
    size_hints.height_inc = options.height_inc;
    if (options.max_width > 0 && options.max_height > 0) {
        size_hints.flags |= PMaxSize;
        size_hints.max_width = options.max_width;
        size_hints.max_height = options.max_height;
    }
    if (options.base_width > 0 || options.base_height > 0) {
        size_hints.flags |= PBaseSize;
        size_hints.base_width = options.base_width;
        size_hints.base_height = options.base_height;
    }
    if (options.min_aspect_x > 0 && options.min_aspect_y > 0 &&
        options.max_aspect_x > 0 && options.max_aspect_y > 0) {
        size_hints.flags |= PAspect;
        size_hints.min_aspect.x = options.min_aspect_x;
        size_hints.min_aspect.y = options.min_aspect_y;
        size_hints.max_aspect.x = options.max_aspect_x;
        size_hints.max_aspect.y = options.max_aspect_y;
    }
    XSetWMNormalHints(display, window, &size_hints);

    memset(&wm_hints, 0, sizeof(wm_hints));
    wm_hints.flags = InputHint;
    wm_hints.input = options.input ? True : False;
    if (options.urgent) {
        wm_hints.flags |= XUrgencyHint;
    }
    XSetWMHints(display, window, &wm_hints);

    if (options.transient_for != None) {
        XSetTransientForHint(display, window, options.transient_for);
    }
    set_type(display, window, options.type_name);
    set_motif_hints(display, window, options.motif);
    set_initial_states(display, window, &options);
    if (options.has_strut) {
        XChangeProperty(display,
                        window,
                        XInternAtom(display, "_NET_WM_STRUT", False),
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (const unsigned char *)options.strut,
                        4);
    }
    if (options.has_strut_partial) {
        XChangeProperty(display,
                        window,
                        XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False),
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (const unsigned char *)options.strut_partial,
                        12);
    }

    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    XMapWindow(display, window);

    for (;;) {
        XNextEvent(display, &event);

        if (event.type == Expose && event.xexpose.count == 0) {
            const char *line = "Cliente de prueba de AntonioWM";

            XDrawString(display,
                        window,
                        DefaultGC(display, screen),
                        18,
                        35,
                        line,
                        (int)strlen(line));
        } else if (event.type == MapNotify && !state_sent) {
            send_requested_state(display,
                                 root,
                                 window,
                                 requested_state);
            state_sent = true;
        } else if (event.type == ClientMessage &&
                   (Atom)event.xclient.data.l[0] == protocols[0]) {
            break;
        } else if (event.type == ClientMessage && options.take_focus &&
                   (Atom)event.xclient.data.l[0] == protocols[1]) {
            Atom counter = XInternAtom(display,
                                       "_AWM_TEST_TAKE_FOCUS_COUNT",
                                       False);

            ++take_focus_count;
            XChangeProperty(display,
                            window,
                            counter,
                            XA_CARDINAL,
                            32,
                            PropModeReplace,
                            (const unsigned char *)&take_focus_count,
                            1);
            XSetInputFocus(display,
                           window,
                           RevertToPointerRoot,
                           (Time)event.xclient.data.l[1]);
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return EXIT_SUCCESS;
}
