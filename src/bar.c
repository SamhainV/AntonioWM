#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    Client *client;
    int x;
    int width;
} BarSlot;

static BarSlot *slots;
static size_t slot_count;
static size_t slot_capacity;
static char clock_text[64];
static time_t clock_minute = (time_t)-1;

static int minimum_int(int first, int second)
{
    return first < second ? first : second;
}

static int maximum_int(int first, int second)
{
    return first > second ? first : second;
}

static void update_clock_text(void)
{
    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) == NULL ||
        strftime(clock_text,
                 sizeof(clock_text),
                 AWM_CLOCK_FORMAT,
                 &local_time) == 0U) {
        (void)snprintf(clock_text, sizeof(clock_text), "--:--");
    }
    clock_minute = now / 60;
}

static void draw_label(int x,
                       int width,
                       unsigned long background,
                       unsigned long foreground,
                       const char *text,
                       bool centered)
{
    int text_x = x + AWM_BAR_PADDING;
    int content_width = width - 2 * AWM_BAR_PADDING;
    int measured;

    if (width <= 0) {
        return;
    }

    draw_fill(wm.bar_window,
              background,
              x,
              0,
              (unsigned int)width,
              AWM_BAR_HEIGHT);
    if (content_width <= 0) {
        return;
    }
    if (centered) {
        measured = draw_text_width(text);
        if (measured < content_width) {
            text_x = x + (width - measured) / 2;
        }
    }
    draw_text_ellipsized(wm.bar_window,
                         foreground,
                         text_x,
                         draw_text_baseline((int)AWM_BAR_HEIGHT),
                         x + AWM_BAR_PADDING,
                         0,
                         (unsigned int)content_width,
                         AWM_BAR_HEIGHT,
                         text);
}

static size_t count_desktop_clients(void)
{
    Client *client;
    size_t count = 0U;

    for (client = wm.clients; client != NULL; client = client->next) {
        if (desktop_is_current(client) && client->mapped &&
            client_is_taskbar_visible(client)) {
            ++count;
        }
    }
    return count;
}

static bool reserve_slots(size_t required)
{
    BarSlot *new_slots;

    if (required <= slot_capacity) {
        return true;
    }

    new_slots = realloc(slots, required * sizeof(*slots));
    if (new_slots == NULL) {
        return false;
    }
    slots = new_slots;
    slot_capacity = required;
    return true;
}

static void layout_and_draw_slots(int start, int end)
{
    Client *client;
    size_t count = count_desktop_clients();
    size_t index = 0U;
    int available = end - start;
    int each;
    int next_x = start;
    int total_gaps;

    slot_count = 0U;
    if (count == 0U || available <= 0 || !reserve_slots(count)) {
        return;
    }

    total_gaps = count > 1U ? ((int)count - 1) * AWM_ELEMENT_GAP : 0;
    each = maximum_int((available - total_gaps) / (int)count, 1);
    each = minimum_int(each, AWM_BAR_WINDOW_MAX_WIDTH);
    for (client = wm.clients; client != NULL; client = client->next) {
        char label[512];
        int x;
        int width;
        unsigned long background;

        if (!desktop_is_current(client) || !client->mapped ||
            !client_is_taskbar_visible(client)) {
            continue;
        }

        x = next_x;
        width = minimum_int(each, end - x);
        if (x >= end || width <= 0) {
            break;
        }

        slots[index].client = client;
        slots[index].x = x;
        slots[index].width = width;

        if (client->demands_attention || client->urgent) {
            (void)snprintf(label,
                           sizeof(label),
                           "[!] %s",
                           client->title != NULL ? client->title : "(sin titulo)");
            background = wm.bar_urgent;
        } else if (client->minimized) {
            (void)snprintf(label,
                           sizeof(label),
                           "[m] %s",
                           client->title != NULL ? client->title : "(sin titulo)");
            background = wm.bar_minimized;
        } else if (client == wm.focused) {
            (void)snprintf(label,
                           sizeof(label),
                           "%s",
                           client->title != NULL ? client->title : "(sin titulo)");
            background = wm.bar_active_window;
        } else {
            (void)snprintf(label,
                           sizeof(label),
                           "%s",
                           client->title != NULL ? client->title : "(sin titulo)");
            background = wm.bar_item;
        }

        draw_label(x,
                   width,
                   background,
                   wm.bar_foreground,
                   label,
                   false);
        next_x = x + width + AWM_ELEMENT_GAP;
        ++index;
    }
    slot_count = index;
}

