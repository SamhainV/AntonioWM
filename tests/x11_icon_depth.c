#include "wm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
WM wm;
int main(void)
{
    const char *display = getenv("DISPLAY");
    assert(display && (!strcmp(display, ":121") || !strcmp(display, ":122")));
    wm.display = XOpenDisplay(NULL); assert(wm.display);
    wm.screen = DefaultScreen(wm.display); wm.root = DefaultRootWindow(wm.display);
    wm.atoms.net_wm_icon = XInternAtom(wm.display, "_NET_WM_ICON", False);
    Window w = XCreateSimpleWindow(wm.display, wm.root, 30, 50, 260, 150, 0, 0, 0);
    XStoreName(wm.display, w, "Depth icon client");
    unsigned long data[] = {1, 1, 0xffff0000UL};
    XChangeProperty(wm.display, w, wm.atoms.net_wm_icon, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)data, 3);
    XMapWindow(wm.display, w); XSync(wm.display, False);
    struct timespec delay = {0, 500000000}; nanosleep(&delay, NULL);
    IconImage icon = {0}; assert(icon_load_client(w, 20, &icon));
    wm.draw_gc = XCreateGC(wm.display, w, 0, NULL);
    Visual *v = DefaultVisual(wm.display, wm.screen);
    bool drawn = icon_draw(w, &icon, 10, 10, 20, 20, BlackPixel(wm.display, wm.screen));
    assert(drawn == (v->class == TrueColor));
    if (drawn) {
        XImage *im = XGetImage(wm.display, w, 15, 15, 1, 1, AllPlanes, ZPixmap);
        assert(im && XGetPixel(im, 0, 0) == v->red_mask); XDestroyImage(im);
    }
    icon_free(&icon); XDeleteProperty(wm.display, w, wm.atoms.net_wm_icon);
    Pixmap p = XCreatePixmap(wm.display, wm.root, 2, 1, 1);
    GC gc = XCreateGC(wm.display, p, 0, NULL);
    XSetForeground(wm.display, gc, 0); XDrawPoint(wm.display, p, gc, 0, 0);
    XSetForeground(wm.display, gc, 1); XDrawPoint(wm.display, p, gc, 1, 0);
    XWMHints hints = {0}; hints.flags = IconPixmapHint; hints.icon_pixmap = p;
    XSetWMHints(wm.display, w, &hints);
    assert(icon_load_client(w, 20, &icon));
    assert(icon.width == 2 && icon.rgba[0] == 0 && icon.rgba[4] == 255);
    icon_free(&icon); XSync(wm.display, False); nanosleep(&delay, NULL);
    printf("PASS depth=%d visual=%d net icon draw=%d; monochrome WM_HINTS decoded\n",
           DefaultDepth(wm.display, wm.screen), v->class, drawn);
    /* Capture the WM's taskbar before disconnecting this client. */
    const char *out = !strcmp(display, ":121") ? "tests/evidence/depth16.xwd" : "tests/evidence/depth8.xwd";
    char command[256]; snprintf(command, sizeof(command), "xwd -root -silent -out %s", out);
    assert(system(command) == 0);
    XFreeGC(wm.display, gc); XFreeGC(wm.display, wm.draw_gc);
    XDestroyWindow(wm.display, w); XFreePixmap(wm.display, p);
    XCloseDisplay(wm.display); return 0;
}
