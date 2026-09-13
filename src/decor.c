#include "wm.h"
#include "config.h"

#include <X11/X.h>

#include <string.h>

static int button_size(void)
{
    int size = (int)AWM_TITLE_HEIGHT - 6;

    return size >= 8 ? size : 8;
}

static int button_gap(void)
{
    return 3;
}

static void button_positions(const Client *client,
                             int *minimize_x,
                             int *maximize_x,
                             int *close_x)
{
    int size = button_size();
    int gap = button_gap();
    int right = client->width - gap;

    *close_x = right - size;
    *maximize_x = *close_x - gap - size;
    *minimize_x = *maximize_x - gap - size;

    if (*minimize_x < 0) {
        *minimize_x = 0;
    }
    if (*maximize_x < 0) {
        *maximize_x = 0;
    }
    if (*close_x < 0) {
        *close_x = 0;
    }
}

int decor_border_width(const Client *client)
{
    if (client == NULL || client->fullscreen || client->decor == CLIENT_DECOR_NONE) {
        return 0;
    }
    return (int)AWM_BORDER_WIDTH;
}

int decor_title_height(const Client *client)
{
    if (client == NULL || client->fullscreen || client->decor != CLIENT_DECOR_FULL) {
        return 0;
    }
    return (int)AWM_TITLE_HEIGHT;
}

int decor_outer_width(const Client *client)
{
    if (client == NULL) {
        return 0;
    }
    return client->width + 2 * decor_border_width(client);
}

int decor_outer_height(const Client *client)
{
    if (client == NULL) {
        return 0;
    }
    return client->height + decor_title_height(client) +
           2 * decor_border_width(client);
}

int decor_client_root_x(const Client *client)
{
    if (client == NULL) {
        return 0;
    }
    return client->x + decor_border_width(client);
}

int decor_client_root_y(const Client *client)
{
    if (client == NULL) {
        return 0;
    }
    return client->y + decor_border_width(client) + decor_title_height(client);
}

static void create_controls(Client *client)
{
    if (client->decor != CLIENT_DECOR_FULL) {
        return;
    }

    client->titlebar = XCreateSimpleWindow(wm.display,
                                           client->frame,
                                           (int)AWM_BORDER_WIDTH,
                                           (int)AWM_BORDER_WIDTH,
                                           (unsigned int)client->width,
                                           AWM_TITLE_HEIGHT,
                                           0U,
                                           wm.title_inactive,
                                           wm.title_inactive);
    client->button_minimize = XCreateSimpleWindow(wm.display,
                                                  client->titlebar,
                                                  0,
                                                  0,
                                                  1U,
                                                  1U,
                                                  0U,
                                                  wm.button_background,
                                                  wm.button_background);
    client->button_maximize = XCreateSimpleWindow(wm.display,
                                                  client->titlebar,
                                                  0,
                                                  0,
                                                  1U,
                                                  1U,
                                                  0U,
                                                  wm.button_background,
                                                  wm.button_background);
    client->button_close = XCreateSimpleWindow(wm.display,
                                               client->titlebar,
                                               0,
                                               0,
                                               1U,
                                               1U,
                                               0U,
                                               wm.button_background,
                                               wm.button_background);
    XStoreName(wm.display, client->titlebar, "AntonioWM Titlebar");
    XStoreName(wm.display, client->button_minimize, "AntonioWM Minimize");
    XStoreName(wm.display, client->button_maximize, "AntonioWM Maximize");
    XStoreName(wm.display, client->button_close, "AntonioWM Close");
}

static void select_decor_events(Client *client)
{
    const long frame_events = ExposureMask |
                              ButtonPressMask |
                              ButtonReleaseMask |
                              PointerMotionMask |
                              SubstructureRedirectMask |
                              SubstructureNotifyMask;
    const long control_events = ExposureMask |
                                ButtonPressMask |
                                ButtonReleaseMask |
                                PointerMotionMask;

    XSelectInput(wm.display, client->frame, frame_events);
    if (client->titlebar != None) {
        XSelectInput(wm.display, client->titlebar, control_events);
        XSelectInput(wm.display, client->button_minimize, control_events);
        XSelectInput(wm.display, client->button_maximize, control_events);
        XSelectInput(wm.display, client->button_close, control_events);
    }
}

