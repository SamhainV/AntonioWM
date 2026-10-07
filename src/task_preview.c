#include "wm.h"
#include "config.h"

#include <X11/extensions/Xrender.h>

#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* XComposite is loaded at runtime so a missing optional extension or shared
 * library never prevents AntonioWM from starting.  These are the public API
 * signatures from Xlib's Xcomposite.h. */
typedef Bool (*CompositeQueryExtension)(Display *, int *, int *);
typedef void (*CompositeRedirectWindow)(Display *, Window, int);
typedef void (*CompositeUnredirectWindow)(Display *, Window, int);
typedef Pixmap (*CompositeNameWindowPixmap)(Display *, Window);

enum {
    COMPOSITE_REDIRECT_AUTOMATIC = 0
};

typedef struct {
    void *library;
    CompositeQueryExtension query_extension;
    CompositeRedirectWindow redirect_window;
    CompositeUnredirectWindow unredirect_window;
    CompositeNameWindowPixmap name_window_pixmap;
    bool available;
    bool pending;
    bool visible;
    Window window;
    Window client_window;
    int anchor_x;
    int anchor_width;
    int panel_y;
    int width;
    int height;
    int64_t deadline_ms;
} TaskPreview;

static TaskPreview preview;

static int minimum_int(int first, int second)
{
    return first < second ? first : second;
}

static int maximum_int(int first, int second)
{
    return first > second ? first : second;
}

static int64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool load_symbol(void *library,
                        const char *name,
                        void *destination,
                        size_t destination_size)
{
    void *symbol;

    if (destination_size != sizeof(symbol)) {
        return false;
    }
    dlerror();
    symbol = dlsym(library, name);
    if (symbol == NULL || dlerror() != NULL) {
        return false;
    }
    memcpy(destination, &symbol, sizeof(symbol));
    return true;
}

static bool eligible_client(const Client *client)
{
    return client != NULL && client->mapped && desktop_is_current(client) &&
           client_is_taskbar_visible(client) &&
           (client->minimized || client->visible);
}

static void unredirect_client(Client *client, bool destroyed)
{
    if (client == NULL) {
        return;
    }
    if (client->preview_pixmap != None) {
        XFreePixmap(wm.display, client->preview_pixmap);
        client->preview_pixmap = None;
    }
    client->preview_pixmap_width = 0U;
    client->preview_pixmap_height = 0U;
    if (client->preview_redirected && !destroyed && preview.available) {
        preview.unredirect_window(wm.display,
                                  client->window,
                                  COMPOSITE_REDIRECT_AUTOMATIC);
    }
    client->preview_redirected = false;
}

static bool redirect_client(Client *client)
{
    if (!preview.available || client == NULL || client->window == None) {
        return false;
    }
    if (!client->preview_redirected) {
        preview.redirect_window(wm.display,
                                client->window,
                                COMPOSITE_REDIRECT_AUTOMATIC);
        client->preview_redirected = true;
        XSync(wm.display, False);
    }
    return true;
}

static bool name_client_pixmap(Client *client,
                               Pixmap *pixmap,
                               unsigned int *width,
                               unsigned int *height)
{
    Window root;
    int x;
    int y;
    unsigned int border;
    unsigned int depth;

    if (!redirect_client(client)) {
        return false;
    }
    *pixmap = preview.name_window_pixmap(wm.display, client->window);
    if (*pixmap == None) {
        return false;
    }
    XSync(wm.display, False);
    if (XGetGeometry(wm.display,
                     *pixmap,
                     &root,
                     &x,
                     &y,
                     width,
                     height,
                     &border,
                     &depth) == 0 ||
        *width == 0U || *height == 0U) {
        XFreePixmap(wm.display, *pixmap);
        *pixmap = None;
        return false;
    }
    return true;
}

static bool acquire_source(Client *client,
                           Pixmap *pixmap,
                           unsigned int *width,
                           unsigned int *height,
                           bool *temporary)
{
    if (client->minimized && client->preview_pixmap != None) {
        *pixmap = client->preview_pixmap;
        *width = client->preview_pixmap_width;
        *height = client->preview_pixmap_height;
        *temporary = false;
        return *width > 0U && *height > 0U;
    }
    if (!client->visible) {
        return false;
    }
    if (!name_client_pixmap(client, pixmap, width, height)) {
        unredirect_client(client, false);
        return false;
    }
    *temporary = true;
    return true;
}

