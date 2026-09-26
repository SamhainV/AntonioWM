#include "wm.h"
#include "config.h"

#include <X11/keysym.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef enum {
    MENU_VIEW_MAIN = 0,
    MENU_VIEW_CATEGORIES,
    MENU_VIEW_APPLICATIONS,
    MENU_VIEW_CONFIRM_REBOOT,
    MENU_VIEW_CONFIRM_POWEROFF
} MenuView;

typedef enum {
    MENU_ITEM_APPLICATIONS = 0,
    MENU_ITEM_TERMINAL,
    MENU_ITEM_FILES,
    MENU_ITEM_LOGOUT,
    MENU_ITEM_SWITCH_USER,
    MENU_ITEM_REBOOT,
    MENU_ITEM_POWEROFF,
    MENU_ITEM_BACK,
    MENU_ITEM_CATEGORY,
    MENU_ITEM_APPLICATION,
    MENU_ITEM_CANCEL,
    MENU_ITEM_CONFIRM,
    MENU_ITEM_INFORMATION,
    MENU_ITEM_SEPARATOR
} MenuItemType;

typedef struct {
    MenuItemType type;
    const char *label;
    const ApplicationEntry *application;
    ApplicationCategory category;
    bool enabled;
} MenuItem;

static MenuView current_view;
static ApplicationCategory current_category;
static IconImage *application_icons;
static size_t application_icon_count;
static size_t selected_index;
static size_t hovered_index;
static size_t pressed_index;
static size_t scroll_first;
static bool open_menu;
static bool has_selection;
static bool has_hover;
static bool has_press;
static bool pointer_grabbed;
static bool keyboard_grabbed;
static int menu_x;
static int menu_y;
static int menu_width;
static int menu_height;

static int minimum_int(int first, int second)
{
    return first < second ? first : second;
}

static int maximum_int(int first, int second)
{
    return first > second ? first : second;
}

