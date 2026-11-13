#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *display;
static Window root;
static int errors;
static int error_handler(Display *d, XErrorEvent *e)
{
    char text[128]; XGetErrorText(d, e->error_code, text, sizeof(text));
    fprintf(stderr, "XERROR %s request=%u\n", text, e->request_code); ++errors;
    return 0;
}
static void pause_ms(long ms)
{
    struct timespec t = {ms / 1000, ms % 1000 * 1000000};
    XSync(display, False); nanosleep(&t, NULL);
}
static Atom atom(const char *name) { return XInternAtom(display, name, False); }
static unsigned long property(Window w, const char *name)
{
    Atom type; int format; unsigned long n, after, result = ~0UL; unsigned char *data = NULL;
    XGetWindowProperty(display, w, atom(name), 0, 4096, False, AnyPropertyType,
                       &type, &format, &n, &after, &data);
    if (format == 32 && n) result = ((unsigned long *)data)[0];
    if (data) XFree(data);
    return result;
}
static void wait_property(Window w, const char *name, unsigned long expected)
{
    for (int i = 0; i < 100; ++i) { if (property(w, name) == expected) return; pause_ms(20); }
    fprintf(stderr, "%s expected=%lu actual=%lu\n", name, expected, property(w, name)); abort();
}
static Window popup(bool mapped)
{
    Window r, p, *children = NULL, result = None; unsigned int count;
    XQueryTree(display, root, &r, &p, &children, &count);
    for (unsigned int i = 0; i < count; ++i) {
        char *name = NULL; XWindowAttributes a;
        if (XFetchName(display, children[i], &name) && name) {
            if (!strcmp(name, "AntonioWM Alt-Tab") && XGetWindowAttributes(display, children[i], &a) &&
                (!mapped || a.map_state == IsViewable)) result = children[i];
            XFree(name);
        }
    }
    if (children) XFree(children);
    return result;
}
static void no_visible_preview(void)
{
    Window r, parent, *children = NULL; unsigned int count;
    XQueryTree(display, root, &r, &parent, &children, &count);
    for (unsigned int i = 0; i < count; ++i) {
        char *name = NULL; XWindowAttributes attr;
        if (XFetchName(display, children[i], &name) && name) {
            if (!strncmp(name, "AntonioWM Task Preview", 22)) {
                assert(XGetWindowAttributes(display, children[i], &attr));
                assert(attr.map_state != IsViewable);
            }
            XFree(name);
        }
    }
    if (children) XFree(children);
}
static void wait_popup(bool wanted)
{
    for (int i = 0; i < 100; ++i) { if ((popup(true) != None) == wanted) return; pause_ms(20); }
    fprintf(stderr, "popup wanted=%d\n", wanted); abort();
}
static void key(KeySym symbol, bool down)
{
    XTestFakeKeyEvent(display, XKeysymToKeycode(display, symbol), down, CurrentTime);
    pause_ms(35);
}
static void tab(void) { key(XK_Tab, true); key(XK_Tab, false); }
static void message(Window w, const char *name, long a, long b)
{
    XEvent e = {0}; e.xclient.type = ClientMessage; e.xclient.window = w;
    e.xclient.message_type = atom(name); e.xclient.format = 32;
    e.xclient.data.l[0] = a; e.xclient.data.l[1] = b;
    XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    pause_ms(100);
}
static Window create(const char *name, int x, bool input, const char *type)
{
    Window w = XCreateSimpleWindow(display, root, x, 120, 280, 180, 0, 0, 0x3b627e);
    XStoreName(display, w, name);
    XWMHints hints = {0}; hints.flags = InputHint; hints.input = input; XSetWMHints(display, w, &hints);
    if (type) {
        Atom t = atom(type);
        XChangeProperty(display, w, atom("_NET_WM_WINDOW_TYPE"), XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&t, 1);
    }
    XMapWindow(display, w); wait_property(w, "WM_STATE", NormalState);
    return w;
}
static void activate(Window w) { message(w, "_NET_ACTIVE_WINDOW", 2, 0); wait_property(root, "_NET_ACTIVE_WINDOW", w); }
static void not_listed(Window w)
{
    Atom t; int f; unsigned long n, after; unsigned char *data = NULL;
    XGetWindowProperty(display, root, atom("_NET_CLIENT_LIST"), 0, 4096, False, XA_WINDOW,
                       &t, &f, &n, &after, &data);
    for (unsigned long i = 0; i < n; ++i) assert(((unsigned long *)data)[i] != w);
    if (data) XFree(data);
    assert(property(w, "WM_STATE") == ~0UL);
}
static void check_focus(Window w)
{
    wait_property(root, "_NET_ACTIVE_WINDOW", w);
    Window focus; int revert; XGetInputFocus(display, &focus, &revert); assert(focus == w);
}
static void snapshot(const char *name)
{
    Window w = popup(true); assert(w);
    XWindowAttributes a; XGetWindowAttributes(display, w, &a);
    XImage *im = XGetImage(display, w, 0, 0, (unsigned)a.width, (unsigned)a.height, AllPlanes, ZPixmap); assert(im);
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.ppm", getenv("SWITCHER_EVIDENCE"), name);
    FILE *f = fopen(path, "wb"); assert(f); fprintf(f, "P6\n%d %d\n255\n", a.width, a.height);
    for (int y = 0; y < a.height; ++y) for (int x = 0; x < a.width; ++x) {
        unsigned long p = XGetPixel(im, x, y);
        fputc((int)(p >> 16) & 255, f); fputc((int)(p >> 8) & 255, f); fputc((int)p & 255, f);
    }
    fclose(f); XDestroyImage(im);
}
static void assert_ungrabbed(void)
{
    assert(XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess);
    XUngrabKeyboard(display, CurrentTime); pause_ms(20);
}
int main(void)
{
    assert(getenv("DISPLAY") && !strcmp(getenv("DISPLAY"), ":151"));
    assert(getenv("SWITCHER_EVIDENCE")); setvbuf(stdout, NULL, _IOLBF, 0);
    display = XOpenDisplay(NULL); assert(display); root = DefaultRootWindow(display);
    XSetErrorHandler(error_handler); wait_property(root, "_NET_NUMBER_OF_DESKTOPS", 4);
    Window a = create("Alpha - long application title for visual truncation and icon display", 60, true, NULL);
    unsigned long icon[] = {1, 1, 0xff22cc88UL};
    XChangeProperty(display, a, atom("_NET_WM_ICON"), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)icon, 3);
    Window b = create("Beta minimized - must be excluded", 120, true, NULL);
    Window c = create("Gamma on active right monitor", 850, true, NULL);
    Window dock = create("Excluded dock", 10, true, "_NET_WM_WINDOW_TYPE_DOCK");
    Window noinput = create("Excluded no input", 20, false, NULL);
    Window other = create("Other desktop", 30, true, NULL);
    message(other, "_NET_WM_DESKTOP", 1, 2);
    XIconifyWindow(display, b, DefaultScreen(display)); wait_property(b, "WM_STATE", IconicState);
    /* skip_taskbar does not exclude a focusable normal window from existing Alt+Tab. */
    message(a, "_NET_WM_STATE", 1, (long)atom("_NET_WM_STATE_SKIP_TASKBAR"));
    XResizeWindow(display, c, 280U, 500U); pause_ms(100);
    activate(c);
    XWarpPointer(display, None, root, 0, 0, 0, 0, 100, 500); pause_ms(100);
    key(XK_Alt_L, true); tab(); wait_popup(true); check_focus(a);
    Window overlay = popup(true); not_listed(overlay);
    XWindowAttributes attr; XGetWindowAttributes(display, overlay, &attr);
    assert(attr.override_redirect && attr.x >= 640 && attr.x + attr.width <= 1280 &&
           attr.y >= 0 && attr.y + attr.height <= 800 && attr.height == 96);
    XWMHints *hints = XGetWMHints(display, overlay); assert(hints && (hints->flags & InputHint) && !hints->input); XFree(hints);
    XImage *selected_image = XGetImage(display, overlay, 10, 33, 1, 1, AllPlanes, ZPixmap);
    assert(selected_image && XGetPixel(selected_image, 0, 0) == 0x536b7cUL);
    XDestroyImage(selected_image);
    selected_image = XGetImage(display, overlay, 24, 48, 1, 1, AllPlanes, ZPixmap);
    assert(selected_image && XGetPixel(selected_image, 0, 0) == 0x22cc88UL);
    XDestroyImage(selected_image);
    snapshot("right-forward");
    XTestFakeMotionEvent(display, -1, 250, 786, CurrentTime); pause_ms(500);
    no_visible_preview(); wait_popup(true); check_focus(a);
    key(XK_Shift_L, true); tab(); check_focus(c); wait_popup(true);
    selected_image = XGetImage(display, overlay, 10, 65, 1, 1, AllPlanes, ZPixmap);
    assert(selected_image && XGetPixel(selected_image, 0, 0) == 0x536b7cUL);
    XDestroyImage(selected_image);
    snapshot("right-reverse"); key(XK_Shift_L, false); wait_popup(true);
    tab(); check_focus(a); tab(); check_focus(c);
    tab(); check_focus(a);
    /* Pointer is over Gamma underneath the popup, but release must keep Alpha. */
    XTestFakeMotionEvent(display, -1, 950, 400, CurrentTime); pause_ms(100);
    key(XK_Alt_L, false); wait_popup(false); check_focus(a); assert_ungrabbed();
    wait_property(b, "WM_STATE", IconicState);
    puts("PASS original forward/reverse order, excluded minimized/dock/no-input/other desktop, skip_taskbar eligible, right monitor, no overlay focus/EWMH/client");
    key(XK_Alt_L, true); tab(); key(XK_Alt_R, true);
    key(XK_Alt_L, false); wait_popup(true); key(XK_Alt_R, false); wait_popup(false); assert_ungrabbed();
    activate(a); key(XK_Alt_L, true); tab();
    XGetWindowAttributes(display, popup(true), &attr); assert(attr.x >= 0 && attr.x + attr.width <= 640);
    snapshot("left-monitor"); key(XK_Escape, true); key(XK_Escape, false); wait_popup(false); key(XK_Alt_L, false);
    assert_ungrabbed(); puts("PASS both Alt keys, Escape cancellation and left active monitor");
    XIconifyWindow(display, a, DefaultScreen(display)); wait_property(a, "WM_STATE", IconicState); activate(c);
    key(XK_Alt_L, true); tab(); wait_popup(true); check_focus(c);
    XGetWindowAttributes(display, popup(true), &attr); assert(attr.height == 64);
    key(XK_Alt_L, false); wait_popup(false);
    XIconifyWindow(display, c, DefaultScreen(display)); wait_property(c, "WM_STATE", IconicState);
    key(XK_Alt_L, true); tab(); wait_popup(false); key(XK_Alt_L, false); assert_ungrabbed();
    puts("PASS one and zero eligible windows");
    XMapWindow(display, a); XMapWindow(display, c); wait_property(c, "WM_STATE", NormalState); activate(c);
    key(XK_Alt_L, true); tab(); check_focus(a); wait_popup(true);
    XDestroyWindow(display, a); pause_ms(150); wait_popup(false); key(XK_Alt_L, false); check_focus(c); assert_ungrabbed();
    key(XK_Alt_L, true); tab(); wait_popup(true);
    message(root, "_NET_CURRENT_DESKTOP", 1, 0); wait_popup(false); key(XK_Alt_L, false);
    message(root, "_NET_CURRENT_DESKTOP", 0, 0); activate(c); assert_ungrabbed();
    puts("PASS selected client closed while held; desktop switch cancels; subsequent cycling remains usable");
    Window many[30];
    for (int i = 0; i < 30; ++i) { char name[64]; snprintf(name, sizeof(name), "Scrollable application %02d", i); many[i] = create(name, 50, true, NULL); }
    key(XK_Alt_L, true); tab(); wait_popup(true);
    for (int i = 0; i < 28; ++i) tab();
    XGetWindowAttributes(display, popup(true), &attr); assert(attr.height <= 800 && attr.x + attr.width <= 640);
    snapshot("scrolling"); key(XK_Alt_L, false); wait_popup(false);
    for (int i = 0; i < 30; ++i) { XDestroyWindow(display, many[i]); pause_ms(100); }
    pause_ms(200); activate(c); puts("PASS long list scrolls within monitor bounds");
    /* Exercise internal restart while the popup is mapped without changing real modifiers. */
    key(XK_Alt_L, true); tab(); wait_popup(true);
    XEvent restart = {0}; restart.xkey.type = KeyPress; restart.xkey.display = display;
    restart.xkey.window = root; restart.xkey.root = root; restart.xkey.same_screen = True;
    restart.xkey.keycode = XKeysymToKeycode(display, XK_r); restart.xkey.state = Mod4Mask | ShiftMask;
    XSendEvent(display, root, False, SubstructureNotifyMask, &restart); pause_ms(600);
    key(XK_Alt_L, false); wait_popup(false); check_focus(c); assert_ungrabbed();
    puts("PASS restart clears overlay and keyboard grab");
    for (int i = 0; i < 25; ++i) { key(XK_Alt_L, true); tab(); key(XK_Alt_L, false); wait_popup(false); }
    XDestroyWindow(display, b); XDestroyWindow(display, c); XDestroyWindow(display, dock);
    XDestroyWindow(display, noinput); XDestroyWindow(display, other); pause_ms(200);
    assert(errors == 0); XCloseDisplay(display);
    puts("PASS 25 repeat cycles, teardown and zero test X errors"); return 0;
}
