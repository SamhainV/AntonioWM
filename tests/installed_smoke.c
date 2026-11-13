/* Check the installed WM using only a temporary window owned by this client. */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *display;
static Window root, window, previous_focus;
static int pointer_x, pointer_y;
static bool saved_pointer;
static const char *directory;
static Atom atom(const char *name) { return XInternAtom(display, name, False); }
static void pause_ms(long ms)
{
    struct timespec t = {ms / 1000, ms % 1000 * 1000000};
    XSync(display, False); nanosleep(&t, NULL);
}
static unsigned long value(Window w, const char *name)
{
    Atom type; int format; unsigned long count, after, result = ~0UL;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, w, atom(name), 0, 1024, False, AnyPropertyType,
                           &type, &format, &count, &after, &data) == Success &&
        format == 32 && count > 0) result = ((unsigned long *)data)[0];
    if (data != NULL) XFree(data);
    return result;
}
static void activate(Window w)
{
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = w;
    e.xclient.message_type = atom("_NET_ACTIVE_WINDOW"); e.xclient.format = 32;
    e.xclient.data.l[0] = 2;
    XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
}
static void cleanup(void)
{
    if (display == NULL) return;
    if (window != None) XDestroyWindow(display, window);
    if (previous_focus != None && previous_focus != (Window)~0UL) {
        XWindowAttributes a;
        if (XGetWindowAttributes(display, previous_focus, &a)) activate(previous_focus);
    }
    if (saved_pointer) XWarpPointer(display, None, root, 0, 0, 0, 0, pointer_x, pointer_y);
    XSync(display, False); XCloseDisplay(display); display = NULL;
}
static void require(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL %s\n", message); exit(EXIT_FAILURE); }
}
static Window named(const char *prefix, bool visible)
{
    Window r, parent, *children = NULL, result = None; unsigned int count;
    XQueryTree(display, root, &r, &parent, &children, &count);
    for (unsigned int i = 0; i < count; ++i) {
        char *name = NULL; XWindowAttributes a;
        if (XFetchName(display, children[i], &name) && name != NULL) {
            if (strncmp(name, prefix, strlen(prefix)) == 0 &&
                XGetWindowAttributes(display, children[i], &a) &&
                (!visible || a.map_state == IsViewable)) result = children[i];
            XFree(name);
        }
    }
    if (children != NULL) XFree(children);
    return result;
}
static void wait_value(Window w, const char *name, unsigned long expected)
{
    for (int i = 0; i < 100; ++i) { if (value(w, name) == expected) return; pause_ms(30); }
    require(false, name);
}
static void image_file(Window w, const char *name)
{
    XWindowAttributes a; require(XGetWindowAttributes(display, w, &a) != 0, "capture attributes");
    XImage *im = XGetImage(display, w, 0, 0, (unsigned)a.width, (unsigned)a.height, AllPlanes, ZPixmap);
    require(im != NULL, "capture image");
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb"); require(file != NULL, "capture file");
    fprintf(file, "P6\n%d %d\n255\n", a.width, a.height);
    for (int y = 0; y < a.height; ++y) for (int x = 0; x < a.width; ++x) {
        unsigned long p = XGetPixel(im, x, y);
        fputc((int)(p >> 16) & 255, file); fputc((int)(p >> 8) & 255, file); fputc((int)p & 255, file);
    }
    fclose(file); XDestroyImage(im);
}
static bool has_content(Window w)
{
    XWindowAttributes a; if (!XGetWindowAttributes(display, w, &a)) return false;
    XImage *im = XGetImage(display, w, 0, 0, (unsigned)a.width, (unsigned)a.height, AllPlanes, ZPixmap);
    if (im == NULL) return false;
    unsigned int matches = 0;
    for (int y = 0; y < a.height; ++y) for (int x = 0; x < a.width; ++x)
        if (XGetPixel(im, x, y) == 0x3456abUL) ++matches;
    XDestroyImage(im); return matches > 100U;
}
int main(int argc, char **argv)
{
    require(argc == 2, "output directory argument"); directory = argv[1];
    require(getenv("DISPLAY") && !strcmp(getenv("DISPLAY"), ":0"), "explicit installed session :0");
    display = XOpenDisplay(NULL); require(display != NULL, "XOpenDisplay");
    atexit(cleanup); root = DefaultRootWindow(display);
    require(DefaultDepth(display, DefaultScreen(display)) == 24, "24-bit capture visual");
    previous_focus = (Window)value(root, "_NET_ACTIVE_WINDOW");
    Window r, child; int wx, wy; unsigned int modifiers;
    saved_pointer = XQueryPointer(display, root, &r, &child, &pointer_x, &pointer_y, &wx, &wy, &modifiers);
    require(value(root, "_NET_NUMBER_OF_DESKTOPS") == 4, "four default desktops from unchanged config");
    window = XCreateSimpleWindow(display, root, 100, 100, 360, 200, 0, 0, 0x3456ab);
    XStoreName(display, window, "AntonioWM comprobacion instalada - titulo largo para truncamiento");
    unsigned long icon[402]; icon[0] = icon[1] = 20;
    for (int i = 2; i < 402; ++i) icon[i] = 0xff00fe7fUL;
    XChangeProperty(display, window, atom("_NET_WM_ICON"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)icon, 402);
    XMapWindow(display, window); wait_value(window, "WM_STATE", NormalState);
    pause_ms(250);
    Window panel = named("AntonioWM Panel", true); require(panel != None, "panel visible");
    XWindowAttributes a; XGetWindowAttributes(display, panel, &a);
    XImage *im = XGetImage(display, panel, 0, 0, (unsigned)a.width, (unsigned)a.height, AllPlanes, ZPixmap);
    require(im != NULL, "panel image");
    int icon_x = -1;
    for (int x = 0; x < a.width; ++x) if (XGetPixel(im, x, a.height / 2) == 0x00fe7fUL) { icon_x = x; break; }
    XDestroyImage(im); require(icon_x >= 0, "application icon rendered in taskbar");
    image_file(panel, "installed-panel");
    XWarpPointer(display, None, root, 0, 0, 0, 0, a.x + icon_x + 8, a.y + a.height / 2);
    pause_ms(700);
    const char *prefix = "AntonioWM Task Preview - AntonioWM comprobacion instalada";
    Window preview = named(prefix, true); require(preview != None, "normal preview visible");
    require(has_content(preview), "normal preview contains client pixels");
    image_file(preview, "installed-preview-normal");
    XWarpPointer(display, None, root, 0, 0, 0, 0, a.x + 100, a.y - 100); pause_ms(100);
    XIconifyWindow(display, window, DefaultScreen(display)); wait_value(window, "WM_STATE", IconicState);
    XWarpPointer(display, None, root, 0, 0, 0, 0, a.x + icon_x + 8, a.y + a.height / 2);
    pause_ms(700);
    preview = named(prefix, true); require(preview != None, "minimized preview visible");
    require(has_content(preview), "minimized preview contains cached client pixels");
    image_file(preview, "installed-preview-minimized");
    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    wait_value(window, "WM_STATE", NormalState); wait_value(root, "_NET_ACTIVE_WINDOW", window);
    puts("PASS installed WM: application icon rendered; normal and minimized previews contain real content; task click restores; four desktops remain configured.");
    return EXIT_SUCCESS;
}