static bool executable_available(const char *name)
{
    const char *path;
    const char *start;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    if (strchr(name, '/') != NULL) {
        return access(name, X_OK) == 0;
    }
    path = getenv("PATH");
    if (path == NULL || path[0] == '\0') {
        path = "/usr/local/bin:/usr/bin:/bin";
    }
    start = path;
    for (;;) {
        const char *end = strchr(start, ':');
        size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
        char candidate[AWM_RUNTIME_PATH_LENGTH];
        int written;

        if (length == 0U) {
            written = snprintf(candidate, sizeof(candidate), "./%s", name);
        } else {
            written = snprintf(candidate,
                               sizeof(candidate),
                               "%.*s/%s",
                               (int)length,
                               start,
                               name);
        }
        if (written >= 0 && (size_t)written < sizeof(candidate) &&
            access(candidate, X_OK) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        start = end + 1;
    }
    return false;
}

static const char *switch_user_command(void)
{
    if (wm.config.system_switch_user_command[0] != '\0') {
        return wm.config.system_switch_user_command;
    }
    return executable_available("gdmflexiserver") ? "gdmflexiserver" : NULL;
}

static const char *files_command(void)
{
    size_t index;

    for (index = 0U; index < wm.config.launcher_count; ++index) {
        const LauncherConfig *launcher = &wm.config.launchers[index];

        if (strcasecmp(launcher->label, "Archivos") == 0 ||
            strcasecmp(launcher->label, "Files") == 0 ||
            strstr(launcher->command, "pcmanfm") != NULL) {
            return launcher->command;
        }
    }
    return "pcmanfm";
}

static size_t nonempty_category_count(void)
{
    ApplicationCategory category;
    size_t count = 0U;

    for (category = APPLICATION_CATEGORY_ACCESSORIES;
         category < APPLICATION_CATEGORY_COUNT;
         category = (ApplicationCategory)(category + 1)) {
        if (desktop_entries_category_count(category) > 0U) {
            ++count;
        }
    }
    return count;
}

static size_t item_count(void)
{
    switch (current_view) {
    case MENU_VIEW_MAIN:
        return 8U;
    case MENU_VIEW_CATEGORIES:
        return 1U + nonempty_category_count();
    case MENU_VIEW_APPLICATIONS:
        return 1U + desktop_entries_category_count(current_category);
    case MENU_VIEW_CONFIRM_REBOOT:
    case MENU_VIEW_CONFIRM_POWEROFF:
        return 4U;
    }
    return 0U;
}

static bool category_for_position(size_t position,
                                  ApplicationCategory *result)
{
    ApplicationCategory category;

    for (category = APPLICATION_CATEGORY_ACCESSORIES;
         category < APPLICATION_CATEGORY_COUNT;
         category = (ApplicationCategory)(category + 1)) {
        if (desktop_entries_category_count(category) == 0U) {
            continue;
        }
        if (position == 0U) {
            *result = category;
            return true;
        }
        --position;
    }
    return false;
}

static void item_at(size_t index, MenuItem *item)
{
    static const char *const main_labels[] = {
        "Aplicaciones  >",
        "Terminal",
        "Archivos",
        "",
        "Cerrar sesión",
        "Cambiar de usuario",
        "Reiniciar",
        "Apagar"
    };

    memset(item, 0, sizeof(*item));
    item->enabled = true;
    if (current_view == MENU_VIEW_MAIN) {
        static const MenuItemType main_types[] = {
            MENU_ITEM_APPLICATIONS,
            MENU_ITEM_TERMINAL,
            MENU_ITEM_FILES,
            MENU_ITEM_SEPARATOR,
            MENU_ITEM_LOGOUT,
            MENU_ITEM_SWITCH_USER,
            MENU_ITEM_REBOOT,
            MENU_ITEM_POWEROFF
        };

        if (index >= sizeof(main_types) / sizeof(main_types[0])) {
            item->enabled = false;
            return;
        }
        item->type = main_types[index];
        item->label = main_labels[index];
        if (item->type == MENU_ITEM_TERMINAL) {
            item->enabled = wm.config.terminal[0] != '\0';
        } else if (item->type == MENU_ITEM_SWITCH_USER) {
            item->enabled = switch_user_command() != NULL;
        } else if (item->type == MENU_ITEM_REBOOT) {
            item->enabled = wm.config.system_reboot_command[0] != '\0';
        } else if (item->type == MENU_ITEM_POWEROFF) {
            item->enabled = wm.config.system_poweroff_command[0] != '\0';
        }
        return;
    }
    if (current_view == MENU_VIEW_CATEGORIES) {
        if (index == 0U) {
            item->type = MENU_ITEM_BACK;
            item->label = "<  Volver";
            return;
        }
        if (category_for_position(index - 1U, &item->category)) {
            item->type = MENU_ITEM_CATEGORY;
            item->label = desktop_entries_category_name(item->category);
        } else {
            item->enabled = false;
        }
        return;
    }
    if (current_view == MENU_VIEW_APPLICATIONS) {
        if (index == 0U) {
            item->type = MENU_ITEM_BACK;
            item->label = "<  Categorías";
            return;
        }
        item->application = desktop_entries_category_get(current_category,
                                                          index - 1U);
        if (item->application != NULL) {
            item->type = MENU_ITEM_APPLICATION;
            item->label = item->application->name;
            item->category = current_category;
        } else {
            item->enabled = false;
        }
        return;
    }

    if (index == 0U) {
        item->type = MENU_ITEM_INFORMATION;
        item->label = current_view == MENU_VIEW_CONFIRM_REBOOT
                          ? "¿Reiniciar el equipo?"
                          : "¿Apagar el equipo?";
        item->enabled = false;
    } else if (index == 1U) {
        item->type = MENU_ITEM_SEPARATOR;
        item->label = "";
        item->enabled = false;
    } else if (index == 2U) {
        item->type = MENU_ITEM_CANCEL;
        item->label = "Cancelar";
    } else if (index == 3U) {
        item->type = MENU_ITEM_CONFIRM;
        item->label = current_view == MENU_VIEW_CONFIRM_REBOOT
                          ? "Reiniciar ahora"
                          : "Apagar ahora";
    } else {
        item->enabled = false;
    }
}

static bool item_selectable(size_t index)
{
    MenuItem item;

    item_at(index, &item);
    return item.enabled && item.type != MENU_ITEM_SEPARATOR &&
           item.type != MENU_ITEM_INFORMATION;
}

static int item_height(size_t index)
{
    MenuItem item;

    item_at(index, &item);
    return item.type == MENU_ITEM_SEPARATOR ? AWM_MENU_SEPARATOR_HEIGHT
                                            : AWM_MENU_ROW_HEIGHT;
}

static size_t first_selectable(void)
{
    size_t count = item_count();
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (item_selectable(index)) {
            return index;
        }
    }
    return 0U;
}