bool bar_initialize(void)
{
    XSetWindowAttributes attributes;
    int bar_x;
    int bar_y;
    int bar_width;
    int bar_height;

    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = wm.bar_background;
    attributes.event_mask = ExposureMask | ButtonPressMask;

    ewmh_get_bar_geometry(&bar_x, &bar_y, &bar_width, &bar_height);
    wm.bar_window = XCreateWindow(wm.display,
                                  wm.root,
                                  bar_x,
                                  bar_y,
                                  (unsigned int)bar_width,
                                  (unsigned int)bar_height,
                                  0U,
                                  CopyFromParent,
                                  InputOutput,
                                  CopyFromParent,
                                  CWOverrideRedirect | CWBackPixel | CWEventMask,
                                  &attributes);
    if (wm.bar_window == None) {
        fprintf(stderr, "%s: no se pudo crear la barra superior\n", AWM_NAME);
        return false;
    }

    XStoreName(wm.display, wm.bar_window, "AntonioWM Bar");
    XMapRaised(wm.display, wm.bar_window);
    update_clock_text();
    bar_update();
    return true;
}

void bar_cleanup(void)
{
    free(slots);
    slots = NULL;
    slot_count = 0U;
    slot_capacity = 0U;

    if (wm.bar_window != None) {
        XDestroyWindow(wm.display, wm.bar_window);
        wm.bar_window = None;
    }
}

void bar_update(void)
{
    unsigned int desktop;
    int brand_width;
    int desktop_start;
    int desktop_width = AWM_BAR_DESKTOP_WIDTH;
    int list_start;
    int clock_width;
    int clock_x;
    int active_width;
    int active_x;
    char desktop_fallback[16];
    char active_label[512];
    unsigned long active_background;
    int bar_x;
    int bar_y;
    int bar_width;
    int bar_height;

    ewmh_get_bar_geometry(&bar_x, &bar_y, &bar_width, &bar_height);
    (void)bar_x;
    (void)bar_y;
    if (wm.bar_window == None || bar_width <= 0) {
        return;
    }
    if (clock_minute == (time_t)-1) {
        update_clock_text();
    }

    draw_fill(wm.bar_window,
              wm.bar_background,
              0,
              0,
              (unsigned int)bar_width,
              (unsigned int)bar_height);
    brand_width = minimum_int(AWM_BAR_BRAND_WIDTH, bar_width);
    draw_label(0,
               brand_width,
               wm.bar_background,
               wm.bar_foreground,
               AWM_NAME,
               false);

    desktop_start = brand_width + AWM_ELEMENT_GAP;

    for (desktop = 0U; desktop < wm.desktop_count; ++desktop) {
        int x = desktop_start +
                (int)desktop * (desktop_width + AWM_ELEMENT_GAP);

        if (x >= bar_width) {
            break;
        }
        const char *desktop_label;

        if (desktop < AWM_RUNTIME_MAX_DESKTOPS &&
            wm.config.desktop_names[desktop][0] != '\0') {
            desktop_label = wm.config.desktop_names[desktop];
        } else {
            (void)snprintf(desktop_fallback,
                           sizeof(desktop_fallback),
                           "%u",
                           desktop + 1U);
            desktop_label = desktop_fallback;
        }
        draw_label(x,
                   minimum_int(desktop_width, bar_width - x),
                   desktop == wm.current_desktop ? wm.bar_selected
                                                 : wm.bar_item,
                   wm.bar_foreground,
                   desktop_label,
                   true);
    }

    list_start = desktop_start +
                 (int)wm.desktop_count * (desktop_width + AWM_ELEMENT_GAP);
    clock_width = minimum_int(
        maximum_int(AWM_BAR_CLOCK_WIDTH,
                    draw_text_width(clock_text) + 2 * AWM_BAR_PADDING),
        bar_width);
    clock_x = bar_width - clock_width;
    active_width = minimum_int(AWM_BAR_ACTIVE_TITLE_WIDTH,
                               maximum_int(clock_x - list_start, 0));
    active_x = clock_x - active_width;

    layout_and_draw_slots(list_start,
                          maximum_int(active_x - AWM_ELEMENT_GAP, list_start));

    if (wm.focused != NULL && wm.focused->visible &&
        desktop_is_current(wm.focused)) {
        (void)snprintf(active_label,
                       sizeof(active_label),
                       "> %s",
                       wm.focused->title != NULL ? wm.focused->title : "(sin titulo)");
        active_background = wm.bar_active_window;
    } else {
        (void)snprintf(active_label, sizeof(active_label), "Sin ventana activa");
        active_background = wm.bar_item;
    }
    draw_label(active_x,
               active_width,
               active_background,
               wm.bar_foreground,
               active_label,
               false);
    draw_label(clock_x,
               clock_width,
               wm.bar_item,
               wm.bar_foreground,
               clock_text,
               true);
    if (bar_height > 0) {
        draw_line(wm.bar_window,
                  wm.bar_separator,
                  0,
                  bar_height - 1,
                  bar_width - 1,
                  bar_height - 1);
    }
    taskbar_update();
}

