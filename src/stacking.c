#include "wm.h"

#include <stdlib.h>

static bool is_special_above_type(const Client *client)
{
    return client->window_type == CLIENT_TYPE_DOCK ||
           client->window_type == CLIENT_TYPE_MENU ||
           client->window_type == CLIENT_TYPE_DROPDOWN_MENU ||
           client->window_type == CLIENT_TYPE_POPUP_MENU ||
           client->window_type == CLIENT_TYPE_TOOLTIP ||
           client->window_type == CLIENT_TYPE_NOTIFICATION ||
           client->window_type == CLIENT_TYPE_SPLASH;
}

static int base_layer(const Client *client)
{
    if (client->window_type == CLIENT_TYPE_DESKTOP) {
        return 0;
    }
    if (client->below) {
        return 1;
    }
    if (client->fullscreen) {
        return 5;
    }
    if (client->above || is_special_above_type(client)) {
        return 3;
    }
    return 2;
}

static int client_layer(const Client *client)
{
    Window parent_window;
    int layer;
    unsigned int depth = 0U;

    if (client == NULL) {
        return 2;
    }

    layer = base_layer(client);
    parent_window = client->transient_for;
    while (parent_window != None && depth < 32U) {
        Client *parent = client_find(parent_window);
        int parent_layer;

        if (parent == NULL || parent == client) {
            break;
        }
        parent_layer = base_layer(parent);
        if (parent_layer > layer) {
            layer = parent_layer;
        }
        parent_window = parent->transient_for;
        ++depth;
    }
    return layer;
}

static size_t managed_visible_count(void)
{
    Client *client;
    size_t count = 0U;

    if (wm.taskbar_window != None) {
        ++count;
    }
    if (menu_is_open() && wm.menu_window != None) {
        ++count;
    }

    for (client = wm.clients; client != NULL; client = client->next) {
        if (client->visible && client->frame != None) {
            ++count;
        }
    }
    return count;
}

static Client *client_for_frame(Window frame)
{
    Client *client = client_find_any(frame);

    return client != NULL && client->frame == frame ? client : NULL;
}

static void keep_transients_above_parents(Window *bottom_to_top, size_t count)
{
    bool changed = true;
    unsigned int pass = 0U;

    while (changed && pass < 32U) {
        size_t child_index;

        changed = false;
        for (child_index = 0U; child_index < count; ++child_index) {
            Client *child = client_for_frame(bottom_to_top[child_index]);
            size_t parent_index;

            if (child == NULL || child->transient_for == None) {
                continue;
            }
            for (parent_index = 0U; parent_index < count; ++parent_index) {
                Client *parent = client_for_frame(bottom_to_top[parent_index]);

                if (parent != NULL && parent->window == child->transient_for) {
                    break;
                }
            }
            if (parent_index < count && child_index < parent_index) {
                Window child_frame = bottom_to_top[child_index];
                size_t move;

                for (move = child_index; move < parent_index; ++move) {
                    bottom_to_top[move] = bottom_to_top[move + 1U];
                }
                bottom_to_top[parent_index] = child_frame;
                changed = true;
                break;
            }
        }
        ++pass;
    }
}

void stacking_enforce(void)
{
    size_t wanted = managed_visible_count();
    Window *bottom_to_top;
    Window *top_to_bottom;
    Window root_return;
    Window parent_return;
    Window *children = NULL;
    unsigned int child_count = 0U;
    size_t count = 0U;
    int layer;
    unsigned int index;

    if (wanted == 0U) {
        ewmh_update_client_list();
        return;
    }

    bottom_to_top = calloc(wanted, sizeof(*bottom_to_top));
    top_to_bottom = calloc(wanted, sizeof(*top_to_bottom));
    if (bottom_to_top == NULL || top_to_bottom == NULL) {
        free(bottom_to_top);
        free(top_to_bottom);
        return;
    }

    if (XQueryTree(wm.display,
                   wm.root,
                   &root_return,
                   &parent_return,
                   &children,
                   &child_count) == 0) {
        free(bottom_to_top);
        free(top_to_bottom);
        return;
    }

    for (layer = 0; layer <= 5; ++layer) {
        for (index = 0U; index < child_count; ++index) {
            Client *client = client_for_frame(children[index]);

            if (client != NULL && client->visible &&
                client_layer(client) == layer && count < wanted) {
                bottom_to_top[count++] = client->frame;
            } else if (layer == 4 &&
                       (children[index] == wm.taskbar_window ||
                        (menu_is_open() &&
                         children[index] == wm.menu_window)) &&
                       count < wanted) {
                bottom_to_top[count++] = children[index];
            }
        }
    }

    keep_transients_above_parents(bottom_to_top, count);
    for (index = 0U; index < count; ++index) {
        top_to_bottom[index] = bottom_to_top[count - index - 1U];
    }
    if (count > 1U) {
        XRestackWindows(wm.display, top_to_bottom, (int)count);
    }

    if (children != NULL) {
        XFree(children);
    }
    free(bottom_to_top);
    free(top_to_bottom);
    ewmh_update_client_list();
    task_preview_raise();
}

void stacking_raise(Client *client)
{
    Client *parent;

    if (client == NULL || client->frame == None) {
        return;
    }
    parent = client_find(client->transient_for);
    if (parent != NULL && parent->visible && parent->frame != None) {
        XRaiseWindow(wm.display, parent->frame);
    }
    XRaiseWindow(wm.display, client->frame);
    stacking_enforce();
}

void stacking_lower(Client *client)
{
    if (client == NULL || client->frame == None) {
        return;
    }
    XLowerWindow(wm.display, client->frame);
    stacking_enforce();
}

void stacking_restack_request(Client *client, Window sibling, int detail)
{
    XWindowChanges changes;
    unsigned int mask = CWStackMode;
    Client *sibling_client;

    if (client == NULL || client->frame == None) {
        return;
    }

    changes.x = 0;
    changes.y = 0;
    changes.width = 1;
    changes.height = 1;
    changes.border_width = 0;
    changes.sibling = sibling;
    changes.stack_mode = detail;

    sibling_client = client_find(sibling);
    if (sibling_client == NULL) {
        sibling_client = client_find_any(sibling);
    }
    if (sibling_client != NULL && sibling_client != client) {
        changes.sibling = sibling_client->frame;
        mask |= CWSibling;
    }
    XConfigureWindow(wm.display, client->frame, mask, &changes);
    stacking_enforce();
}
