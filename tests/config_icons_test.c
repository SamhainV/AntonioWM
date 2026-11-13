#include "wm.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

WM wm;
XWMHints *XGetWMHints(Display *display, Window window)
{
    (void)display; (void)window;
    return NULL;
}

static unsigned long property[32];
static unsigned long property_count;
static unsigned long property_after;

/* Isolate the property parser from the server, preserving Xlib LP64 layout. */
int XGetWindowProperty(Display *display, Window window, Atom atom,
                       long offset, long length, Bool delete_property,
                       Atom requested_type, Atom *type, int *format,
                       unsigned long *count, unsigned long *after,
                       unsigned char **data)
{
    (void)display; (void)window; (void)atom; (void)offset;
    (void)length; (void)delete_property; (void)requested_type;
    *type = XA_CARDINAL; *format = 32; *count = property_count;
    *after = property_after;
    *data = malloc(sizeof(property));
    assert(*data != NULL);
    memcpy(*data, property, sizeof(property));
    return Success;
}

static void config_case(const char *text, unsigned int expected)
{
    char path[] = "/tmp/awm-config-XXXXXX";
    int fd = mkstemp(path);
    FILE *file;
    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
    assert(setenv("ANTONIOWM_CONFIG", path, 1) == 0);
    runtime_config_load();
    assert(wm.config.desktop_count == expected);
    assert(unlink(path) == 0);
}

int main(void)
{
    IconImage image = {0};
    config_case("", 4U);
    config_case("desktop.count=1\n", 1U);
    config_case("desktop.count=6\ndesktop.6.name=Correo\n", 6U);
    assert(strcmp(wm.config.desktop_names[5], "Correo") == 0);
    config_case("desktop.count=0\ndesktop.count=17\ndesktop.count=no\n", 4U);
    config_case("desktop.count=4\n", 4U);
    property_count = 3UL;
    property[0] = 1UL; property[1] = 1UL; property[2] = 0x80402010UL;
    assert(icon_load_client(1, 16, &image));
    assert(image.rgba[0] == 0x40 && image.rgba[1] == 0x20 &&
           image.rgba[2] == 0x10 && image.rgba[3] == 0x80);
    icon_free(&image);
    property[0] = 2049UL;
    assert(!icon_load_client(1, 16, &image));
    property[0] = 0UL;
    assert(!icon_load_client(1, 16, &image));
    property[0] = 2UL; property[1] = 2UL;
    assert(!icon_load_client(1, 16, &image));
    /* Two resolutions: choose the smallest adequate, then the largest fallback. */
    property_count = 9UL;
    property[0] = 1UL; property[1] = 1UL; property[2] = 0xff000000UL;
    property[3] = 2UL; property[4] = 2UL;
    property[5] = property[6] = property[7] = property[8] = 0xffffffffUL;
    assert(icon_load_client(1, 2, &image) && image.width == 2U);
    assert(icon_load_client(1, 1, &image) && image.width == 1U);
    assert(icon_load_client(1, 16, &image) && image.width == 2U);
    icon_free(&image);
    property_after = 4UL;
    assert(!icon_load_client(1, 16, &image));
    puts("Config and icon parser tests passed");
    return 0;
}