static size_t last_selectable(void)
{
    size_t index = item_count();

    while (index > 0U) {
        --index;
        if (item_selectable(index)) {
            return index;
        }
    }
    return 0U;
}

static size_t next_selectable(size_t from, int direction)
{
    size_t count = item_count();
    size_t index = from;
    size_t attempts;

    if (count == 0U) {
        return 0U;
    }
    for (attempts = 0U; attempts < count; ++attempts) {
        if (direction > 0) {
            index = index + 1U < count ? index + 1U : 0U;
        } else {
            index = index > 0U ? index - 1U : count - 1U;
        }
        if (item_selectable(index)) {
            return index;
        }
    }
    return from;
}

static int natural_height(void)
{
    size_t count = item_count();
    size_t index;
    int height = 2 * AWM_MENU_BORDER_WIDTH;

    for (index = 0U; index < count; ++index) {
        height += item_height(index);
    }
    return maximum_int(height, 1);
}

static void calculate_geometry(void)
{
    Monitor monitor;
    WorkArea area;
    int panel_x;
    int panel_y;
    int panel_width;
    int panel_height;
    int available_above;
    int wanted_width = current_view == MENU_VIEW_APPLICATIONS
                           ? AWM_MENU_APPLICATION_WIDTH
                           : AWM_MENU_WIDTH;

    monitor_primary(&monitor);
    ewmh_get_monitor_workarea(wm.current_desktop, &monitor, &area);
    ewmh_get_taskbar_geometry(&panel_x,
                              &panel_y,
                              &panel_width,
                              &panel_height);
    (void)panel_width;
    (void)panel_height;
    available_above = panel_y - area.y;
    menu_width = minimum_int(wanted_width, area.width);
    menu_width = maximum_int(menu_width, 1);
    menu_height = natural_height();
    if (available_above > 0 && menu_height > available_above) {
        menu_height = available_above;
    }
    menu_height = minimum_int(menu_height, area.height);
    menu_height = maximum_int(menu_height, 1);
    menu_x = panel_x;
    if (menu_x + menu_width > area.x + area.width) {
        menu_x = area.x + area.width - menu_width;
    }
    if (menu_x < area.x) {
        menu_x = area.x;
    }
    menu_y = panel_y - menu_height;
    if (menu_y < area.y) {
        menu_y = area.y;
    }
}

static bool index_is_visible(size_t wanted)
{
    size_t count = item_count();
    size_t index;
    int y = AWM_MENU_BORDER_WIDTH;
    int limit = menu_height - AWM_MENU_BORDER_WIDTH;

    for (index = scroll_first; index < count; ++index) {
        int height = item_height(index);

        if (y + height > limit) {
            break;
        }
        if (index == wanted) {
            return true;
        }
        y += height;
    }
    return false;
}

static void ensure_selection_visible(void)
{
    size_t count = item_count();

    if (!has_selection || count == 0U) {
        scroll_first = 0U;
        return;
    }
    if (selected_index < scroll_first) {
        scroll_first = selected_index;
    }
    while (scroll_first < selected_index &&
           !index_is_visible(selected_index)) {
        ++scroll_first;
    }
}

static size_t application_index(const ApplicationEntry *application)
{
    size_t index;

    for (index = 0U; index < desktop_entries_count(); ++index) {
        if (desktop_entries_get(index) == application) {
            return index;
        }
    }
    return desktop_entries_count();
}