static void release_source(Client *client, Pixmap pixmap, bool temporary)
{
    if (!temporary) {
        return;
    }
    if (pixmap != None) {
        XFreePixmap(wm.display, pixmap);
    }
    if (client != NULL && client->preview_pixmap == None) {
        unredirect_client(client, false);
    }
}

static void scaled_dimensions(unsigned int source_width,
                              unsigned int source_height,
                              unsigned int maximum_width,
                              unsigned int maximum_height,
                              unsigned int *width,
                              unsigned int *height)
{
    uint64_t scaled_width = source_width;
    uint64_t scaled_height = source_height;

    if (scaled_width > maximum_width) {
        scaled_height = scaled_height * maximum_width / scaled_width;
        scaled_width = maximum_width;
    }
    if (scaled_height > maximum_height) {
        scaled_width = scaled_width * maximum_height / scaled_height;
        scaled_height = maximum_height;
    }
    *width = (unsigned int)maximum_int((int)scaled_width, 1);
    *height = (unsigned int)maximum_int((int)scaled_height, 1);
}

static int preview_title_height(void)
{
    return maximum_int(wm.font_ascent + wm.font_descent + 4,
                       (int)AWM_TITLE_HEIGHT);
}

static void preview_monitor(Monitor *monitor)
{
    monitor_for_rectangle(preview.anchor_x,
                          preview.panel_y,
                          maximum_int(preview.anchor_width, 1),
                          maximum_int((int)wm.config.taskbar_height, 1),
                          monitor);
}

static void position_window(void)
{
    Monitor monitor;
    int x;
    int y;
    int maximum_x;
    int maximum_y;

    if (preview.window == None || preview.width <= 0 || preview.height <= 0) {
        return;
    }
    preview_monitor(&monitor);
    x = preview.anchor_x + preview.anchor_width / 2 - preview.width / 2;
    maximum_x = monitor.x + monitor.width - preview.width;
    if (maximum_x < monitor.x) {
        maximum_x = monitor.x;
    }
    if (x < monitor.x) {
        x = monitor.x;
    } else if (x > maximum_x) {
        x = maximum_x;
    }

    y = preview.panel_y - preview.height - wm.config.taskbar_gap;
    maximum_y = monitor.y + monitor.height - preview.height;
    if (maximum_y < monitor.y) {
        maximum_y = monitor.y;
    }
    if (y < monitor.y) {
        y = monitor.y;
    } else if (y > maximum_y) {
        y = maximum_y;
    }
    XMoveWindow(wm.display, preview.window, x, y);
}

static bool create_window(void)
{
    XSetWindowAttributes attributes;
    unsigned long mask;

    if (preview.window != None) {
        return true;
    }
    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = wm.bar_background;
    attributes.event_mask = ExposureMask;
    attributes.cursor = wm.normal_cursor;
    mask = CWOverrideRedirect | CWBackPixel | CWEventMask;
    if (wm.normal_cursor != None) {
        mask |= CWCursor;
    }
    preview.window = XCreateWindow(wm.display,
                                   wm.root,
                                   0,
                                   0,
                                   1U,
                                   1U,
                                   0U,
                                   CopyFromParent,
                                   InputOutput,
                                   CopyFromParent,
                                   mask,
                                   &attributes);
    if (preview.window == None) {
        return false;
    }
    XStoreName(wm.display, preview.window, "AntonioWM Task Preview");
    return true;
}

