#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool parse_unsigned(const char *text, unsigned long maximum, unsigned long *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = parsed;
    return true;
}

int main(int argc, char **argv)
{
    Display *display;
    unsigned long window_value;
    unsigned long x_value;
    unsigned long y_value;
    XImage *image;
    XColor color;
    int screen;
    bool is_red;

    if (argc != 4 ||
        !parse_unsigned(argv[1], ULONG_MAX, &window_value) ||
        !parse_unsigned(argv[2], (unsigned long)INT_MAX, &x_value) ||
        !parse_unsigned(argv[3], (unsigned long)INT_MAX, &y_value)) {
        fprintf(stderr, "uso: %s WINDOW X Y\n", argv[0]);
        return EXIT_FAILURE;
    }
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "pixel_probe: no se pudo abrir DISPLAY\n");
        return EXIT_FAILURE;
    }
    screen = DefaultScreen(display);
    image = XGetImage(display,
                      (Window)window_value,
                      (int)x_value,
                      (int)y_value,
                      1U,
                      1U,
                      AllPlanes,
                      ZPixmap);
    if (image == NULL) {
        fprintf(stderr, "pixel_probe: XGetImage fallo\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    color.pixel = XGetPixel(image, 0, 0);
    XDestroyImage(image);
    if (XQueryColor(display, DefaultColormap(display, screen), &color) == 0) {
        fprintf(stderr, "pixel_probe: XQueryColor fallo\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }
    is_red = color.red > 45000U && color.green < 15000U && color.blue < 15000U;
    if (!is_red) {
        fprintf(stderr,
                "pixel_probe: se esperaba rojo y se obtuvo %u,%u,%u\n",
                color.red,
                color.green,
                color.blue);
    }
    XCloseDisplay(display);
    return is_red ? EXIT_SUCCESS : EXIT_FAILURE;
}