bool decor_create(Client *client, bool was_viewable)
{
    XSetWindowAttributes attributes;
    unsigned long mask;
    unsigned int frame_width;
    unsigned int frame_height;

    if (client == NULL) {
        return false;
    }

    memset(&attributes, 0, sizeof(attributes));
    attributes.override_redirect = True;
    attributes.background_pixel = wm.unfocused_border;
    attributes.event_mask = ExposureMask |
                            ButtonPressMask |
                            ButtonReleaseMask |
                            PointerMotionMask |
                            SubstructureRedirectMask |
                            SubstructureNotifyMask;
    mask = CWOverrideRedirect | CWBackPixel | CWEventMask;
    frame_width = (unsigned int)decor_outer_width(client);
    frame_height = (unsigned int)decor_outer_height(client);

    client->frame = XCreateWindow(wm.display,
                                  wm.root,
                                  client->x,
                                  client->y,
                                  frame_width,
                                  frame_height,
                                  0U,
                                  CopyFromParent,
                                  InputOutput,
                                  CopyFromParent,
                                  mask,
                                  &attributes);
    create_controls(client);

    XStoreName(wm.display, client->frame, "AntonioWM Frame");
    select_decor_events(client);
    if (client->decor != CLIENT_DECOR_NONE) {
        XDefineCursor(wm.display, client->frame, wm.resize_cursor);
    }
    if (client->titlebar != None) {
        XDefineCursor(wm.display, client->titlebar, wm.move_cursor);
    }

    XAddToSaveSet(wm.display, client->window);
    XSetWindowBorderWidth(wm.display, client->window, 0U);
    if (was_viewable) {
        ++client->ignore_unmap;
    }
    XReparentWindow(wm.display,
                    client->window,
                    client->frame,
                    decor_border_width(client),
                    decor_border_width(client) + decor_title_height(client));
    client->reparented = true;

    decor_apply_geometry(client);
    if (client->titlebar != None) {
        XMapWindow(wm.display, client->titlebar);
        XMapWindow(wm.display, client->button_minimize);
        XMapWindow(wm.display, client->button_maximize);
        XMapWindow(wm.display, client->button_close);
    }
    decor_redraw(client);
    return true;
}

void decor_release(Client *client, bool destroyed, bool map_client)
{
    if (client == NULL) {
        return;
    }

    if (client->frame != None) {
        XUnmapWindow(wm.display, client->frame);
    }

    if (!destroyed && client->reparented) {
        XReparentWindow(wm.display,
                        client->window,
                        wm.root,
                        client->x,
                        client->y);
        XMoveResizeWindow(wm.display,
                          client->window,
                          client->x,
                          client->y,
                          (unsigned int)client->width,
                          (unsigned int)client->height);
        XRemoveFromSaveSet(wm.display, client->window);
        XSetWindowBorderWidth(wm.display,
                              client->window,
                              (unsigned int)client->original_border_width);
        if (map_client) {
            XMapWindow(wm.display, client->window);
        }
    }

    if (client->frame != None) {
        XDestroyWindow(wm.display, client->frame);
    }

    client->frame = None;
    client->titlebar = None;
    client->button_minimize = None;
    client->button_maximize = None;
    client->button_close = None;
    client->reparented = false;
}

