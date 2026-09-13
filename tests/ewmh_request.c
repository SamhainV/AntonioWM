#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long parse_long(const char *text)
{
    return strtol(text, NULL, 0);
}

static void usage(void)
{
    fprintf(stderr,
            "uso: awm-ewmh-request COMMAND WINDOW [argumentos]\n"
            "  active WINDOW [source requestor]\n"
            "  close WINDOW\n"
            "  desktop WINDOW INDEX\n"
            "  state WINDOW add|remove|toggle STATE [STATE]\n"
            "  moveresize WINDOW X Y WIDTH HEIGHT\n"
            "  restack WINDOW SIBLING DETAIL\n"
            "  interactive WINDOW X Y DIRECTION\n");
}

static long state_action(const char *text)
{
    if (strcmp(text, "add") == 0) {
        return 1L;
    }
    if (strcmp(text, "toggle") == 0) {
        return 2L;
    }
    return 0L;
}

static Atom state_atom(Display *display, const char *name)
{
    char full_name[128];

    (void)snprintf(full_name,
                   sizeof(full_name),
                   "_NET_WM_STATE_%s",
                   name);
    return XInternAtom(display, full_name, False);
}

int main(int argc, char **argv)
{
    Display *display;
    Window root;
    Window window;
    XEvent event;

    if (argc < 3) {
        usage();
        return EXIT_FAILURE;
    }
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "awm-ewmh-request: no se pudo abrir DISPLAY\n");
        return EXIT_FAILURE;
    }
    root = DefaultRootWindow(display);
    window = (Window)strtoul(argv[2], NULL, 0);
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.format = 32;

    if (strcmp(argv[1], "active") == 0) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_ACTIVE_WINDOW",
                                                 False);
        event.xclient.data.l[0] = argc > 3 ? parse_long(argv[3]) : 2L;
        event.xclient.data.l[1] = CurrentTime;
        event.xclient.data.l[2] = argc > 4 ? parse_long(argv[4]) : 0L;
    } else if (strcmp(argv[1], "close") == 0) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_CLOSE_WINDOW",
                                                 False);
        event.xclient.data.l[0] = CurrentTime;
        event.xclient.data.l[1] = 2L;
    } else if (strcmp(argv[1], "desktop") == 0 && argc >= 4) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_WM_DESKTOP",
                                                 False);
        event.xclient.data.l[0] = parse_long(argv[3]);
        event.xclient.data.l[1] = 2L;
    } else if (strcmp(argv[1], "state") == 0 && argc >= 5) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_WM_STATE",
                                                 False);
        event.xclient.data.l[0] = state_action(argv[3]);
        event.xclient.data.l[1] = (long)state_atom(display, argv[4]);
        event.xclient.data.l[2] = argc > 5
                                      ? (long)state_atom(display, argv[5])
                                      : (long)None;
        event.xclient.data.l[3] = 2L;
    } else if (strcmp(argv[1], "moveresize") == 0 && argc >= 7) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_MOVERESIZE_WINDOW",
                                                 False);
        event.xclient.data.l[0] = (long)(0x0fUL << 8) | (2L << 12);
        event.xclient.data.l[1] = parse_long(argv[3]);
        event.xclient.data.l[2] = parse_long(argv[4]);
        event.xclient.data.l[3] = parse_long(argv[5]);
        event.xclient.data.l[4] = parse_long(argv[6]);
    } else if (strcmp(argv[1], "restack") == 0 && argc >= 5) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_RESTACK_WINDOW",
                                                 False);
        event.xclient.data.l[0] = 2L;
        event.xclient.data.l[1] = parse_long(argv[3]);
        event.xclient.data.l[2] = parse_long(argv[4]);
    } else if (strcmp(argv[1], "interactive") == 0 && argc >= 6) {
        event.xclient.message_type = XInternAtom(display,
                                                 "_NET_WM_MOVERESIZE",
                                                 False);
        event.xclient.data.l[0] = parse_long(argv[3]);
        event.xclient.data.l[1] = parse_long(argv[4]);
        event.xclient.data.l[2] = parse_long(argv[5]);
        event.xclient.data.l[3] = CurrentTime;
        event.xclient.data.l[4] = 2L;
    } else {
        usage();
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    if (XSendEvent(display,
                   root,
                   False,
                   SubstructureRedirectMask | SubstructureNotifyMask,
                   &event) == 0) {
        fprintf(stderr, "awm-ewmh-request: XSendEvent fallo\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    XSync(display, False);
    XCloseDisplay(display);
    return EXIT_SUCCESS;
}
