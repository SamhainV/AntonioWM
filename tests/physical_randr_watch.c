#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void)
{
    const char *display_name = getenv("DISPLAY");
    if (display_name == NULL || strcmp(display_name, ":1") != 0 ||
        getenv("ANTONIOWM_PHYSICAL_RUN") == NULL) {
        fputs("Only the prepared physical test session :1 is allowed.\n", stderr);
        return EXIT_FAILURE;
    }
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) return EXIT_FAILURE;
    int event_base, error_base, major = 1, minor = 5;
    if (!XRRQueryExtension(display, &event_base, &error_base) ||
        !XRRQueryVersion(display, &major, &minor)) {
        fputs("RandR unavailable\n", stderr);
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("RandR %d.%d; subscribed on %s\n", major, minor, display_name);
    XRRSelectInput(display, DefaultRootWindow(display), RRScreenChangeNotifyMask |
                   RRCrtcChangeNotifyMask | RROutputChangeNotifyMask |
                   RROutputPropertyNotifyMask);
    XSync(display, False);
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (event.type == event_base + RRScreenChangeNotify) {
            XRRScreenChangeNotifyEvent *screen = (XRRScreenChangeNotifyEvent *)&event;
            XRRUpdateConfiguration(&event);
            printf("%lld.%03ld screen=%dx%d rotation=%u\n",
                   (long long)now.tv_sec, now.tv_nsec / 1000000,
                   screen->width, screen->height, (unsigned)screen->rotation);
        } else if (event.type == event_base + RRNotify) {
            XRRNotifyEvent *notify = (XRRNotifyEvent *)&event;
            printf("%lld.%03ld notify subtype=%d", (long long)now.tv_sec,
                   now.tv_nsec / 1000000, notify->subtype);
            if (notify->subtype == RRNotify_OutputChange) {
                XRROutputChangeNotifyEvent *output = (XRROutputChangeNotifyEvent *)&event;
                printf(" output=0x%lx connection=%d crtc=0x%lx mode=0x%lx",
                       output->output, output->connection, output->crtc, output->mode);
            }
            putchar('\n');
        }
    }
}
