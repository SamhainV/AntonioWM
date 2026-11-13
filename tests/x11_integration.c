#include "wm.h"
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

WM wm;
static Window root;
static const char *config_path;
static unsigned int desktop_count = 4U;
static int errors;

static int on_error(Display *d, XErrorEvent *e)
{
    char message[128];
    XGetErrorText(d, e->error_code, message, sizeof(message));
    fprintf(stderr, "TEST XERROR %s request=%u xid=%lx\n", message, e->request_code, e->resourceid);
    ++errors;
    return 0;
}
static void pause_ms(long ms)
{
    struct timespec t = {ms / 1000, ms % 1000 * 1000000};
    XSync(wm.display, False);
    nanosleep(&t, NULL);
}
static Atom atom(const char *name) { return XInternAtom(wm.display, name, False); }
static unsigned long cardinal(Window w, const char *name)
{
    Atom type; int format; unsigned long n, after, value = ~0UL;
    unsigned char *data = NULL;
    XGetWindowProperty(wm.display, w, atom(name), 0, 1024, False, AnyPropertyType,
                       &type, &format, &n, &after, &data);
    if (format == 32 && n > 0) value = ((unsigned long *)data)[0];
    if (data != NULL) XFree(data);
    return value;
}
static Window named(const char *prefix, bool visible)
{
    Window r, parent, *children = NULL, found = None;
    unsigned int count;
    XQueryTree(wm.display, root, &r, &parent, &children, &count);
    for (unsigned int i = 0; i < count; ++i) {
        char *name = NULL; XWindowAttributes a;
        if (XFetchName(wm.display, children[i], &name) && name != NULL) {
            if (strncmp(name, prefix, strlen(prefix)) == 0 &&
                XGetWindowAttributes(wm.display, children[i], &a) &&
                (!visible || a.map_state == IsViewable)) found = children[i];
            XFree(name);
        }
    }
    if (children != NULL) XFree(children);
    return found;
}
static void wait_cardinal(Window w, const char *name, unsigned long value)
{
    for (int i = 0; i < 100; ++i) {
        if (cardinal(w, name) == value) return;
        pause_ms(20);
    }
    fprintf(stderr, "%s expected=%lu actual=%lu\n", name, value, cardinal(w, name));
    abort();
}
static void message(Window w, const char *name, long a, long b, long c, long d, long e)
{
    XEvent event = {0};
    event.xclient.type = ClientMessage; event.xclient.window = w;
    event.xclient.message_type = atom(name); event.xclient.format = 32;
    event.xclient.data.l[0] = a; event.xclient.data.l[1] = b;
    event.xclient.data.l[2] = c; event.xclient.data.l[3] = d; event.xclient.data.l[4] = e;
    XSendEvent(wm.display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    pause_ms(100);
}
static void key(KeySym symbol, bool down)
{
    XTestFakeKeyEvent(wm.display, XKeysymToKeycode(wm.display, symbol), down, CurrentTime);
}
static void chord(KeySym modifier, KeySym symbol)
{
    key(modifier, true); key(symbol, true); key(symbol, false); key(modifier, false);
    pause_ms(160);
}
static void config(unsigned int count, bool invalid)
{
    FILE *f = fopen(config_path, "w"); assert(f);
    fprintf(f, "desktop.count=%u\ndesktop.1.name=Trabajo\ndesktop.%u.name=Correo\n"
               "taskbar.launcher_count=0\ntaskbar.preview_delay=180\n", count, count);
    if (invalid) fputs("desktop.count=0\ndesktop.count=17\ndesktop.count=no\n", f);
    assert(fclose(f) == 0);
}
static void restart(unsigned int count, bool invalid)
{
    config(count, invalid);
    key(XK_Super_L, true); key(XK_Shift_L, true); key(XK_r, true);
    key(XK_r, false); key(XK_Shift_L, false); key(XK_Super_L, false);
    for (int i = 0; i < 200; ++i) {
        pause_ms(20);
        Window current = (Window)cardinal(root, "_NET_SUPPORTING_WM_CHECK");
        if (i >= 25 && current != (Window)~0UL &&
            cardinal(root, "_NET_NUMBER_OF_DESKTOPS") == count &&
            named("AntonioWM Panel", true) != None) {
            desktop_count = count; pause_ms(150); return;
        }
    }
    fputs("restart timed out\n", stderr); abort();
}
static Window create(const char *name)
{
    Window w = XCreateSimpleWindow(wm.display, root, 80, 100, 340, 180, 0, 0, 0x996633);
    XStoreName(wm.display, w, name);
    XMapWindow(wm.display, w);
    wait_cardinal(w, "WM_STATE", NormalState);
    return w;
}
static void net_icon(Window w, unsigned long color)
{
    unsigned long data[402]; data[0] = data[1] = 20;
    for (int i = 0; i < 400; ++i) data[i + 2] = i % 20 < 10 ? 0UL : color;
    XChangeProperty(wm.display, w, atom("_NET_WM_ICON"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)data, 402);
    pause_ms(100);
}
static Window panel(void) { Window w = named("AntonioWM Panel", true); assert(w); return w; }
static int task_start(void) { return 100 + (int)desktop_count * 30; }
static void hover(int index)
{
    XWindowAttributes a; XGetWindowAttributes(wm.display, panel(), &a);
    XTestFakeMotionEvent(wm.display, -1, a.x + task_start() + index * 204 + 100, a.y + 14, CurrentTime);
    pause_ms(40);
}
static void leave(void) { XTestFakeMotionEvent(wm.display, -1, 1000, 200, CurrentTime); pause_ms(40); }
static void click(void)
{
    XTestFakeButtonEvent(wm.display, 1, True, CurrentTime);
    XTestFakeButtonEvent(wm.display, 1, False, CurrentTime); pause_ms(120);
}
static unsigned long pixel(int x, int y)
{
    XImage *im = XGetImage(wm.display, panel(), x, y, 1, 1, AllPlanes, ZPixmap);
    assert(im); unsigned long p = XGetPixel(im, 0, 0); XDestroyImage(im); return p;
}
static void screenshot(const char *path)
{
    int width = DisplayWidth(wm.display, wm.screen), height = DisplayHeight(wm.display, wm.screen);
    XImage *im = XGetImage(wm.display, root, 0, 0, (unsigned)width, (unsigned)height, AllPlanes, ZPixmap);
    assert(im); FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        unsigned long p = XGetPixel(im, x, y);
        fputc((int)(p >> 16) & 255, f); fputc((int)(p >> 8) & 255, f); fputc((int)p & 255, f);
    }
    fclose(f); XDestroyImage(im);
}
static void names_check(unsigned int count)
{
    Atom t; int format; unsigned long n, after; unsigned char *data = NULL;
    XGetWindowProperty(wm.display, root, atom("_NET_DESKTOP_NAMES"), 0, 4096,
                       False, atom("UTF8_STRING"), &t, &format, &n, &after, &data);
    assert(format == 8 && data != NULL);
    unsigned int strings = 0;
    for (unsigned long i = 0; i < n; ++i) if (!data[i]) ++strings;
    assert(strings == count); assert(strcmp((char *)data, count == 1 ? "Correo" : "Trabajo") == 0);
    XFree(data);
}
typedef struct { Atom type; unsigned int count; } ResourceType;
static void resources(unsigned long *total, unsigned long *bytes)
{
    const char *library = getenv("AWM_XRES_LIBRARY");
    static void *handle;
    if (handle == NULL) handle = dlopen(library ? library : "libXRes.so.1", RTLD_NOW);
    assert(handle != NULL);
    int (*query)(Display *, XID, int *, ResourceType **);
    int (*pixmaps)(Display *, XID, unsigned long *);
    void *symbol = dlsym(handle, "XResQueryClientResources");
    memcpy(&query, &symbol, sizeof(query));
    symbol = dlsym(handle, "XResQueryClientPixmapBytes");
    memcpy(&pixmaps, &symbol, sizeof(pixmaps));
    assert(query && pixmaps);
    ResourceType *types = NULL; int count;
    XID owner = cardinal(root, "_NET_SUPPORTING_WM_CHECK");
    assert(query(wm.display, owner, &count, &types));
    *total = 0;
    for (int i = 0; i < count; ++i) *total += types[i].count;
    XFree(types);
    assert(pixmaps(wm.display, owner, bytes));
    /* Xlib retains extension callbacks until XCloseDisplay. Keep it loaded. */
}
static void repeat_cycle(Window w)
{
    Window temp = create("Resource cycle");
    net_icon(temp, 0x80ff0080UL);
    XIconifyWindow(wm.display, temp, wm.screen); pause_ms(15);
    XMapWindow(wm.display, temp); pause_ms(15);
    hover(0); pause_ms(200); leave();
    XDestroyWindow(wm.display, temp); pause_ms(20);
    assert(cardinal(w, "WM_STATE") == NormalState);
    assert(!named("AntonioWM Task Preview", true));
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    assert(argc == 2); config_path = argv[1];
    /* The runner owns :96. Never silently run this on the user's display. */
    assert(getenv("DISPLAY") && strcmp(getenv("DISPLAY"), ":96") == 0);
    wm.display = XOpenDisplay(NULL); assert(wm.display);
    wm.screen = DefaultScreen(wm.display); root = DefaultRootWindow(wm.display);
    wm.atoms.net_wm_icon = atom("_NET_WM_ICON"); XSetErrorHandler(on_error);
    wait_cardinal(root, "_NET_NUMBER_OF_DESKTOPS", 4);
    Window a = create("ARGB application - a very long title that must be truncated in the taskbar safely");
    net_icon(a, 0x80ff0000UL);
    Window b = create("WM_HINTS application");
    Pixmap pixmap = XCreatePixmap(wm.display, root, 20, 20, (unsigned)DefaultDepth(wm.display, wm.screen));
    Pixmap mask = XCreatePixmap(wm.display, root, 20, 20, 1);
    GC gc = XCreateGC(wm.display, pixmap, 0, NULL);
    XSetForeground(wm.display, gc, 0x0000ff); XFillRectangle(wm.display, pixmap, gc, 0, 0, 20, 20);
    XFreeGC(wm.display, gc); gc = XCreateGC(wm.display, mask, 0, NULL);
    XSetForeground(wm.display, gc, 0); XFillRectangle(wm.display, mask, gc, 0, 0, 20, 20);
    XSetForeground(wm.display, gc, 1); XFillRectangle(wm.display, mask, gc, 10, 0, 10, 20);
    XFreeGC(wm.display, gc);
    XWMHints hints = {0}; hints.flags = IconPixmapHint | IconMaskHint;
    hints.icon_pixmap = pixmap; hints.icon_mask = mask; XSetWMHints(wm.display, b, &hints);
    Window c = create("No icon - generic window glyph"); pause_ms(200);
    IconImage icon = {0}; assert(icon_load_client(b, 20, &icon));
    assert(icon.rgba[3] == 0 && icon.rgba[10 * 4 + 2] == 255 && icon.rgba[10 * 4 + 3] == 255);
    icon_free(&icon);
    int start = task_start();
    unsigned long bg = pixel(start + 8, 23);
    assert(pixel(start + 8, 12) == bg); /* fully transparent half */
    unsigned long expected = ((((bg >> 16) & 255) * 127 + 255 * 128 + 127) / 255) << 16 |
                             ((((bg >> 8) & 255) * 127 + 127) / 255) << 8 |
                             (((bg & 255) * 127 + 127) / 255);
    assert(pixel(start + 18, 12) == expected);
    assert(pixel(start + 204 + 18, 12) == 0x0000ffUL);
    unsigned long *large = malloc((512U * 512U + 2U) * sizeof(*large));
    assert(large != NULL); large[0] = large[1] = 512UL;
    for (size_t i = 2; i < 512U * 512U + 2U; ++i) large[i] = 0xff3366ccUL;
    XChangeProperty(wm.display, a, atom("_NET_WM_ICON"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)large, 512 * 512 + 2);
    free(large); pause_ms(100);
    assert(pixel(start + 18, 12) == 0x3366ccUL);
    assert(icon_load_client(a, 20, &icon) && icon.width == 512U); icon_free(&icon);
    unsigned long malformed[] = {2049UL, 1UL, 0xffffffffUL};
    XChangeProperty(wm.display, a, atom("_NET_WM_ICON"), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)malformed, 3);
    pause_ms(100); assert(!icon_load_client(a, 20, &icon));
    assert(pixel(start + 18, 12) != 0x3366ccUL);
    XChangeProperty(wm.display, a, atom("_NET_WM_ICON"), XA_STRING, 8,
                    PropModeReplace, (unsigned char *)"bad", 3);
    pause_ms(100); assert(!icon_load_client(a, 20, &icon));
    net_icon(a, 0x80ff0000UL);
    screenshot("tests/evidence/icons.ppm");
    net_icon(a, 0xff00ff00UL); assert(pixel(start + 18, 12) == 0x00ff00UL);
    XDeleteProperty(wm.display, a, atom("_NET_WM_ICON")); pause_ms(100);
    assert(pixel(start + 18, 12) != 0x00ff00UL); net_icon(a, 0x80ff0000UL);
    /* Changes of WM_HINTS invalidate the cache as well. */
    gc = XCreateGC(wm.display, pixmap, 0, NULL); XSetForeground(wm.display, gc, 0xffff00);
    XFillRectangle(wm.display, pixmap, gc, 0, 0, 20, 20); XFreeGC(wm.display, gc);
    XSetWMHints(wm.display, b, &hints); pause_ms(100);
    assert(pixel(start + 204 + 18, 12) == 0xffff00UL);
    /* Stale client pixmap must fail locally, without aborting or leaking XImages. */
    XFreePixmap(wm.display, pixmap); XFreePixmap(wm.display, mask);
    assert(!icon_load_client(b, 20, &icon)); XSetWMHints(wm.display, b, &hints); pause_ms(100);
    puts("PASS rendered ARGB alpha, 512px/malformed/type-invalid icons, property changes/deletion, WM_HINTS/mask/stale pixmap, generic fallback");
    hover(0); pause_ms(260); assert(named("AntonioWM Task Preview", true));
    screenshot("tests/evidence/preview.ppm");
    leave(); assert(!named("AntonioWM Task Preview", true));
    XIconifyWindow(wm.display, a, wm.screen); wait_cardinal(a, "WM_STATE", IconicState);
    hover(0); click(); wait_cardinal(a, "WM_STATE", NormalState);
    wait_cardinal(root, "_NET_ACTIVE_WINDOW", a); leave();
    chord(XK_Alt_L, XK_Tab); assert(cardinal(root, "_NET_ACTIVE_WINDOW") != a);
    for (int i = 0; i < 25; ++i) { hover(i % 3); }
    leave(); assert(!named("AntonioWM Task Preview", true));
    Window temporary = create("Close during preview delay"); hover(3);
    XDestroyWindow(wm.display, temporary); pause_ms(260); assert(!named("AntonioWM Task Preview", true));
    hover(2); pause_ms(260); assert(named("AntonioWM Task Preview", true));
    XDestroyWindow(wm.display, c); pause_ms(120); assert(!named("AntonioWM Task Preview", true)); leave();
    puts("PASS preview, minimize/restore by task click, focus/Alt+Tab, rapid hover, close pending/visible");
    for (unsigned int i = 0; i < 4; ++i) {
        unsigned int counts[] = {1, 4, 6, 16};
        restart(counts[i], false); names_check(counts[i]);
        wait_cardinal(a, "WM_STATE", NormalState);
        printf("PASS desktop count=%u names/EWMH and live restart\n", counts[i]);
        if (counts[i] == 6) screenshot("tests/evidence/desktops6.ppm");
        if (counts[i] == 16) screenshot("tests/evidence/desktops16.ppm");
    }
    message(a, "_NET_WM_DESKTOP", 15, 2, 0, 0, 0);
    message(root, "_NET_CURRENT_DESKTOP", 15, 0, 0, 0, 0);
    wait_cardinal(root, "_NET_CURRENT_DESKTOP", 15);
    restart(4, true);
    wait_cardinal(a, "_NET_WM_DESKTOP", 3); wait_cardinal(root, "_NET_CURRENT_DESKTOP", 3);
    names_check(4); puts("PASS invalid count and shrink 16->4 migrates live window and current desktop");
    for (int i = 0; i < 4; ++i) restart(4, false);
    puts("PASS four further hot restarts with clients open");
    message(root, "_NET_CURRENT_DESKTOP", 3, 0, 0, 0, 0);
    message(a, "_NET_ACTIVE_WINDOW", 2, 0, 0, 0, 0);
    message(a, "_NET_WM_STATE", 1, (long)atom("_NET_WM_STATE_FULLSCREEN"), 0, 2, 0);
    XWindowAttributes attr; XGetWindowAttributes(wm.display, a, &attr);
    assert(attr.width == DisplayWidth(wm.display, wm.screen));
    message(a, "_NET_WM_STATE", 0, (long)atom("_NET_WM_STATE_FULLSCREEN"), 0, 2, 0);
    for (int i = 0; i < 5; ++i) repeat_cycle(a);
    unsigned long before_count, after_count, before_bytes, after_bytes;
    resources(&before_count, &before_bytes);
    for (int i = 0; i < 40; ++i) repeat_cycle(a);
    resources(&after_count, &after_bytes);
    printf("XRes warm baseline=%lu resources/%lu pixmap bytes; after 40 cycles=%lu/%lu\n",
           before_count, before_bytes, after_count, after_bytes);
    assert(after_count <= before_count + 2 && after_bytes <= before_bytes + 4096);
    XDestroyWindow(wm.display, a); XDestroyWindow(wm.display, b); pause_ms(200);
    assert(!named("AntonioWM Task Preview", true));
    assert(errors == 0); XCloseDisplay(wm.display);
    puts("PASS fullscreen and clean client teardown; no untrapped test X errors");
    return 0;
}