static void draw_item(size_t index, int y, int height)
{
    MenuItem item;
    unsigned long background = wm.bar_background;
    unsigned long foreground = wm.bar_foreground;
    int text_x = AWM_MENU_BORDER_WIDTH + AWM_BAR_PADDING;
    int content_width = menu_width - text_x - AWM_BAR_PADDING -
                        AWM_MENU_BORDER_WIDTH;

    item_at(index, &item);
    if (item.type == MENU_ITEM_SEPARATOR) {
        draw_line(wm.menu_window,
                  wm.bar_separator,
                  AWM_MENU_BORDER_WIDTH + AWM_BAR_PADDING,
                  y + height / 2,
                  menu_width - AWM_MENU_BORDER_WIDTH - AWM_BAR_PADDING - 1,
                  y + height / 2);
        return;
    }
    if (has_press && pressed_index == index) {
        background = wm.button_pressed;
    } else if ((has_hover && hovered_index == index) ||
               (has_selection && selected_index == index)) {
        background = wm.bar_selected;
    }
    draw_fill(wm.menu_window,
              background,
              AWM_MENU_BORDER_WIDTH,
              y,
              (unsigned int)maximum_int(menu_width -
                                            2 * AWM_MENU_BORDER_WIDTH,
                                        1),
              (unsigned int)height);
    if (!item.enabled) {
        foreground = wm.title_text_inactive;
    }

    if (item.type == MENU_ITEM_APPLICATION && item.application != NULL) {
        size_t icon_index = application_index(item.application);
        int icon_size = minimum_int(height - 6, 18);

        if (icon_index < application_icon_count && icon_size > 0 &&
            application_icons[icon_index].rgba != NULL &&
            icon_draw(wm.menu_window,
                      &application_icons[icon_index],
                      text_x,
                      y + (height - icon_size) / 2,
                      (unsigned int)icon_size,
                      (unsigned int)icon_size,
                      background)) {
            text_x += icon_size + AWM_ELEMENT_GAP;
            content_width -= icon_size + AWM_ELEMENT_GAP;
        }
    }
    if (content_width > 0) {
        draw_text_ellipsized(wm.menu_window,
                             foreground,
                             text_x,
                             y + draw_text_baseline(height),
                             text_x,
                             y,
                             (unsigned int)content_width,
                             (unsigned int)height,
                             item.label != NULL ? item.label : "");
    }
}

static void redraw(void)
{
    size_t count;
    size_t index;
    int y;
    int limit;

    if (!open_menu || wm.menu_window == None) {
        return;
    }
    ensure_selection_visible();
    draw_fill(wm.menu_window,
              wm.bar_background,
              0,
              0,
              (unsigned int)menu_width,
              (unsigned int)menu_height);
    draw_rectangle(wm.menu_window,
                   wm.bar_separator,
                   0,
                   0,
                   (unsigned int)maximum_int(menu_width - 1, 1),
                   (unsigned int)maximum_int(menu_height - 1, 1));

    count = item_count();
    y = AWM_MENU_BORDER_WIDTH;
    limit = menu_height - AWM_MENU_BORDER_WIDTH;
    for (index = scroll_first; index < count; ++index) {
        int height = item_height(index);

        if (y + height > limit) {
            break;
        }
        draw_item(index, y, height);
        y += height;
    }
}

static void select_initial(void)
{
    selected_index = first_selectable();
    has_selection = item_count() > 0U && item_selectable(selected_index);
    has_hover = false;
    has_press = false;
    scroll_first = 0U;
}

static void set_view(MenuView view)
{
    current_view = view;
    select_initial();
    calculate_geometry();
    if (wm.menu_window != None) {
        XMoveResizeWindow(wm.display,
                          wm.menu_window,
                          menu_x,
                          menu_y,
                          (unsigned int)menu_width,
                          (unsigned int)menu_height);
    }
    redraw();
}

static void free_application_icons(void)
{
    size_t index;

    for (index = 0U; index < application_icon_count; ++index) {
        icon_free(&application_icons[index]);
    }
    free(application_icons);
    application_icons = NULL;
    application_icon_count = 0U;
}