static bool render_image(Client *client,
                         Pixmap pixmap,
                         unsigned int source_width,
                         unsigned int source_height,
                         int destination_x,
                         int destination_y,
                         unsigned int destination_width,
                         unsigned int destination_height)
{
    XWindowAttributes client_attributes;
    XRenderPictFormat *source_format;
    XRenderPictFormat *destination_format;
    Picture source_picture;
    Picture destination_picture;
    XTransform transform = {{
        {XDoubleToFixed(1.0), XDoubleToFixed(0.0), XDoubleToFixed(0.0)},
        {XDoubleToFixed(0.0), XDoubleToFixed(1.0), XDoubleToFixed(0.0)},
        {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)}
    }};

    if (XGetWindowAttributes(wm.display,
                             client->window,
                             &client_attributes) == 0) {
        return false;
    }
    source_format = XRenderFindVisualFormat(wm.display,
                                             client_attributes.visual);
    destination_format = XRenderFindVisualFormat(
        wm.display,
        DefaultVisual(wm.display, wm.screen));
    if (source_format == NULL || destination_format == NULL) {
        return false;
    }
    source_picture = XRenderCreatePicture(wm.display,
                                           pixmap,
                                           source_format,
                                           0UL,
                                           NULL);
    destination_picture = XRenderCreatePicture(wm.display,
                                                preview.window,
                                                destination_format,
                                                0UL,
                                                NULL);
    if (source_picture == None || destination_picture == None) {
        if (source_picture != None) {
            XRenderFreePicture(wm.display, source_picture);
        }
        if (destination_picture != None) {
            XRenderFreePicture(wm.display, destination_picture);
        }
        return false;
    }

    transform.matrix[0][0] =
        XDoubleToFixed((double)source_width / destination_width);
    transform.matrix[1][1] =
        XDoubleToFixed((double)source_height / destination_height);
    XRenderSetPictureTransform(wm.display, source_picture, &transform);
    XRenderSetPictureFilter(wm.display,
                            source_picture,
                            FilterBilinear,
                            NULL,
                            0);
    XRenderComposite(wm.display,
                     PictOpSrc,
                     source_picture,
                     None,
                     destination_picture,
                     0,
                     0,
                     0,
                     0,
                     destination_x,
                     destination_y,
                     destination_width,
                     destination_height);
    XRenderFreePicture(wm.display, destination_picture);
    XRenderFreePicture(wm.display, source_picture);
    return true;
}

static void set_debug_name(const Client *client)
{
    char name[512];

    (void)snprintf(name,
                   sizeof(name),
                   "AntonioWM Task Preview - %s",
                   client->title != NULL ? client->title : "(sin titulo)");
    XStoreName(wm.display, preview.window, name);
}

static bool draw_preview(Client *client)
{
    const int border = (int)AWM_TASKBAR_PREVIEW_BORDER_WIDTH;
    const int padding = AWM_TASKBAR_PREVIEW_PADDING;
    Monitor monitor;
    Pixmap source = None;
    unsigned int source_width = 1U;
    unsigned int source_height = 1U;
    unsigned int image_width;
    unsigned int image_height;
    unsigned int maximum_width;
    unsigned int maximum_height;
    bool temporary = false;
    bool have_source;
    int title_height = preview_title_height();
    int chrome_width = 2 * (border + padding);
    int chrome_height = 2 * (border + padding) + title_height;
    int image_x;
    int image_y;
    int inner_width;
    int available_height;

    preview_monitor(&monitor);
    available_height = preview.panel_y - monitor.y -
                       wm.config.taskbar_gap - chrome_height;
    if (monitor.width <= chrome_width || available_height <= 0) {
        /* There is no fully contained rectangle above the task button. */
        return false;
    }
    maximum_width = wm.config.taskbar_preview_width;
    maximum_height = wm.config.taskbar_preview_height;
    maximum_width = (unsigned int)minimum_int((int)maximum_width,
                                               monitor.width - chrome_width);
    maximum_height = (unsigned int)minimum_int((int)maximum_height,
                                                available_height);
    maximum_width = maximum_width > 0U ? maximum_width : 1U;
    maximum_height = maximum_height > 0U ? maximum_height : 1U;

    have_source = acquire_source(client,
                                 &source,
                                 &source_width,
                                 &source_height,
                                 &temporary);
    if (!have_source) {
        source_width = minimum_int((int)maximum_width, 180);
        source_height = minimum_int((int)maximum_height, 100);
    }
    scaled_dimensions(source_width,
                      source_height,
                      maximum_width,
                      maximum_height,
                      &image_width,
                      &image_height);
    inner_width = maximum_int((int)image_width,
                              minimum_int((int)maximum_width, 180));
    preview.width = inner_width + chrome_width;
    preview.height = (int)image_height + chrome_height;
    XResizeWindow(wm.display,
                  preview.window,
                  (unsigned int)preview.width,
                  (unsigned int)preview.height);
    position_window();
    if (!preview.visible) {
        /* Map only after sizing and positioning: no 1x1 flash at (0,0). */
        XMapRaised(wm.display, preview.window);
        preview.visible = true;
    }

    draw_fill(preview.window,
              wm.bar_background,
              0,
              0,
              (unsigned int)preview.width,
              (unsigned int)preview.height);
    draw_rectangle(preview.window,
                   wm.bar_separator,
                   0,
                   0,
                   (unsigned int)maximum_int(preview.width - 1, 1),
                   (unsigned int)maximum_int(preview.height - 1, 1));
    draw_text_ellipsized(preview.window,
                         wm.bar_foreground,
                         border + padding,
                         border + draw_text_baseline(title_height),
                         border + padding,
                         border,
                         (unsigned int)maximum_int(inner_width, 1),
                         (unsigned int)title_height,
                         client->title != NULL ? client->title : "(sin titulo)");
    image_x = border + padding + (inner_width - (int)image_width) / 2;
    image_y = border + title_height + padding;
    draw_fill(preview.window,
              wm.bar_item,
              image_x,
              image_y,
              image_width,
              image_height);
    if (have_source) {
        (void)render_image(client,
                           source,
                           source_width,
                           source_height,
                           image_x,
                           image_y,
                           image_width,
                           image_height);
    } else {
        draw_text_ellipsized(preview.window,
                             wm.title_text_inactive,
                             image_x + padding,
                             image_y + draw_text_baseline((int)image_height),
                             image_x + padding,
                             image_y,
                             (unsigned int)maximum_int((int)image_width -
                                                          2 * padding,
                                                      1),
                             image_height,
                             "Vista no disponible");
    }
    set_debug_name(client);
    release_source(client, source, temporary);
    return true;
}

