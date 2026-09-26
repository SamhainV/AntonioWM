#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char *read_stream(FILE *stream, size_t *length)
{
    unsigned char *data = NULL;
    size_t used = 0U;
    size_t capacity = 0U;

    for (;;) {
        size_t available;
        size_t received;

        if (used == capacity) {
            size_t next_capacity = capacity == 0U ? 4096U : capacity * 2U;
            unsigned char *resized;

            if (next_capacity < capacity || next_capacity > (size_t)INT_MAX) {
                free(data);
                return NULL;
            }
            resized = realloc(data, next_capacity);
            if (resized == NULL) {
                free(data);
                return NULL;
            }
            data = resized;
            capacity = next_capacity;
        }
        available = capacity - used;
        received = fread(data + used, 1U, available, stream);
        used += received;
        if (received < available) {
            if (ferror(stream) != 0) {
                free(data);
                return NULL;
            }
            break;
        }
    }

    *length = used;
    return data;
}

static void record_owner_pid(void)
{
    const char *path = getenv("AWM_TEST_XCLIP_PID_LOG");
    FILE *file;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    file = fopen(path, "a");
    if (file == NULL) {
        return;
    }
    (void)fprintf(file, "%ld\n", (long)getpid());
    (void)fclose(file);
}

static void answer_selection_request(Display *display,
                                     const XSelectionRequestEvent *request,
                                     Atom targets,
                                     Atom png,
                                     const unsigned char *data,
                                     size_t length)
{
    XEvent reply;
    Atom property = request->property;

    memset(&reply, 0, sizeof(reply));
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = request->display;
    reply.xselection.requestor = request->requestor;
    reply.xselection.selection = request->selection;
    reply.xselection.target = request->target;
    reply.xselection.time = request->time;
    reply.xselection.property = None;

    if (property == None) {
        property = request->target;
    }
    if (request->target == targets) {
        Atom advertised[] = { targets, png };

        XChangeProperty(display,
                        request->requestor,
                        property,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (const unsigned char *)advertised,
                        (int)(sizeof(advertised) / sizeof(advertised[0])));
        reply.xselection.property = property;
    } else if (request->target == png) {
        XChangeProperty(display,
                        request->requestor,
                        property,
                        png,
                        8,
                        PropModeReplace,
                        data,
                        (int)length);
        reply.xselection.property = property;
    }

    (void)XSendEvent(display, request->requestor, False, 0L, &reply);
    XFlush(display);
}

static int run_owner(void)
{
    unsigned char *data;
    size_t length = 0U;
    Display *display;
    Window window;
    Atom clipboard;
    Atom targets;
    Atom png;
    bool running = true;

    data = read_stream(stdin, &length);
    if (data == NULL || length == 0U) {
        free(data);
        return EXIT_FAILURE;
    }
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        free(data);
        return EXIT_FAILURE;
    }

    clipboard = XInternAtom(display, "CLIPBOARD", False);
    targets = XInternAtom(display, "TARGETS", False);
    png = XInternAtom(display, "image/png", False);
    window = XCreateSimpleWindow(display,
                                 DefaultRootWindow(display),
                                 0,
                                 0,
                                 1U,
                                 1U,
                                 0U,
                                 0UL,
                                 0UL);
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    XSync(display, False);
    if (XGetSelectionOwner(display, clipboard) != window) {
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        free(data);
        return EXIT_FAILURE;
    }
    record_owner_pid();

    while (running) {
        XEvent event;

        XNextEvent(display, &event);
        if (event.type == SelectionRequest) {
            answer_selection_request(display,
                                     &event.xselectionrequest,
                                     targets,
                                     png,
                                     data,
                                     length);
        } else if (event.type == SelectionClear) {
            running = false;
        }
    }

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    free(data);
    return EXIT_SUCCESS;
}