void decor_apply_geometry(Client *client)
{
    int minimize_x;
    int maximize_x;
    int close_x;
    int size;
    int button_y;

    if (client == NULL || client->frame == None) {
        return;
    }

    if (client->width < 1) {
        client->width = 1;
    }
    if (client->height < 1) {
        client->height = 1;
    }

    if (client->fullscreen || client->decor == CLIENT_DECOR_NONE) {
        if (client->titlebar != None) {
            XUnmapWindow(wm.display, client->titlebar);
        }
        XMoveResizeWindow(wm.display,
                          client->frame,
                          client->x,
                          client->y,
                          (unsigned int)client->width,
                          (unsigned int)client->height);
        XMoveResizeWindow(wm.display,
                          client->window,
                          0,
                          0,
                          (unsigned int)client->width,
                          (unsigned int)client->height);
        client_send_configure(client);
        return;
    }

    if (client->decor == CLIENT_DECOR_BORDER) {
        XMoveResizeWindow(wm.display,
                          client->frame,
                          client->x,
                          client->y,
                          (unsigned int)decor_outer_width(client),
                          (unsigned int)decor_outer_height(client));
        XMoveResizeWindow(wm.display,
                          client->window,
                          (int)AWM_BORDER_WIDTH,
                          (int)AWM_BORDER_WIDTH,
                          (unsigned int)client->width,
                          (unsigned int)client->height);
        client_send_configure(client);
        decor_redraw(client);
        return;
    }

    XMoveResizeWindow(wm.display,
                      client->frame,
                      client->x,
                      client->y,
                      (unsigned int)decor_outer_width(client),
                      (unsigned int)decor_outer_height(client));
    XMoveResizeWindow(wm.display,
                      client->titlebar,
                      (int)AWM_BORDER_WIDTH,
                      (int)AWM_BORDER_WIDTH,
                      (unsigned int)client->width,
                      AWM_TITLE_HEIGHT);
    XMoveResizeWindow(wm.display,
                      client->window,
                      (int)AWM_BORDER_WIDTH,
                      (int)AWM_BORDER_WIDTH + (int)AWM_TITLE_HEIGHT,
                      (unsigned int)client->width,
                      (unsigned int)client->height);

    size = button_size();
    button_y = ((int)AWM_TITLE_HEIGHT - size) / 2;
    button_positions(client, &minimize_x, &maximize_x, &close_x);
    XMoveResizeWindow(wm.display,
                      client->button_minimize,
                      minimize_x,
                      button_y,
                      (unsigned int)size,
                      (unsigned int)size);
    XMoveResizeWindow(wm.display,
                      client->button_maximize,
                      maximize_x,
                      button_y,
                      (unsigned int)size,
                      (unsigned int)size);
    XMoveResizeWindow(wm.display,
                      client->button_close,
                      close_x,
                      button_y,
                      (unsigned int)size,
                      (unsigned int)size);
    XMapWindow(wm.display, client->titlebar);
    client_send_configure(client);
    decor_redraw(client);
}

void decor_move(Client *client, int x, int y)
{
    if (client == NULL || client->frame == None) {
        return;
    }
    client->x = x;
    client->y = y;
    XMoveWindow(wm.display, client->frame, x, y);
    client_send_configure(client);
}

static void draw_button(Client *client, Window button, ClientPart part)
{
    int size = button_size();
    int inset = size >= 14 ? 5 : 3;
    int far = size - inset - 1;

    draw_fill(button,
              wm.button_background,
              0,
              0,
              (unsigned int)size,
              (unsigned int)size);

    if (part == CLIENT_PART_MINIMIZE) {
        draw_line(button,
                  wm.button_symbol,
                  inset,
                  far - 1,
                  far,
                  far - 1);
    } else if (part == CLIENT_PART_CLOSE) {
        draw_line(button, wm.button_symbol, inset, inset, far, far);
        draw_line(button, wm.button_symbol, far, inset, inset, far);
    } else if (part == CLIENT_PART_MAXIMIZE) {
        if (client->maximized) {
            draw_rectangle(button,
                           wm.button_symbol,
                           inset + 2,
                           inset,
                           (unsigned int)(far - inset - 2),
                           (unsigned int)(far - inset - 2));
            draw_rectangle(button,
                           wm.button_symbol,
                           inset,
                           inset + 2,
                           (unsigned int)(far - inset - 2),
                           (unsigned int)(far - inset - 2));
        } else {
            draw_rectangle(button,
                           wm.button_symbol,
                           inset,
                           inset,
                           (unsigned int)(far - inset),
                           (unsigned int)(far - inset));
        }
    }
}