static void show_preview(void)
{
    Client *client = client_find(preview.client_window);

    preview.pending = false;
    if (!preview.available || menu_is_open() || !eligible_client(client) ||
        !create_window()) {
        task_preview_cancel();
        return;
    }
    if (!draw_preview(client)) {
        task_preview_cancel();
        return;
    }
    XRaiseWindow(wm.display, preview.window);
    XFlush(wm.display);
}

bool task_preview_initialize(void)
{
    int composite_event;
    int composite_error;
    int render_event;
    int render_error;

    memset(&preview, 0, sizeof(preview));
    if (!wm.config.taskbar_enabled || !wm.config.taskbar_preview_enabled ||
        wm.config.taskbar_height == 0U) {
        return true;
    }
    preview.library = dlopen("libXcomposite.so.1", RTLD_NOW | RTLD_LOCAL);
    if (preview.library == NULL ||
        !load_symbol(preview.library,
                     "XCompositeQueryExtension",
                     &preview.query_extension,
                     sizeof(preview.query_extension)) ||
        !load_symbol(preview.library,
                     "XCompositeRedirectWindow",
                     &preview.redirect_window,
                     sizeof(preview.redirect_window)) ||
        !load_symbol(preview.library,
                     "XCompositeUnredirectWindow",
                     &preview.unredirect_window,
                     sizeof(preview.unredirect_window)) ||
        !load_symbol(preview.library,
                     "XCompositeNameWindowPixmap",
                     &preview.name_window_pixmap,
                     sizeof(preview.name_window_pixmap))) {
        fprintf(stderr,
                "%s: XComposite no esta disponible; previews de tareas desactivadas\n",
                AWM_NAME);
        if (preview.library != NULL) {
            dlclose(preview.library);
            preview.library = NULL;
        }
        return true;
    }
    if (preview.query_extension(wm.display,
                                &composite_event,
                                &composite_error) == 0 ||
        XRenderQueryExtension(wm.display, &render_event, &render_error) == 0) {
        fprintf(stderr,
                "%s: el servidor X no ofrece Composite/Render; "
                "previews de tareas desactivadas\n",
                AWM_NAME);
        /* XCompositeQueryExtension registers Xlib extension hooks.  Keep the
         * DSO loaded until after XCloseDisplay has called those hooks. */
        return true;
    }
    preview.available = true;
    return true;
}

void task_preview_cleanup(void)
{
    task_preview_cancel();
    if (preview.window != None) {
        XDestroyWindow(wm.display, preview.window);
        preview.window = None;
    }
    preview.available = false;
}

void task_preview_close_library(void)
{
    if (preview.library != NULL) {
        dlclose(preview.library);
        preview.library = NULL;
    }
}

