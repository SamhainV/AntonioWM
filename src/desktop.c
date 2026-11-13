#include "wm.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

static Client *focus_candidate(unsigned int desktop)
{
    Client *client;
    Window remembered;

    if (desktop >= wm.desktop_count) {
        return NULL;
    }

    remembered = wm.desktop_focus[desktop];
    if (remembered != None) {
        client = client_find(remembered);
        if (client != NULL && desktop_is_current(client) && client->mapped &&
            client->visible && !client->minimized &&
            client_is_focus_candidate(client)) {
            return client;
        }
    }

    for (client = wm.clients_tail; client != NULL; client = client->previous) {
        if ((client->desktop == desktop || client->sticky) && client->mapped &&
            client->visible && !client->minimized &&
            client_is_focus_candidate(client)) {
            return client;
        }
    }

    return NULL;
}

bool desktop_initialize(void)
{
    wm.desktop_count = wm.config.desktop_count;
    if (wm.desktop_count == 0U || wm.desktop_count > AWM_RUNTIME_MAX_DESKTOPS) {
        wm.desktop_count = 4U;
    }
    wm.current_desktop = 0U;
    wm.cascade_indices = calloc(wm.desktop_count, sizeof(*wm.cascade_indices));
    wm.desktop_focus = calloc(wm.desktop_count, sizeof(*wm.desktop_focus));

    if (wm.cascade_indices == NULL || wm.desktop_focus == NULL) {
        fprintf(stderr, "%s: no hay memoria para inicializar los escritorios\n", AWM_NAME);
        free(wm.cascade_indices);
        free(wm.desktop_focus);
        wm.cascade_indices = NULL;
        wm.desktop_focus = NULL;
        return false;
    }

    return true;
}

void desktop_cleanup(void)
{
    free(wm.cascade_indices);
    free(wm.desktop_focus);
    wm.cascade_indices = NULL;
    wm.desktop_focus = NULL;
    wm.desktop_count = 0U;
    wm.current_desktop = 0U;
}

bool desktop_is_current(const Client *client)
{
    return client != NULL &&
           (client->sticky || client->desktop == wm.current_desktop);
}

static bool position_is_explicit(const Client *client,
                                 const XWindowAttributes *attributes)
{
    XSizeHints hints;
    long supplied = 0L;
    bool intersects_screen;

    if (client->position_explicit ||
        (XGetWMNormalHints(wm.display, client->window, &hints, &supplied) != 0 &&
         (hints.flags & (USPosition | PPosition)) != 0L)) {
        return true;
    }

    intersects_screen = attributes->x < wm.screen_width &&
                        attributes->y < wm.screen_height &&
                        attributes->x + attributes->width > 0 &&
                        attributes->y + attributes->height > 0;

    return intersects_screen && (attributes->x != 0 || attributes->y != 0);
}

void desktop_place_client(Client *client,
                          const XWindowAttributes *attributes,
                          bool map_requested)
{
    unsigned int index;
    unsigned int slots;
    int outer_width;
    int outer_height;
    int start_y;
    int maximum_x;
    int maximum_y;
    int diagonal_limit;
    int step = AWM_CASCADE_STEP > 0 ? AWM_CASCADE_STEP : 1;
    Monitor monitor;
    WorkArea area;

    if (!map_requested || position_is_explicit(client, attributes) ||
        client->desktop >= wm.desktop_count ||
        (client->window_type != CLIENT_TYPE_NORMAL &&
         client->window_type != CLIENT_TYPE_DIALOG &&
         client->window_type != CLIENT_TYPE_UTILITY &&
         client->window_type != CLIENT_TYPE_TOOLBAR)) {
        return;
    }

    monitor_primary(&monitor);
    ewmh_get_monitor_workarea(client->desktop, &monitor, &area);
    outer_width = decor_outer_width(client);
    outer_height = decor_outer_height(client);
    start_y = area.y + AWM_CASCADE_MARGIN;
    maximum_x = area.x + area.width - outer_width - AWM_CASCADE_MARGIN;
    maximum_y = area.y + area.height - outer_height - AWM_CASCADE_MARGIN;
    diagonal_limit = maximum_x - area.x - AWM_CASCADE_MARGIN;
    if (maximum_y - start_y < diagonal_limit) {
        diagonal_limit = maximum_y - start_y;
    }

    if (diagonal_limit < 0) {
        client->x = area.x;
        client->y = area.y;
    } else {
        slots = (unsigned int)(diagonal_limit / step + 1);
        index = wm.cascade_indices[client->desktop] % slots;
        client->x = area.x + AWM_CASCADE_MARGIN + (int)index * step;
        client->y = start_y + (int)index * step;
        ++wm.cascade_indices[client->desktop];
    }
}

void desktop_remember_focus(const Client *client)
{
    if (client != NULL && client->desktop < wm.desktop_count) {
        wm.desktop_focus[client->desktop] = client->window;
    }
}

void desktop_forget_client(const Client *client)
{
    if (client != NULL && client->desktop < wm.desktop_count &&
        wm.desktop_focus[client->desktop] == client->window) {
        wm.desktop_focus[client->desktop] = None;
    }
}

void desktop_switch(unsigned int desktop)
{
    Client *client;
    Client *candidate;
    unsigned int old_desktop;

    if (desktop >= wm.desktop_count || desktop == wm.current_desktop) {
        return;
    }
    switcher_cancel();
    task_preview_cancel();

    old_desktop = wm.current_desktop;
    for (client = wm.clients; client != NULL; client = client->next) {
        if (!client->sticky && client->desktop == old_desktop && client->visible) {
            client_set_visible(client, false);
        }
    }

    wm.current_desktop = desktop;
    ewmh_recalculate_workarea();

    for (client = wm.clients; client != NULL; client = client->next) {
        if ((client->desktop == desktop || client->sticky) && client->mapped &&
            !client->minimized) {
            client_set_visible(client, true);
        }
    }

    candidate = focus_candidate(desktop);
    client_focus(candidate, true);
    ewmh_update_client_list();
    stacking_enforce();
    taskbar_update();
}

static void move_client_group(Client *client, unsigned int desktop)
{
    Client *child;

    desktop_forget_client(client);
    if (client->visible && !client->sticky) {
        client_set_visible(client, false);
    }
    client->desktop = desktop;
    ewmh_set_client_desktop(client);

    for (child = wm.clients; child != NULL; child = child->next) {
        if (child != client && child->transient_for == client->window &&
            !child->sticky && child->desktop != desktop) {
            move_client_group(child, desktop);
        }
    }
}

void desktop_move_client(Client *client, unsigned int desktop)
{
    bool was_focused;

    if (client == NULL || desktop >= wm.desktop_count || client->desktop == desktop) {
        return;
    }
    task_preview_client_state_changed(client);

    was_focused = wm.focused == client;
    move_client_group(client, desktop);

    if (desktop == wm.current_desktop && client->mapped && !client->minimized) {
        client_set_visible(client, true);
        client_focus(client, true);
    } else if (was_focused ||
               (wm.focused != NULL &&
                (!desktop_is_current(wm.focused) || !wm.focused->visible))) {
        client_focus(focus_candidate(wm.current_desktop), true);
    }

    ewmh_update_client_list();
    ewmh_recalculate_workarea();
    stacking_enforce();
    taskbar_update();
}

void desktop_reapply_screen_states(void)
{
    Client *client;

    for (client = wm.clients; client != NULL; client = client->next) {
        client_reapply_screen_state(client);
    }
}
