#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TASKBAR_SLOT_MENU = 0,
    TASKBAR_SLOT_DESKTOP,
    TASKBAR_SLOT_LAUNCHER,
    TASKBAR_SLOT_CLIENT
} TaskbarSlotKind;

typedef struct {
    TaskbarSlotKind kind;
    unsigned int desktop_index;
    size_t launcher_index;
    Window client_window;
    int x;
    int width;
} TaskbarSlot;

static TaskbarSlot *slots;
static size_t slot_count;
static size_t slot_capacity;
static IconImage launcher_icons[AWM_RUNTIME_MAX_LAUNCHERS];
static TaskbarSlot hovered_slot;
static TaskbarSlot pressed_slot;
static bool has_hovered_slot;
static bool has_pressed_slot;

static void taskbar_preview_slot(const TaskbarSlot *slot, bool schedule)
{
    int panel_x;
    int panel_y;
    int panel_width;
    int panel_height;

    if (slot == NULL || slot->kind != TASKBAR_SLOT_CLIENT) {
        if (schedule) {
            task_preview_cancel();
        }
        return;
    }
    ewmh_get_taskbar_geometry(&panel_x,
                              &panel_y,
                              &panel_width,
                              &panel_height);
    (void)panel_width;
    (void)panel_height;
    if (schedule) {
        task_preview_schedule(slot->client_window,
                              panel_x + slot->x,
                              slot->width,
                              panel_y);
    } else {
        task_preview_reanchor(slot->client_window,
                              panel_x + slot->x,
                              slot->width,
                              panel_y);
    }
}

static int minimum_int(int first, int second)
{
    return first < second ? first : second;
}

static int maximum_int(int first, int second)
{
    return first > second ? first : second;
}

static bool taskbar_enabled(void)
{
    return wm.config.taskbar_enabled && wm.config.taskbar_height > 0U;
}

static size_t launcher_count(void)
{
    return wm.config.launcher_count;
}

static int taskbar_padding(int height)
{
    int maximum = maximum_int((height - 2) / 2, 0);

    return minimum_int(wm.config.taskbar_padding, maximum);
}