static void load_application_icons(void)
{
    size_t count = desktop_entries_count();
    size_t index;

    free_application_icons();
    if (count == 0U) {
        return;
    }
    application_icons = calloc(count, sizeof(*application_icons));
    if (application_icons == NULL) {
        return;
    }
    application_icon_count = count;
    for (index = 0U; index < count; ++index) {
        const ApplicationEntry *application = desktop_entries_get(index);

        if (application != NULL && application->icon_path != NULL &&
            application->icon_path[0] != '\0') {
            (void)icon_load_png(application->icon_path,
                                &application_icons[index]);
        }
    }
}

bool menu_initialize(void)
{
    XSetWindowAttributes attributes;
    unsigned long mask;

    load_application_icons();
    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = wm.bar_background;
    attributes.event_mask = ExposureMask | ButtonPressMask |
                            ButtonReleaseMask | PointerMotionMask;
    attributes.cursor = wm.normal_cursor;
    mask = CWOverrideRedirect | CWBackPixel | CWEventMask;
    if (wm.normal_cursor != None) {
        mask |= CWCursor;
    }
    current_view = MENU_VIEW_MAIN;
    select_initial();
    calculate_geometry();
    wm.menu_window = XCreateWindow(wm.display,
                                   wm.root,
                                   menu_x,
                                   menu_y,
                                   (unsigned int)menu_width,
                                   (unsigned int)menu_height,
                                   0U,
                                   CopyFromParent,
                                   InputOutput,
                                   CopyFromParent,
                                   mask,
                                   &attributes);
    if (wm.menu_window == None) {
        fprintf(stderr, "%s: no se pudo crear el menu principal\n", AWM_NAME);
        free_application_icons();
        return false;
    }
    XStoreName(wm.display, wm.menu_window, "AntonioWM Menu");
    return true;
}

void menu_cleanup(void)
{
    menu_close();
    free_application_icons();
    if (wm.menu_window != None) {
        XDestroyWindow(wm.display, wm.menu_window);
        wm.menu_window = None;
    }
}

void menu_toggle(Time time)
{
    int pointer_result;
    int keyboard_result;

    if (open_menu) {
        menu_close();
        return;
    }
    if (wm.menu_window == None || !wm.config.taskbar_enabled) {
        return;
    }
    set_view(MENU_VIEW_MAIN);
    XMapRaised(wm.display, wm.menu_window);
    XSync(wm.display, False);
    pointer_result = XGrabPointer(wm.display,
                                  wm.root,
                                  False,
                                  ButtonPressMask | ButtonReleaseMask |
                                      PointerMotionMask,
                                  GrabModeAsync,
                                  GrabModeAsync,
                                  None,
                                  wm.normal_cursor,
                                  time);
    if (pointer_result == GrabSuccess) {
        pointer_grabbed = true;
    }
    keyboard_result = XGrabKeyboard(wm.display,
                                    wm.root,
                                    False,
                                    GrabModeAsync,
                                    GrabModeAsync,
                                    time);
    if (keyboard_result == GrabSuccess) {
        keyboard_grabbed = true;
    }
    if (!pointer_grabbed || !keyboard_grabbed) {
        if (pointer_grabbed) {
            XUngrabPointer(wm.display, time);
            pointer_grabbed = false;
        }
        if (keyboard_grabbed) {
            XUngrabKeyboard(wm.display, time);
            keyboard_grabbed = false;
        }
        XUnmapWindow(wm.display, wm.menu_window);
        return;
    }
    open_menu = true;
    redraw();
    taskbar_update();
}

void menu_close(void)
{
    if (pointer_grabbed) {
        XUngrabPointer(wm.display, CurrentTime);
        pointer_grabbed = false;
    }
    if (keyboard_grabbed) {
        XUngrabKeyboard(wm.display, CurrentTime);
        keyboard_grabbed = false;
    }
    if (!open_menu) {
        return;
    }
    open_menu = false;
    has_hover = false;
    has_press = false;
    if (wm.menu_window != None) {
        XUnmapWindow(wm.display, wm.menu_window);
    }
    taskbar_update();
    stacking_enforce();
}