void task_preview_schedule(Window client_window,
                           int button_root_x,
                           int button_width,
                           int panel_root_y)
{
    Client *client;

    if (!preview.available || client_window == None || menu_is_open()) {
        task_preview_cancel();
        return;
    }
    client = client_find(client_window);
    if (!eligible_client(client)) {
        task_preview_cancel();
        return;
    }
    if ((preview.pending || preview.visible) &&
        preview.client_window == client_window) {
        preview.anchor_x = button_root_x;
        preview.anchor_width = button_width;
        preview.panel_y = panel_root_y;
        if (preview.visible) {
            position_window();
        }
        return;
    }

    task_preview_cancel();
    preview.client_window = client_window;
    preview.anchor_x = button_root_x;
    preview.anchor_width = button_width;
    preview.panel_y = panel_root_y;
    preview.deadline_ms = monotonic_milliseconds() +
                          (int64_t)wm.config.taskbar_preview_delay;
    preview.pending = true;
}

void task_preview_reanchor(Window client_window,
                           int button_root_x,
                           int button_width,
                           int panel_root_y)
{
    if ((!preview.pending && !preview.visible) ||
        preview.client_window != client_window) {
        return;
    }
    preview.anchor_x = button_root_x;
    preview.anchor_width = button_width;
    preview.panel_y = panel_root_y;
    if (preview.visible) {
        position_window();
    }
}

void task_preview_cancel(void)
{
    if (preview.visible && preview.window != None) {
        XUnmapWindow(wm.display, preview.window);
    }
    preview.pending = false;
    preview.visible = false;
    preview.client_window = None;
    preview.deadline_ms = 0;
}

void task_preview_tick(void)
{
    if (preview.pending &&
        monotonic_milliseconds() >= preview.deadline_ms) {
        show_preview();
    }
}

int task_preview_next_timeout_ms(void)
{
    int64_t remaining;

    if (!preview.pending) {
        return -1;
    }
    remaining = preview.deadline_ms - monotonic_milliseconds();
    if (remaining <= 0) {
        return 0;
    }
    return remaining > INT_MAX ? INT_MAX : (int)remaining;
}

void task_preview_before_minimize(Client *client)
{
    Pixmap pixmap = None;
    unsigned int width = 0U;
    unsigned int height = 0U;

    if (client == NULL) {
        return;
    }
    if (preview.client_window == client->window) {
        task_preview_cancel();
    }
    unredirect_client(client, false);
    if (!preview.available || !wm.config.taskbar_preview_enabled ||
        !client->mapped || !client->visible) {
        return;
    }
    if (name_client_pixmap(client, &pixmap, &width, &height)) {
        client->preview_pixmap = pixmap;
        client->preview_pixmap_width = width;
        client->preview_pixmap_height = height;
    } else {
        unredirect_client(client, false);
    }
}

void task_preview_client_restored(Client *client)
{
    if (client == NULL) {
        return;
    }
    if (preview.client_window == client->window) {
        task_preview_cancel();
    }
    unredirect_client(client, false);
}

void task_preview_client_invalidated(Client *client)
{
    if (client == NULL) {
        return;
    }
    if (preview.client_window == client->window) {
        task_preview_cancel();
    }
    /* A minimized client is unmapped.  Its named pixmap is the last valid
     * image; geometry and RandR changes must not throw that image away. */
    if (client->minimized && client->preview_pixmap != None) {
        return;
    }
    if (client->preview_pixmap != None || client->preview_redirected) {
        unredirect_client(client, false);
    }
}

void task_preview_client_state_changed(Client *client)
{
    if (client != NULL && preview.client_window == client->window) {
        task_preview_cancel();
    }
}

void task_preview_client_title_changed(Client *client)
{
    if (client != NULL && preview.visible &&
        preview.client_window == client->window) {
        if (!draw_preview(client)) {
            task_preview_cancel();
        }
    }
}

void task_preview_client_forgotten(Client *client, bool destroyed)
{
    if (client == NULL) {
        return;
    }
    if (preview.client_window == client->window) {
        task_preview_cancel();
    }
    unredirect_client(client, destroyed);
}

void task_preview_handle_expose(const XExposeEvent *event)
{
    Client *client;

    if (event == NULL || event->window != preview.window ||
        event->count != 0 || !preview.visible) {
        return;
    }
    client = client_find(preview.client_window);
    if (eligible_client(client)) {
        if (!draw_preview(client)) {
            task_preview_cancel();
        }
    } else {
        task_preview_cancel();
    }
}

void task_preview_raise(void)
{
    if (preview.visible && preview.window != None) {
        XRaiseWindow(wm.display, preview.window);
    }
}