static size_t desktop_client_count(void)
{
    const Client *client;
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
    TaskbarSlot *new_slots;

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

static bool same_identity(const TaskbarSlot *first, const TaskbarSlot *second)
{
    if (first->kind != second->kind) {
        return false;
    }
    if (first->kind == TASKBAR_SLOT_MENU) {
        return true;
    }
    if (first->kind == TASKBAR_SLOT_DESKTOP) {
        return first->desktop_index == second->desktop_index;
    }
    if (first->kind == TASKBAR_SLOT_LAUNCHER) {
        return first->launcher_index == second->launcher_index;
    }
    return first->client_window == second->client_window;
}

static const TaskbarSlot *slot_by_identity(const TaskbarSlot *identity)
{
    size_t index;

    for (index = 0U; index < slot_count; ++index) {
        if (same_identity(identity, &slots[index])) {
            return &slots[index];
        }
    }
    return NULL;
}

static TaskbarSlot *slot_at(int x)
{
    size_t index;

    for (index = 0U; index < slot_count; ++index) {
        if (x >= slots[index].x && x < slots[index].x + slots[index].width) {
            return &slots[index];
        }
    }
    return NULL;
}

static unsigned long slot_background(const TaskbarSlot *slot,
                                     unsigned long normal)
{
    if (has_pressed_slot && same_identity(slot, &pressed_slot)) {
        return wm.button_pressed;
    }
    if (has_hovered_slot && same_identity(slot, &hovered_slot)) {
        return wm.button_hover;
    }
    return normal;
}

static unsigned long draw_button_background(const TaskbarSlot *slot,
                                            unsigned long background,
                                            int height)
{
    background = slot_background(slot, background);
    draw_fill(wm.taskbar_window,
              background,
              slot->x,
              1,
              (unsigned int)slot->width,
              (unsigned int)maximum_int(height - 1, 1));
    draw_rectangle(wm.taskbar_window,
                   wm.bar_separator,
                   slot->x,
                   1,
                   (unsigned int)maximum_int(slot->width - 1, 1),
                   (unsigned int)maximum_int(height - 2, 1));
    return background;
}

static void draw_button(const TaskbarSlot *slot,
                        unsigned long background,
                        const char *label,
                        bool centered,
                        int height)
{
    int padding;
    int content_width;
    int text_x;
    int measured;

    if (slot->width <= 0 || height <= 0) {
        return;
    }
    (void)draw_button_background(slot, background, height);
    padding = taskbar_padding(height);
    content_width = slot->width - 2 * padding;
    if (content_width <= 0) {
        return;
    }
    text_x = slot->x + padding;
    if (centered) {
        measured = draw_text_width(label);
        if (measured < content_width) {
            text_x = slot->x + (slot->width - measured) / 2;
        }
    }
    draw_text_ellipsized(wm.taskbar_window,
                         wm.bar_foreground,
                         text_x,
                         draw_text_baseline(height),
                         slot->x + padding,
                         1,
                         (unsigned int)content_width,
                         (unsigned int)maximum_int(height - 1, 1),
                         label);
}

static void draw_launcher_button(const TaskbarSlot *slot,
                                 size_t index,
                                 int height)
{
    const LauncherConfig *launcher = &wm.config.launchers[index];
    const IconImage *icon = &launcher_icons[index];
    int padding = taskbar_padding(height);
    int icon_size = minimum_int(slot->width - 2 * padding,
                                height - 2 * padding);
    unsigned long background;

    if (icon->rgba == NULL || icon_size <= 0) {
        draw_button(slot, wm.bar_item, launcher->label, true, height);
        return;
    }
    background = draw_button_background(slot, wm.bar_item, height);
    if (!icon_draw(wm.taskbar_window,
                   icon,
                   slot->x + (slot->width - icon_size) / 2,
                   (height - icon_size) / 2,
                   (unsigned int)icon_size,
                   (unsigned int)icon_size,
                   background)) {
        draw_button(slot, wm.bar_item, launcher->label, true, height);
    }
}

static int append_fixed_slot(TaskbarSlotKind kind,
                             unsigned int desktop_index,
                             size_t launcher_index,
                             int x,
                             int width)
{
    TaskbarSlot *slot = &slots[slot_count++];

    slot->kind = kind;
    slot->desktop_index = desktop_index;
    slot->launcher_index = launcher_index;
    slot->client_window = None;
    slot->x = x;
    slot->width = width;
    return x + width + wm.config.taskbar_gap;
}

static int layout_panel_start(int content_end, int height)
{
    size_t index;
    int x = wm.config.taskbar_padding;
    int available;
    int width;
    TaskbarSlot *slot;

    available = content_end - x;
    if (available > 0) {
        width = minimum_int(AWM_PANEL_MENU_WIDTH, available);
        x = append_fixed_slot(TASKBAR_SLOT_MENU, 0U, 0U, x, width);
        draw_button(&slots[slot_count - 1U],
                    menu_is_open() ? wm.bar_selected : wm.bar_item,
                    AWM_NAME,
                    true,
                    height);
    }

    for (index = 0U; index < wm.desktop_count; ++index) {
        available = content_end - x;
        if (available <= 0) {
            break;
        }
        width = minimum_int(AWM_PANEL_DESKTOP_WIDTH, available);
        x = append_fixed_slot(TASKBAR_SLOT_DESKTOP,
                              (unsigned int)index,
                              0U,
                              x,
                              width);
        draw_button(&slots[slot_count - 1U],
                    index == wm.current_desktop ? wm.bar_selected : wm.bar_item,
                    wm.config.desktop_names[index],
                    true,
                    height);
    }

    for (index = 0U; index < launcher_count(); ++index) {
        available = content_end - x;

        if (available <= 0) {
            break;
        }
        width = launcher_icons[index].rgba != NULL
                    ? minimum_int(height, available)
                    : minimum_int(AWM_TASKBAR_LAUNCHER_WIDTH, available);
        x = append_fixed_slot(TASKBAR_SLOT_LAUNCHER, 0U, index, x, width);
        slot = &slots[slot_count - 1U];
        draw_launcher_button(slot, index, height);
    }
    return x;
}

static void layout_clients(int start, int content_end, int height)
{
    Client *client;
    size_t count = desktop_client_count();
    int end = content_end;
    int available = end - start;
    int total_gaps;
    int each;
    int x = start;

    if (count == 0U || available <= 0) {
        return;
    }
    total_gaps = count > 1U ? ((int)count - 1) * wm.config.taskbar_gap : 0;
    each = maximum_int((available - total_gaps) / (int)count, 1);
    each = minimum_int(each, AWM_TASKBAR_WINDOW_MAX_WIDTH);

    for (client = wm.clients; client != NULL; client = client->next) {
        TaskbarSlot *slot;
        char label[512];
        unsigned long background;
        int width;

        if (!desktop_is_current(client) || !client->mapped ||
            !client_is_taskbar_visible(client)) {
            continue;
        }
        width = minimum_int(each, end - x);
        if (x >= end || width <= 0) {
            break;
        }

        slot = &slots[slot_count++];
        slot->kind = TASKBAR_SLOT_CLIENT;
        slot->desktop_index = 0U;
        slot->launcher_index = 0U;
        slot->client_window = client->window;
        slot->x = x;
        slot->width = width;

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
        {
            int padding = taskbar_padding(height);
            int size = minimum_int(height - 2 * padding, 32);
            int text_x = slot->x + padding;
            int content = slot->width - 2 * padding;
            background = draw_button_background(slot, background, height);
            if (size > 0 && content > size + padding) {
                if (!client->taskbar_icon_loaded) {
                    (void)icon_load_client(client->window, (unsigned int)size,
                                           &client->taskbar_icon);
                    client->taskbar_icon_loaded = true;
                }
                unsigned int icon_width = (unsigned int)size;
                unsigned int icon_height = (unsigned int)size;
                if (client->taskbar_icon.rgba != NULL) {
                    unsigned int w = client->taskbar_icon.width;
                    unsigned int h = client->taskbar_icon.height;
                    if (w > h) icon_height = (unsigned int)size * h / w;
                    else icon_width = (unsigned int)size * w / h;
                    if (icon_width == 0U) icon_width = 1U;
                    if (icon_height == 0U) icon_height = 1U;
                }
                if (!icon_draw(wm.taskbar_window, &client->taskbar_icon,
                               text_x + (size - (int)icon_width) / 2,
                               (height - (int)icon_height) / 2,
                               icon_width, icon_height, background)) {
                    /* Generic window glyph also works on non-TrueColor visuals. */
                    draw_rectangle(wm.taskbar_window, wm.bar_foreground,
                                   text_x, (height - size) / 2,
                                   (unsigned int)maximum_int(size - 1, 1),
                                   (unsigned int)maximum_int(size - 1, 1));
                    draw_line(wm.taskbar_window, wm.bar_foreground, text_x,
                              (height - size) / 2 + size / 3,
                              text_x + size - 1, (height - size) / 2 + size / 3);
                }
                text_x += size + padding;
                content -= size + padding;
            }
            if (content > 0) {
                draw_text_ellipsized(wm.taskbar_window, wm.bar_foreground,
                                     text_x, draw_text_baseline(height), text_x, 1,
                                     (unsigned int)content,
                                     (unsigned int)maximum_int(height - 1, 1), label);
            }
        }
        x += width + wm.config.taskbar_gap;
    }
}

static void draw_clock(int bar_width, int height)
{
    const char *text = clock_text();
    int right = bar_width - wm.config.taskbar_padding;
    int left = right - AWM_PANEL_CLOCK_WIDTH;
    int measured = draw_text_width(text);
    int text_x = right - measured;

    if (left < 0) {
        left = 0;
    }
    if (text_x < left) {
        text_x = left;
    }
    draw_text_ellipsized(wm.taskbar_window,
                         wm.bar_foreground,
                         text_x,
                         draw_text_baseline(height),
                         left,
                         1,
                         (unsigned int)maximum_int(right - left, 1),
                         (unsigned int)maximum_int(height - 1, 1),
                         text);
}

static void free_launcher_icons(void)
{
    size_t index;

    for (index = 0U; index < AWM_RUNTIME_MAX_LAUNCHERS; ++index) {
        icon_free(&launcher_icons[index]);
    }
}

static void load_launcher_icons(void)
{
    size_t index;

    free_launcher_icons();
    for (index = 0U; index < launcher_count(); ++index) {
        const char *path = wm.config.launchers[index].icon;

        if (path[0] != '\0' && !icon_load_png(path, &launcher_icons[index])) {
            fprintf(stderr,
                    "%s: no se pudo cargar el icono PNG '%s'; se usara la etiqueta '%s'\n",
                    AWM_NAME,
                    path,
                    wm.config.launchers[index].label);
        }
    }
}

bool taskbar_initialize(void)
{
    XSetWindowAttributes attributes;
    unsigned long attribute_mask;
    int x;
    int y;
    int width;
    int height;

    if (!taskbar_enabled()) {
        return true;
    }
    load_launcher_icons();

    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = wm.bar_background;
    attributes.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask | EnterWindowMask | LeaveWindowMask;
    attributes.cursor = wm.normal_cursor;
    attribute_mask = CWOverrideRedirect | CWBackPixel | CWEventMask;
    if (wm.normal_cursor != None) {
        attribute_mask |= CWCursor;
    }

    ewmh_get_taskbar_geometry(&x, &y, &width, &height);
    wm.taskbar_window = XCreateWindow(wm.display,
                                      wm.root,
                                      x,
                                      y,
                                      (unsigned int)width,
                                      (unsigned int)height,
                                      0U,
                                      CopyFromParent,
                                      InputOutput,
                                      CopyFromParent,
                                      attribute_mask,
                                      &attributes);
    if (wm.taskbar_window == None) {
        fprintf(stderr, "%s: no se pudo crear la barra inferior\n", AWM_NAME);
        free_launcher_icons();
        return false;
    }
    XStoreName(wm.display, wm.taskbar_window, "AntonioWM Panel");
    XMapRaised(wm.display, wm.taskbar_window);
    taskbar_update();
    return true;
}

void taskbar_cleanup(void)
{
    task_preview_cancel();
    free(slots);
    slots = NULL;
    slot_count = 0U;
    slot_capacity = 0U;
    has_hovered_slot = false;
    has_pressed_slot = false;
    free_launcher_icons();

    if (wm.taskbar_window != None) {
        XDestroyWindow(wm.display, wm.taskbar_window);
        wm.taskbar_window = None;
    }
}

void taskbar_update(void)
{
    size_t required;
    int x;
    int y;
    int width;
    int height;
    int client_start;
    int content_end;
    int clock_right;
    int clock_left;
    int tray_right;
    int tray_left;
    int tray_width;

    if (wm.taskbar_window == None) {
        return;
    }
    ewmh_get_taskbar_geometry(&x, &y, &width, &height);
    (void)x;
    (void)y;
    required = 1U + (size_t)wm.desktop_count + launcher_count() +
               desktop_client_count();
    slot_count = 0U;
    if (!reserve_slots(required)) {
        has_hovered_slot = false;
        has_pressed_slot = false;
        return;
    }

    draw_fill(wm.taskbar_window,
              wm.bar_background,
              0,
              0,
              (unsigned int)width,
              (unsigned int)height);
    draw_line(wm.taskbar_window,
              wm.bar_separator,
              0,
              0,
              maximum_int(width - 1, 0),
              0);
    clock_right = width - wm.config.taskbar_padding;
    clock_left = clock_right - AWM_PANEL_CLOCK_WIDTH;
    if (clock_left < 0) {
        clock_left = 0;
    }
    tray_right = clock_left - wm.config.taskbar_gap;
    if (tray_right < 0) {
        tray_right = 0;
    }
    tray_width = tray_preferred_width(height);
    tray_left = tray_right - tray_width;
    if (tray_left < wm.config.taskbar_padding) {
        tray_left = wm.config.taskbar_padding;
    }
    if (tray_left > tray_right) {
        tray_left = tray_right;
    }
    content_end = tray_left - (tray_width > 0 ? wm.config.taskbar_gap : 0);
    if (content_end < 0) {
        content_end = 0;
    }
    client_start = layout_panel_start(content_end, height);
    layout_clients(client_start, content_end, height);
    tray_layout(tray_left, tray_right - tray_left, height);
    draw_clock(width, height);

    if (has_hovered_slot) {
        const TaskbarSlot *current = slot_by_identity(&hovered_slot);

        /* A redraw can change the position of a task without a MotionNotify.
         * The saved hover no longer proves that the pointer is over it. */
        if (current == NULL || current->x != hovered_slot.x ||
            current->width != hovered_slot.width) {
            has_hovered_slot = false;
            task_preview_cancel();
        } else {
            taskbar_preview_slot(current, false);
        }
    }
    if (has_pressed_slot && slot_by_identity(&pressed_slot) == NULL) {
        has_pressed_slot = false;
    }
}

void taskbar_reconfigure(void)
{
    int x;
    int y;
    int width;
    int height;

    task_preview_cancel();
    if (wm.taskbar_window == None) {
        return;
    }
    ewmh_get_taskbar_geometry(&x, &y, &width, &height);
    XMoveResizeWindow(wm.display,
                      wm.taskbar_window,
                      x,
                      y,
                      (unsigned int)width,
                      (unsigned int)height);
    taskbar_update();
}

void taskbar_raise(void)
{
    if (wm.taskbar_window != None) {
        XRaiseWindow(wm.display, wm.taskbar_window);
    }
}

static void activate_slot(const TaskbarSlot *slot)
{
    if (slot->kind == TASKBAR_SLOT_MENU) {
        menu_toggle(wm.last_event_time);
        taskbar_update();
        return;
    }
    if (slot->kind == TASKBAR_SLOT_DESKTOP) {
        if (slot->desktop_index < wm.desktop_count) {
            menu_close();
            desktop_switch(slot->desktop_index);
        }
        return;
    }
    if (slot->kind == TASKBAR_SLOT_LAUNCHER) {
        if (slot->launcher_index < launcher_count()) {
            command_spawn(wm.config.launchers[slot->launcher_index].command);
        }
        return;
    }

    {
        Client *client = client_find(slot->client_window);

        if (client == NULL || !client->mapped || !desktop_is_current(client) ||
            !client_is_taskbar_visible(client)) {
            return;
        }
        if (client->minimized) {
            client_restore(client);
        } else if (client == wm.focused && client->visible) {
            client_minimize(client);
        } else {
            client_focus(client, true);
        }
    }
}

bool taskbar_handle_button_press(const XButtonEvent *event)
{
    TaskbarSlot *slot;

    if (event == NULL || event->window != wm.taskbar_window) {
        return false;
    }
    wm.last_event_time = event->time;
    task_preview_cancel();
    if (event->button != Button1) {
        return true;
    }
    slot = slot_at(event->x);
    has_pressed_slot = slot != NULL;
    if (slot != NULL) {
        pressed_slot = *slot;
    }
    taskbar_update();
    return true;
}

bool taskbar_handle_button_release(const XButtonEvent *event)
{
    TaskbarSlot *slot;
    TaskbarSlot pressed;
    bool activate = false;

    if (event == NULL ||
        (event->window != wm.taskbar_window && !has_pressed_slot)) {
        return false;
    }
    wm.last_event_time = event->time;
    pressed = pressed_slot;
    slot = event->window == wm.taskbar_window ? slot_at(event->x) : NULL;
    if (event->button == Button1 && has_pressed_slot && slot != NULL &&
        same_identity(&pressed, slot)) {
        activate = true;
    }
    has_pressed_slot = false;
    taskbar_update();
    if (activate) {
        activate_slot(&pressed);
    }
    return true;
}

bool taskbar_handle_motion(const XMotionEvent *event)
{
    TaskbarSlot *slot;
    bool changed;

    if (event == NULL || event->window != wm.taskbar_window) {
        return false;
    }
    slot = slot_at(event->x);
    changed = (slot == NULL) != !has_hovered_slot;
    if (slot != NULL &&
        (!has_hovered_slot || !same_identity(slot, &hovered_slot))) {
        changed = true;
        hovered_slot = *slot;
        has_hovered_slot = true;
    } else if (slot == NULL) {
        has_hovered_slot = false;
    }
    if (changed) {
        taskbar_update();
    }
    taskbar_preview_slot(slot, true);
    return true;
}

bool taskbar_handle_enter(const XCrossingEvent *event)
{
    TaskbarSlot *slot;

    if (event == NULL || event->window != wm.taskbar_window) {
        return false;
    }
    slot = slot_at(event->x);
    has_hovered_slot = slot != NULL;
    if (slot != NULL) {
        hovered_slot = *slot;
    }
    taskbar_update();
    taskbar_preview_slot(slot, true);
    return true;
}

bool taskbar_handle_leave(const XCrossingEvent *event)
{
    if (event == NULL || event->window != wm.taskbar_window) {
        return false;
    }
    if (has_hovered_slot) {
        has_hovered_slot = false;
        taskbar_update();
    }
    task_preview_cancel();
    return true;
}

void taskbar_handle_expose(const XExposeEvent *event)
{
    if (event != NULL && event->window == wm.taskbar_window &&
        event->count == 0) {
        taskbar_update();
    }
}