void menu_reconfigure(void)
{
    if (!open_menu || wm.menu_window == None) {
        return;
    }
    calculate_geometry();
    XMoveResizeWindow(wm.display,
                      wm.menu_window,
                      menu_x,
                      menu_y,
                      (unsigned int)menu_width,
                      (unsigned int)menu_height);
    redraw();
}

void menu_raise(void)
{
    if (open_menu && wm.menu_window != None) {
        XRaiseWindow(wm.display, wm.menu_window);
    }
}

bool menu_is_open(void)
{
    return open_menu;
}

static bool point_inside(int root_x, int root_y)
{
    return root_x >= menu_x && root_x < menu_x + menu_width &&
           root_y >= menu_y && root_y < menu_y + menu_height;
}

static bool index_at_point(int root_x, int root_y, size_t *result)
{
    size_t count = item_count();
    size_t index;
    int relative_y = root_y - menu_y;
    int y = AWM_MENU_BORDER_WIDTH;
    int limit = menu_height - AWM_MENU_BORDER_WIDTH;

    if (!point_inside(root_x, root_y)) {
        return false;
    }
    for (index = scroll_first; index < count; ++index) {
        int height = item_height(index);

        if (y + height > limit) {
            break;
        }
        if (relative_y >= y && relative_y < y + height) {
            *result = index;
            return true;
        }
        y += height;
    }
    return false;
}

static void activate_item(size_t index)
{
    MenuItem item;
    char command[AWM_DESKTOP_ENTRY_COMMAND_LENGTH];

    item_at(index, &item);
    if (!item.enabled || item.type == MENU_ITEM_SEPARATOR ||
        item.type == MENU_ITEM_INFORMATION) {
        return;
    }
    switch (item.type) {
    case MENU_ITEM_APPLICATIONS:
        set_view(MENU_VIEW_CATEGORIES);
        break;
    case MENU_ITEM_TERMINAL:
        menu_close();
        command_spawn(wm.config.terminal);
        break;
    case MENU_ITEM_FILES:
        menu_close();
        command_spawn(files_command());
        break;
    case MENU_ITEM_LOGOUT:
        menu_close();
        if (wm.config.system_logout_command[0] != '\0') {
            command_spawn(wm.config.system_logout_command);
        } else {
            wm.running = false;
        }
        break;
    case MENU_ITEM_SWITCH_USER:
        menu_close();
        command_spawn(switch_user_command());
        break;
    case MENU_ITEM_REBOOT:
        set_view(MENU_VIEW_CONFIRM_REBOOT);
        break;
    case MENU_ITEM_POWEROFF:
        set_view(MENU_VIEW_CONFIRM_POWEROFF);
        break;
    case MENU_ITEM_BACK:
        set_view(current_view == MENU_VIEW_APPLICATIONS
                     ? MENU_VIEW_CATEGORIES
                     : MENU_VIEW_MAIN);
        break;
    case MENU_ITEM_CATEGORY:
        current_category = item.category;
        set_view(MENU_VIEW_APPLICATIONS);
        break;
    case MENU_ITEM_APPLICATION:
        if (desktop_entry_launch_command(item.application,
                                         wm.config.terminal,
                                         command,
                                         sizeof(command))) {
            menu_close();
            command_spawn(command);
        }
        break;
    case MENU_ITEM_CANCEL:
        set_view(MENU_VIEW_MAIN);
        break;
    case MENU_ITEM_CONFIRM:
        if (current_view == MENU_VIEW_CONFIRM_REBOOT) {
            (void)snprintf(command,
                           sizeof(command),
                           "%s",
                           wm.config.system_reboot_command);
        } else {
            (void)snprintf(command,
                           sizeof(command),
                           "%s",
                           wm.config.system_poweroff_command);
        }
        menu_close();
        command_spawn(command);
        break;
    case MENU_ITEM_INFORMATION:
    case MENU_ITEM_SEPARATOR:
        break;
    }
}