static bool wait_for_selection(Display *display,
                               Window window,
                               Atom selection,
                               Atom target,
                               Atom property)
{
    struct pollfd descriptor;
    int remaining_ms = 3000;

    XConvertSelection(display,
                      selection,
                      target,
                      property,
                      window,
                      CurrentTime);
    XFlush(display);

    descriptor.fd = ConnectionNumber(display);
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    while (remaining_ms > 0) {
        int result;

        while (XPending(display) > 0) {
            XEvent event;

            XNextEvent(display, &event);
            if (event.type == SelectionNotify &&
                event.xselection.requestor == window &&
                event.xselection.selection == selection &&
                event.xselection.target == target) {
                return event.xselection.property == property;
            }
        }
        result = poll(&descriptor, 1U, 100);
        if (result < 0 && errno != EINTR) {
            return false;
        }
        if (result >= 0) {
            remaining_ms -= 100;
        }
    }
    return false;
}

static bool target_is_advertised(Display *display,
                                 Window window,
                                 Atom clipboard,
                                 Atom targets,
                                 Atom png,
                                 Atom property)
{
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0UL;
    unsigned long bytes_after = 0UL;
    unsigned char *raw = NULL;
    bool found = false;
    unsigned long index;

    if (!wait_for_selection(display,
                            window,
                            clipboard,
                            targets,
                            property) ||
        XGetWindowProperty(display,
                           window,
                           property,
                           0L,
                           64L,
                           True,
                           XA_ATOM,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &raw) != Success ||
        actual_type != XA_ATOM || actual_format != 32 || bytes_after != 0UL) {
        if (raw != NULL) {
            XFree(raw);
        }
        return false;
    }

    for (index = 0UL; index < item_count; ++index) {
        const Atom *atoms = (const Atom *)raw;

        if (atoms[index] == png) {
            found = true;
            break;
        }
    }
    XFree(raw);
    return found;
}

static bool clipboard_matches(Display *display,
                              Window window,
                              Atom clipboard,
                              Atom png,
                              Atom property,
                              const unsigned char *expected,
                              size_t expected_length)
{
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0UL;
    unsigned long bytes_after = 0UL;
    unsigned char *raw = NULL;
    bool matches;

    if (!wait_for_selection(display, window, clipboard, png, property) ||
        XGetWindowProperty(display,
                           window,
                           property,
                           0L,
                           (long)((expected_length + 3U) / 4U + 1U),
                           True,
                           AnyPropertyType,
                           &actual_type,
                           &actual_format,
                           &item_count,
                           &bytes_after,
                           &raw) != Success) {
        if (raw != NULL) {
            XFree(raw);
        }
        return false;
    }

    matches = actual_type == png && actual_format == 8 && bytes_after == 0UL &&
              item_count == (unsigned long)expected_length && raw != NULL &&
              memcmp(raw, expected, expected_length) == 0;
    if (raw != NULL) {
        XFree(raw);
    }
    return matches;
}

static int run_probe(const char *expected_path)
{
    FILE *file;
    unsigned char *expected;
    size_t expected_length = 0U;
    Display *display;
    Window window;
    Atom clipboard;
    Atom targets;
    Atom png;
    Atom property;
    bool valid;

    file = fopen(expected_path, "rb");
    if (file == NULL) {
        return EXIT_FAILURE;
    }
    expected = read_stream(file, &expected_length);
    (void)fclose(file);
    if (expected == NULL || expected_length == 0U) {
        free(expected);
        return EXIT_FAILURE;
    }

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        free(expected);
        return EXIT_FAILURE;
    }
    clipboard = XInternAtom(display, "CLIPBOARD", False);
    targets = XInternAtom(display, "TARGETS", False);
    png = XInternAtom(display, "image/png", False);
    property = XInternAtom(display, "AWM_TEST_CLIPBOARD_DATA", False);
    window = XCreateSimpleWindow(display,
                                 DefaultRootWindow(display),
                                 0,
                                 0,
                                 1U,
                                 1U,
                                 0U,
                                 0UL,
                                 0UL);

    valid = XGetSelectionOwner(display, clipboard) != None &&
            target_is_advertised(display,
                                 window,
                                 clipboard,
                                 targets,
                                 png,
                                 property) &&
            clipboard_matches(display,
                              window,
                              clipboard,
                              png,
                              property,
                              expected,
                              expected_length);

    XDestroyWindow(display, window);
    XCloseDisplay(display);
    free(expected);
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "probe") == 0) {
        return run_probe(argv[2]);
    }
    return run_owner();
}