void decor_redraw(Client *client)
{
    bool active;
    unsigned long border_color;
    unsigned long title_color;
    unsigned long text_color;
    int minimize_x;
    int maximize_x;
    int close_x;
    int text_width;

    if (client == NULL || client->frame == None || client->fullscreen ||
        client->decor == CLIENT_DECOR_NONE) {
        return;
    }

    active = wm.focused == client;
    border_color = active ? wm.focused_border : wm.unfocused_border;
    title_color = active ? wm.title_active : wm.title_inactive;
    text_color = active ? wm.title_text_active : wm.title_text_inactive;

    XSetWindowBackground(wm.display, client->frame, border_color);
    XClearWindow(wm.display, client->frame);
    if (client->decor == CLIENT_DECOR_BORDER || client->titlebar == None) {
        return;
    }
    draw_fill(client->titlebar,
              title_color,
              0,
              0,
              (unsigned int)client->width,
              AWM_TITLE_HEIGHT);

    button_positions(client, &minimize_x, &maximize_x, &close_x);
    (void)maximize_x;
    (void)close_x;
    text_width = minimize_x - 12;
    if (text_width > 0) {
        draw_text_clipped(client->titlebar,
                          text_color,
                          7,
                          draw_text_baseline((int)AWM_TITLE_HEIGHT),
                          5,
                          0,
                          (unsigned int)text_width,
                          AWM_TITLE_HEIGHT,
                          client->title != NULL ? client->title : "(sin titulo)");
    }

    draw_button(client, client->button_minimize, CLIENT_PART_MINIMIZE);
    draw_button(client, client->button_maximize, CLIENT_PART_MAXIMIZE);
    draw_button(client, client->button_close, CLIENT_PART_CLOSE);
}

void decor_rebuild(Client *client, ClientDecor previous)
{
    int old_border;
    int old_title;
    int client_root_x;
    int client_root_y;

    if (client == NULL || client->frame == None || previous == client->decor) {
        return;
    }

    old_border = previous == CLIENT_DECOR_NONE ? 0 : (int)AWM_BORDER_WIDTH;
    old_title = previous == CLIENT_DECOR_FULL ? (int)AWM_TITLE_HEIGHT : 0;
    client_root_x = client->x + (client->fullscreen ? 0 : old_border);
    client_root_y = client->y + (client->fullscreen ? 0 : old_border + old_title);

    if (client->titlebar != None) {
        XDestroyWindow(wm.display, client->titlebar);
        client->titlebar = None;
        client->button_minimize = None;
        client->button_maximize = None;
        client->button_close = None;
    }
    create_controls(client);
    select_decor_events(client);
    if (client->titlebar != None) {
        XDefineCursor(wm.display, client->titlebar, wm.move_cursor);
        XMapWindow(wm.display, client->titlebar);
        XMapWindow(wm.display, client->button_minimize);
        XMapWindow(wm.display, client->button_maximize);
        XMapWindow(wm.display, client->button_close);
    }

    client->x = client_root_x - decor_border_width(client);
    client->y = client_root_y - decor_border_width(client) -
                decor_title_height(client);
    decor_apply_geometry(client);
    ewmh_set_frame_extents(client);
}

void decor_set_active(Client *client, bool active)
{
    (void)active;
    decor_redraw(client);
}

void decor_handle_expose(const XExposeEvent *event)
{
    Client *client;

    if (event == NULL || event->count != 0) {
        return;
    }
    client = client_find_any(event->window);
    if (client != NULL) {
        decor_redraw(client);
    }
}
