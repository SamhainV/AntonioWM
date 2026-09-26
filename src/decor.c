#include "wm.h"
#include "config.h"

#include <X11/X.h>

#include <limits.h>
#include <string.h>

static int protocol_position(int value)
{
    if (value < SHRT_MIN) {
        return SHRT_MIN;
    }
    if (value > SHRT_MAX) {
        return SHRT_MAX;
    }
    return value;
}

static int button_size(void)
{
    int size = (int)AWM_BUTTON_SIZE;
    int maximum = (int)AWM_TITLE_HEIGHT - 2;

    if (size > maximum) {
        size = maximum;
    }
    return size >= 8 ? size : 8;
}

static int button_gap(void)
{
    return AWM_ELEMENT_GAP;
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

static Window create_resize_handle(Client *client,
                                   Cursor cursor,
                                   const char *name)
{
    XSetWindowAttributes attributes;
    Window handle;

    memset(&attributes, 0, sizeof(attributes));
    attributes.event_mask = ButtonPressMask;
    attributes.cursor = cursor;
    handle = XCreateWindow(wm.display,
                           client->frame,
                           0,
                           0,
                           1U,
                           1U,
                           0U,
                           0,
                           InputOnly,
                           CopyFromParent,
                           CWEventMask | CWCursor,
                           &attributes);
    XStoreName(wm.display, handle, name);
    return handle;
}

static void create_resize_handles(Client *client)
{
    client->resize_left = create_resize_handle(client,
                                               wm.resize_left_cursor,
                                               "AntonioWM Resize Left");
    client->resize_right = create_resize_handle(client,
                                                wm.resize_right_cursor,
                                                "AntonioWM Resize Right");
    client->resize_bottom = create_resize_handle(client,
                                                 wm.resize_bottom_cursor,
                                                 "AntonioWM Resize Bottom");
    client->resize_bottom_left =
        create_resize_handle(client,
                             wm.resize_bottom_left_cursor,
                             "AntonioWM Resize Bottom Left");
    client->resize_bottom_right =
        create_resize_handle(client,
                             wm.resize_cursor,
                             "AntonioWM Resize Bottom Right");
}

static void unmap_resize_handles(const Client *client)
{
    XUnmapWindow(wm.display, client->resize_left);
    XUnmapWindow(wm.display, client->resize_right);
    XUnmapWindow(wm.display, client->resize_bottom);
    XUnmapWindow(wm.display, client->resize_bottom_left);
    XUnmapWindow(wm.display, client->resize_bottom_right);
}

static void layout_resize_handles(Client *client)
{
    int outer_width;
    int outer_height;
    int grab = (int)AWM_RESIZE_GRAB_WIDTH;
    int side_y;
    int side_height;
    int bottom_width;

    if (client->resize_left == None) {
        return;
    }
    if (client->fullscreen || client->decor == CLIENT_DECOR_NONE) {
        unmap_resize_handles(client);
        return;
    }

    outer_width = decor_outer_width(client);
    outer_height = decor_outer_height(client);
    if (grab < 1) {
        grab = 1;
    }
    if (grab * 2 > outer_width) {
        grab = outer_width / 2;
    }
    if (grab * 2 > outer_height) {
        grab = outer_height / 2;
    }
    if (grab < 1) {
        grab = 1;
    }

    side_y = decor_border_width(client) + decor_title_height(client);
    if (side_y > outer_height - grab) {
        side_y = 0;
    }
    side_height = outer_height - side_y - grab;
    if (side_height < 1) {
        side_height = 1;
    }
    bottom_width = outer_width - 2 * grab;
    if (bottom_width < 1) {
        bottom_width = 1;
    }

    XMoveResizeWindow(wm.display,
                      client->resize_left,
                      0,
                      side_y,
                      (unsigned int)grab,
                      (unsigned int)side_height);
    XMoveResizeWindow(wm.display,
                      client->resize_right,
                      outer_width - grab,
                      side_y,
                      (unsigned int)grab,
                      (unsigned int)side_height);
    XMoveResizeWindow(wm.display,
                      client->resize_bottom,
                      grab,
                      outer_height - grab,
                      (unsigned int)bottom_width,
                      (unsigned int)grab);
    XMoveResizeWindow(wm.display,
                      client->resize_bottom_left,
                      0,
                      outer_height - grab,
                      (unsigned int)grab,
                      (unsigned int)grab);
    XMoveResizeWindow(wm.display,
                      client->resize_bottom_right,
                      outer_width - grab,
                      outer_height - grab,
                      (unsigned int)grab,
                      (unsigned int)grab);

    XMapRaised(wm.display, client->resize_left);
    XMapRaised(wm.display, client->resize_right);
    XMapRaised(wm.display, client->resize_bottom);
    XMapRaised(wm.display, client->resize_bottom_left);
    XMapRaised(wm.display, client->resize_bottom_right);
}

static void define_control_cursors(const Client *client)
{
    if (client == NULL || client->titlebar == None) {
        return;
    }

    XDefineCursor(wm.display, client->titlebar, wm.normal_cursor);
    XDefineCursor(wm.display, client->button_minimize, wm.normal_cursor);
    XDefineCursor(wm.display, client->button_maximize, wm.normal_cursor);
    XDefineCursor(wm.display, client->button_close, wm.normal_cursor);
}

void decor_update_cursors(Client *client)
{
    if (client == NULL || client->frame == None) {
        return;
    }

    XDefineCursor(wm.display, client->frame, wm.normal_cursor);
    define_control_cursors(client);
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
                                PointerMotionMask |
                                EnterWindowMask |
                                LeaveWindowMask;

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
    create_resize_handles(client);

    XStoreName(wm.display, client->frame, "AntonioWM Frame");
    select_decor_events(client);
    decor_update_cursors(client);

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

    if (wm.pressed_button_client == client) {
        wm.pressed_button_client = NULL;
        wm.pressed_button_part = CLIENT_PART_NONE;
    }
    client->hovered_part = CLIENT_PART_NONE;

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
    client->resize_left = None;
    client->resize_right = None;
    client->resize_bottom = None;
    client->resize_bottom_left = None;
    client->resize_bottom_right = None;
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
    client->x = protocol_position(client->x);
    client->y = protocol_position(client->y);

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
        layout_resize_handles(client);
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
        layout_resize_handles(client);
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
    layout_resize_handles(client);
    client_send_configure(client);
    decor_redraw(client);
}

void decor_move(Client *client, int x, int y)
{
    if (client == NULL || client->frame == None) {
        return;
    }
    client->x = protocol_position(x);
    client->y = protocol_position(y);
    XMoveWindow(wm.display, client->frame, client->x, client->y);
    client_send_configure(client);
}

static void draw_button(Client *client, Window button, ClientPart part)
{
    int size = button_size();
    int inset = size >= 14 ? 4 : 3;
    int far = size - inset - 2;
    int inner = far - inset;
    int restored_inner;
    bool pressed = wm.pressed_button_client == client &&
                   wm.pressed_button_part == part;
    bool hovered = client->hovered_part == part;
    int offset = pressed ? 1 : 0;
    unsigned long background = pressed ? wm.button_pressed
                               : hovered ? wm.button_hover
                                         : wm.button_background;

    if (inner < 2) {
        inner = 2;
    }
    restored_inner = inner > 2 ? inner - 2 : 1;

    draw_fill(button,
              background,
              0,
              0,
              (unsigned int)size,
              (unsigned int)size);
    draw_rectangle(button,
                   wm.button_outline,
                   0,
                   0,
                   (unsigned int)(size - 1),
                   (unsigned int)(size - 1));

    if (part == CLIENT_PART_MINIMIZE) {
        draw_line(button,
                  wm.button_symbol,
                  inset + offset,
                  far + offset,
                  far + offset,
                  far + offset);
        draw_line(button,
                  wm.button_symbol,
                  inset + offset,
                  far + 1 + offset,
                  far + offset,
                  far + 1 + offset);
    } else if (part == CLIENT_PART_CLOSE) {
        draw_line(button,
                  wm.button_symbol,
                  inset + offset,
                  inset + offset,
                  far + offset,
                  far + offset);
        draw_line(button,
                  wm.button_symbol,
                  inset + 1 + offset,
                  inset + offset,
                  far + offset,
                  far - 1 + offset);
        draw_line(button,
                  wm.button_symbol,
                  far + offset,
                  inset + offset,
                  inset + offset,
                  far + offset);
        draw_line(button,
                  wm.button_symbol,
                  far - 1 + offset,
                  inset + offset,
                  inset + offset,
                  far - 1 + offset);
    } else if (part == CLIENT_PART_MAXIMIZE) {
        if (client->maximized) {
            draw_rectangle(button,
                           wm.button_symbol,
                           inset + 2 + offset,
                           inset + offset,
                           (unsigned int)restored_inner,
                           (unsigned int)restored_inner);
            draw_rectangle(button,
                           wm.button_symbol,
                           inset + offset,
                           inset + 2 + offset,
                           (unsigned int)restored_inner,
                           (unsigned int)restored_inner);
        } else {
            draw_rectangle(button,
                           wm.button_symbol,
                           inset + offset,
                           inset + offset,
                           (unsigned int)inner,
                           (unsigned int)inner);
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
    text_width = minimize_x - 2 * AWM_TITLE_TEXT_PADDING;
    if (text_width > 0) {
        draw_text_ellipsized(
            client->titlebar,
            text_color,
            AWM_TITLE_TEXT_PADDING,
            draw_text_baseline((int)AWM_TITLE_HEIGHT),
            AWM_TITLE_TEXT_PADDING,
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
        if (wm.pressed_button_client == client) {
            wm.pressed_button_client = NULL;
            wm.pressed_button_part = CLIENT_PART_NONE;
        }
        client->hovered_part = CLIENT_PART_NONE;
        XDestroyWindow(wm.display, client->titlebar);
        client->titlebar = None;
        client->button_minimize = None;
        client->button_maximize = None;
        client->button_close = None;
    }
    create_controls(client);
    select_decor_events(client);
    decor_update_cursors(client);
    if (client->titlebar != None) {
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

static bool is_button_part(ClientPart part)
{
    return part == CLIENT_PART_MINIMIZE ||
           part == CLIENT_PART_MAXIMIZE ||
           part == CLIENT_PART_CLOSE;
}

void decor_handle_enter(const XCrossingEvent *event)
{
    Client *client;
    ClientPart part;

    if (event == NULL || event->mode != NotifyNormal) {
        return;
    }
    client = client_find_any(event->window);
    if (client == NULL) {
        return;
    }
    part = client_window_part(client, event->window);
    if (is_button_part(part) && client->hovered_part != part) {
        client->hovered_part = part;
        decor_redraw(client);
    }
}

void decor_handle_leave(const XCrossingEvent *event)
{
    Client *client;
    ClientPart part;

    if (event == NULL || event->mode != NotifyNormal) {
        return;
    }
    client = client_find_any(event->window);
    if (client == NULL) {
        return;
    }
    part = client_window_part(client, event->window);
    if (is_button_part(part) && client->hovered_part == part) {
        client->hovered_part = CLIENT_PART_NONE;
        decor_redraw(client);
    }
}

void decor_button_press(Client *client, ClientPart part)
{
    Client *previous;

    if (client == NULL || !is_button_part(part)) {
        return;
    }
    previous = wm.pressed_button_client;
    wm.pressed_button_client = client;
    wm.pressed_button_part = part;
    client->hovered_part = part;
    if (previous != NULL && previous != client) {
        decor_redraw(previous);
    }
    decor_redraw(client);
}

void decor_button_release(Client *client)
{
    if (wm.pressed_button_client == client) {
        wm.pressed_button_client = NULL;
        wm.pressed_button_part = CLIENT_PART_NONE;
    }
    if (client != NULL) {
        decor_redraw(client);
    }
}