static void scroll_by(int direction)
{
    size_t count = item_count();

    if (direction < 0) {
        if (scroll_first > 0U) {
            --scroll_first;
        }
    } else if (scroll_first + 1U < count) {
        ++scroll_first;
    }
    has_hover = false;
    redraw();
}

bool menu_handle_button_press(const XButtonEvent *event)
{
    size_t index;

    if (!open_menu || event == NULL) {
        return false;
    }
    wm.last_event_time = event->time;
    if (!point_inside(event->x_root, event->y_root)) {
        menu_close();
        return true;
    }
    if (event->button == Button4) {
        scroll_by(-1);
        return true;
    }
    if (event->button == Button5) {
        scroll_by(1);
        return true;
    }
    if (event->button == Button1 &&
        index_at_point(event->x_root, event->y_root, &index) &&
        item_selectable(index)) {
        pressed_index = index;
        selected_index = index;
        has_press = true;
        has_selection = true;
        redraw();
    }
    return true;
}

bool menu_handle_button_release(const XButtonEvent *event)
{
    size_t index;
    bool activate = false;
    size_t pressed = pressed_index;

    if (!open_menu || event == NULL) {
        return false;
    }
    wm.last_event_time = event->time;
    if (event->button == Button1 && has_press &&
        index_at_point(event->x_root, event->y_root, &index) &&
        index == pressed && item_selectable(index)) {
        activate = true;
    }
    has_press = false;
    redraw();
    if (activate) {
        activate_item(pressed);
    }
    return true;
}

bool menu_handle_motion(const XMotionEvent *event)
{
    size_t index;
    bool inside;
    bool changed;

    if (!open_menu || event == NULL) {
        return false;
    }
    inside = index_at_point(event->x_root, event->y_root, &index) &&
             item_selectable(index);
    changed = inside != has_hover ||
              (inside && (!has_hover || hovered_index != index));
    has_hover = inside;
    if (inside) {
        hovered_index = index;
        selected_index = index;
        has_selection = true;
    }
    if (changed) {
        redraw();
    }
    return true;
}

bool menu_handle_keypress(XKeyEvent *event)
{
    KeySym symbol;

    if (!open_menu || event == NULL) {
        return false;
    }
    wm.last_event_time = event->time;
    symbol = XLookupKeysym(event, 0);
    if (symbol == XK_Escape) {
        menu_close();
    } else if (symbol == XK_Up || symbol == XK_KP_Up) {
        selected_index = has_selection
                             ? next_selectable(selected_index, -1)
                             : last_selectable();
        has_selection = true;
        has_hover = false;
        redraw();
    } else if (symbol == XK_Down || symbol == XK_KP_Down) {
        selected_index = has_selection
                             ? next_selectable(selected_index, 1)
                             : first_selectable();
        has_selection = true;
        has_hover = false;
        redraw();
    } else if (symbol == XK_Home || symbol == XK_KP_Home) {
        selected_index = first_selectable();
        has_selection = true;
        has_hover = false;
        redraw();
    } else if (symbol == XK_End || symbol == XK_KP_End) {
        selected_index = last_selectable();
        has_selection = true;
        has_hover = false;
        redraw();
    } else if (symbol == XK_Left || symbol == XK_KP_Left) {
        if (current_view == MENU_VIEW_APPLICATIONS) {
            set_view(MENU_VIEW_CATEGORIES);
        } else if (current_view != MENU_VIEW_MAIN) {
            set_view(MENU_VIEW_MAIN);
        }
    } else if (symbol == XK_Right || symbol == XK_KP_Right) {
        if (has_selection) {
            MenuItem item;

            item_at(selected_index, &item);
            if (item.type == MENU_ITEM_APPLICATIONS ||
                item.type == MENU_ITEM_CATEGORY) {
                activate_item(selected_index);
            }
        }
    } else if (symbol == XK_Return || symbol == XK_KP_Enter ||
               symbol == XK_space) {
        if (has_selection) {
            activate_item(selected_index);
        }
    }
    return true;
}

void menu_handle_expose(const XExposeEvent *event)
{
    if (event != NULL && event->window == wm.menu_window &&
        event->count == 0) {
        redraw();
    }
}