void bar_reconfigure(void)
{
    int x;
    int y;
    int width;
    int height;

    if (wm.bar_window == None) {
        return;
    }
    ewmh_get_bar_geometry(&x, &y, &width, &height);
    XMoveResizeWindow(wm.display,
                      wm.bar_window,
                      x,
                      y,
                      (unsigned int)width,
                      (unsigned int)height);
    bar_update();
}

void bar_raise(void)
{
    if (wm.bar_window != None) {
        XRaiseWindow(wm.display, wm.bar_window);
    }
}

bool bar_handle_button_press(const XButtonEvent *event)
{
    unsigned int desktop;
    size_t index;
    int desktop_start = AWM_BAR_BRAND_WIDTH + AWM_ELEMENT_GAP;

    if (event == NULL || event->window != wm.bar_window || event->button != Button1) {
        return false;
    }
    wm.last_event_time = event->time;

    for (desktop = 0U; desktop < wm.desktop_count; ++desktop) {
        int x = desktop_start +
                (int)desktop * (AWM_BAR_DESKTOP_WIDTH + AWM_ELEMENT_GAP);

        if (event->x >= x && event->x < x + AWM_BAR_DESKTOP_WIDTH) {
            desktop_switch(desktop);
            return true;
        }
    }

    for (index = 0U; index < slot_count; ++index) {
        if (event->x >= slots[index].x &&
            event->x < slots[index].x + slots[index].width) {
            Client *client = slots[index].client;

            if (client->minimized) {
                client_restore(client);
            } else {
                client_focus(client, true);
            }
            return true;
        }
    }
    return true;
}

void bar_handle_expose(const XExposeEvent *event)
{
    if (event != NULL && event->window == wm.bar_window && event->count == 0) {
        bar_update();
    }
}

int bar_next_timeout_ms(void)
{
    time_t now = time(NULL);
    int seconds = 60 - (int)(now % 60);

    if (seconds < 1 || seconds > 60) {
        seconds = 60;
    }
    return seconds * 1000 + 50;
}

void bar_tick(void)
{
    time_t now = time(NULL);

    if (now / 60 != clock_minute) {
        update_clock_text();
        bar_update();
    }
}
