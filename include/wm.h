#ifndef ANTONIOWM_WM_H
#define ANTONIOWM_WM_H

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include <stdbool.h>
#include <stddef.h>

#define AWM_RUNTIME_MAX_LAUNCHERS 16U
#define AWM_RUNTIME_MAX_DESKTOPS 16U
#define AWM_RUNTIME_LABEL_LENGTH 64U
#define AWM_RUNTIME_COMMAND_LENGTH 512U
#define AWM_RUNTIME_PATH_LENGTH 1024U

typedef struct {
    char label[AWM_RUNTIME_LABEL_LENGTH];
    char command[AWM_RUNTIME_COMMAND_LENGTH];
    char icon[AWM_RUNTIME_PATH_LENGTH];
} LauncherConfig;

typedef struct {
    char terminal[AWM_RUNTIME_COMMAND_LENGTH];
    char screenshot_command[AWM_RUNTIME_COMMAND_LENGTH];
    bool taskbar_enabled;
    unsigned int taskbar_height;
    int taskbar_gap;
    int taskbar_padding;
    bool focus_follows_mouse;
    size_t launcher_count;
    LauncherConfig launchers[AWM_RUNTIME_MAX_LAUNCHERS];
    char desktop_names[AWM_RUNTIME_MAX_DESKTOPS][AWM_RUNTIME_LABEL_LENGTH];
} RuntimeConfig;

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned char *rgba;
} IconImage;

typedef struct Client Client;

typedef enum {
    CLIENT_TYPE_NORMAL = 0,
    CLIENT_TYPE_DIALOG,
    CLIENT_TYPE_UTILITY,
    CLIENT_TYPE_TOOLBAR,
    CLIENT_TYPE_MENU,
    CLIENT_TYPE_DROPDOWN_MENU,
    CLIENT_TYPE_POPUP_MENU,
    CLIENT_TYPE_TOOLTIP,
    CLIENT_TYPE_NOTIFICATION,
    CLIENT_TYPE_SPLASH,
    CLIENT_TYPE_DOCK,
    CLIENT_TYPE_DESKTOP
} ClientWindowType;

typedef enum {
    CLIENT_DECOR_NONE = 0,
    CLIENT_DECOR_BORDER,
    CLIENT_DECOR_FULL
} ClientDecor;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} WorkArea;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    bool primary;
} Monitor;

typedef struct {
    Atom wm_protocols;
    Atom wm_delete_window;
    Atom wm_take_focus;
    Atom wm_state;
    Atom wm_change_state;
    Atom wm_selection;
    Atom manager;
    Atom net_supported;
    Atom net_supporting_wm_check;
    Atom net_active_window;
    Atom net_client_list;
    Atom net_client_list_stacking;
    Atom net_number_of_desktops;
    Atom net_current_desktop;
    Atom net_desktop_names;
    Atom net_desktop_geometry;
    Atom net_desktop_viewport;
    Atom net_workarea;
    Atom net_frame_extents;
    Atom net_request_frame_extents;
    Atom net_wm_desktop;
    Atom net_wm_state;
    Atom net_wm_state_fullscreen;
    Atom net_wm_state_maximized_vert;
    Atom net_wm_state_maximized_horz;
    Atom net_wm_state_hidden;
    Atom net_wm_state_above;
    Atom net_wm_state_below;
    Atom net_wm_state_sticky;
    Atom net_wm_state_skip_taskbar;
    Atom net_wm_state_skip_pager;
    Atom net_wm_state_demands_attention;
    Atom net_wm_state_modal;
    Atom net_wm_window_type;
    Atom net_wm_window_type_normal;
    Atom net_wm_window_type_dialog;
    Atom net_wm_window_type_utility;
    Atom net_wm_window_type_toolbar;
    Atom net_wm_window_type_menu;
    Atom net_wm_window_type_dropdown_menu;
    Atom net_wm_window_type_popup_menu;
    Atom net_wm_window_type_tooltip;
    Atom net_wm_window_type_notification;
    Atom net_wm_window_type_splash;
    Atom net_wm_window_type_dock;
    Atom net_wm_window_type_desktop;
    Atom net_wm_strut;
    Atom net_wm_strut_partial;
    Atom net_wm_user_time;
    Atom net_wm_allowed_actions;
    Atom net_wm_action_close;
    Atom net_wm_action_minimize;
    Atom net_wm_action_maximized_vert;
    Atom net_wm_action_maximized_horz;
    Atom net_wm_action_fullscreen;
    Atom net_wm_action_move;
    Atom net_wm_action_resize;
    Atom net_wm_action_change_desktop;
    Atom net_wm_action_above;
    Atom net_wm_action_below;
    Atom net_close_window;
    Atom net_moveresize_window;
    Atom net_wm_moveresize;
    Atom net_restack_window;
    Atom net_wm_name;
    Atom motif_wm_hints;
    Atom utf8_string;
} Atoms;

typedef enum {
    CLIENT_PART_NONE = 0,
    CLIENT_PART_WINDOW,
    CLIENT_PART_FRAME,
    CLIENT_PART_TITLE,
    CLIENT_PART_MINIMIZE,
    CLIENT_PART_MAXIMIZE,
    CLIENT_PART_CLOSE,
    CLIENT_PART_RESIZE_LEFT,
    CLIENT_PART_RESIZE_RIGHT,
    CLIENT_PART_RESIZE_BOTTOM,
    CLIENT_PART_RESIZE_BOTTOM_LEFT,
    CLIENT_PART_RESIZE_BOTTOM_RIGHT
} ClientPart;

typedef enum {
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE
} DragMode;

typedef struct {
    DragMode mode;
    Client *client;
    int pointer_x;
    int pointer_y;
    int window_x;
    int window_y;
    int window_width;
    int window_height;
    bool resize_left;
    bool resize_top;
    bool resize_horizontal;
    bool resize_vertical;
    bool pointer_grabbed;
} DragState;

struct Client {
    Window window;
    Window frame;
    Window titlebar;
    Window button_minimize;
    Window button_maximize;
    Window button_close;
    Window resize_left;
    Window resize_right;
    Window resize_bottom;
    Window resize_bottom_left;
    Window resize_bottom_right;

    /* Root-relative outer-frame position and client content dimensions. */
    int x;
    int y;
    int width;
    int height;

    /* Normal geometry restored after maximization. */
    int old_x;
    int old_y;
    int old_width;
    int old_height;

    /* Geometry restored after leaving fullscreen. */
    int fullscreen_x;
    int fullscreen_y;
    int fullscreen_width;
    int fullscreen_height;

    unsigned int desktop;
    Window transient_for;
    ClientWindowType window_type;
    ClientDecor decor;
    bool maximized;
    bool fullscreen;
    bool minimized;
    bool above;
    bool below;
    bool sticky;
    bool skip_taskbar;
    bool skip_pager;
    bool demands_attention;
    bool urgent;
    bool modal;
    bool accepts_input;
    bool takes_focus;
    bool position_explicit;
    bool has_strut;
    unsigned long strut[12];

    int original_border_width;
    bool reparented;
    bool mapped;
    bool visible;
    unsigned int ignore_unmap;
    ClientPart hovered_part;
    char *title;

    Client *previous;
    Client *next;
};

typedef struct {
    Display *display;
    int screen;
    Window root;
    int screen_width;
    int screen_height;
    Monitor *monitors;
    size_t monitor_count;
    int randr_event_base;
    bool randr_available;

    Client *clients;
    Client *clients_tail;
    Client *focused;
    Window supporting_window;
    Window bar_window;
    Window taskbar_window;

    unsigned int desktop_count;
    unsigned int current_desktop;
    unsigned int *cascade_indices;
    Window *desktop_focus;

    Atoms atoms;
    unsigned long focused_border;
    unsigned long unfocused_border;
    unsigned long title_active;
    unsigned long title_inactive;
    unsigned long title_text_active;
    unsigned long title_text_inactive;
    unsigned long button_background;
    unsigned long button_hover;
    unsigned long button_pressed;
    unsigned long button_symbol;
    unsigned long button_outline;
    unsigned long bar_background;
    unsigned long bar_foreground;
    unsigned long bar_item;
    unsigned long bar_selected;
    unsigned long bar_active_window;
    unsigned long bar_minimized;
    unsigned long bar_urgent;
    unsigned long bar_separator;

    GC draw_gc;
    XftFont *xft_font;
    XFontSet font_set;
    XFontStruct *fallback_font;
    int font_ascent;
    int font_descent;

    unsigned int numlock_mask;
    Cursor normal_cursor;
    Cursor move_cursor;
    Cursor resize_cursor;
    Cursor resize_left_cursor;
    Cursor resize_right_cursor;
    Cursor resize_bottom_cursor;
    Cursor resize_bottom_left_cursor;

    DragState drag;
    Client *pressed_button_client;
    ClientPart pressed_button_part;
    Time last_event_time;
    bool running;
    bool restarting;

    RuntimeConfig config;

    int argc;
    char **argv;
} WM;

extern WM wm;

/* main.c */
bool wm_initialize(int argc, char **argv);
void wm_run(void);
void wm_cleanup(void);
void wm_restart(void);

/* monitor.c */
void monitor_initialize(void);
void monitor_refresh(void);
void monitor_cleanup(void);
void monitor_for_rectangle(int x,
                           int y,
                           int width,
                           int height,
                           Monitor *monitor);
void monitor_for_client(const Client *client, Monitor *monitor);
bool monitor_handle_event(XEvent *event);

/* runtime_config.c */
void runtime_config_load(void);

/* icon.c */
bool icon_load_png(const char *path, IconImage *image);
void icon_free(IconImage *image);
bool icon_draw(Drawable drawable,
               const IconImage *image,
               int x,
               int y,
               unsigned int width,
               unsigned int height,
               unsigned long background);

/* events.c */
void events_dispatch(XEvent *event);

/* client.c */
Client *client_find(Window window);
Client *client_find_any(Window window);
ClientPart client_window_part(const Client *client, Window window);
void client_manage(Window window, bool map_requested);
void client_show(Client *client);
void client_unmanage(Client *client, bool destroyed);
void client_cleanup_all(void);
void client_focus(Client *client, bool raise);
void client_note_focus(Client *client);
void client_sync_focus(void);
void client_cycle_focus(int direction);
void client_close(Client *client);
void client_update_title(Client *client);
void client_configure_request(const XConfigureRequestEvent *event);
void client_apply_size_hints(Client *client, int *width, int *height);
void client_set_wm_state(Client *client, long state);
void client_set_visible(Client *client, bool visible);
void client_set_maximized(Client *client, bool maximized);
void client_toggle_maximized(Client *client);
void client_set_fullscreen(Client *client, bool fullscreen);
void client_toggle_fullscreen(Client *client);
void client_set_minimized(Client *client, bool minimized);
void client_minimize(Client *client);
void client_restore(Client *client);
void client_reapply_screen_state(Client *client);
void client_send_configure(Client *client);
bool client_is_focus_candidate(const Client *client);
bool client_is_taskbar_visible(const Client *client);
void client_raise(Client *client);
void client_refresh_decor(Client *client);
void client_reconsider_focus(Client *client);

/* keyboard.c */
void keyboard_grab_keys(void);
void keyboard_ungrab_keys(void);
void keyboard_handle_keypress(XKeyEvent *event);
void keyboard_refresh_mapping(XMappingEvent *event);
unsigned int keyboard_clean_modifiers(unsigned int state);
void command_spawn(const char *command);

/* mouse.c */
void mouse_grab_buttons(Client *client, bool focused);
void mouse_regrab_all(void);
void mouse_handle_button_press(XButtonEvent *event);
void mouse_handle_button_release(XButtonEvent *event);
void mouse_handle_motion(XMotionEvent *event);
void mouse_cancel_drag(Client *client);
void mouse_begin_moveresize(Client *client,
                           int root_x,
                           int root_y,
                           int direction,
                           Time time);

/* desktop.c */
bool desktop_initialize(void);
void desktop_cleanup(void);
bool desktop_is_current(const Client *client);
void desktop_place_client(Client *client,
                          const XWindowAttributes *attributes,
                          bool map_requested);
void desktop_switch(unsigned int desktop);
void desktop_move_client(Client *client, unsigned int desktop);
void desktop_remember_focus(const Client *client);
void desktop_forget_client(const Client *client);
void desktop_reapply_screen_states(void);

/* draw.c */
bool draw_initialize(void);
void draw_cleanup(void);
int draw_text_width(const char *text);
int draw_text_baseline(int area_height);
void draw_fill(Drawable drawable,
               unsigned long color,
               int x,
               int y,
               unsigned int width,
               unsigned int height);
void draw_line(Drawable drawable,
               unsigned long color,
               int x1,
               int y1,
               int x2,
               int y2);
void draw_rectangle(Drawable drawable,
                    unsigned long color,
                    int x,
                    int y,
                    unsigned int width,
                    unsigned int height);
void draw_text_clipped(Drawable drawable,
                       unsigned long color,
                       int x,
                       int baseline,
                       int clip_x,
                       int clip_y,
                       unsigned int clip_width,
                       unsigned int clip_height,
                       const char *text);
void draw_text_ellipsized(Drawable drawable,
                          unsigned long color,
                          int x,
                          int baseline,
                          int clip_x,
                          int clip_y,
                          unsigned int clip_width,
                          unsigned int clip_height,
                          const char *text);

/* decor.c */
bool decor_create(Client *client, bool was_viewable);
void decor_release(Client *client, bool destroyed, bool map_client);
void decor_apply_geometry(Client *client);
void decor_move(Client *client, int x, int y);
void decor_set_active(Client *client, bool active);
void decor_redraw(Client *client);
void decor_handle_expose(const XExposeEvent *event);
void decor_handle_enter(const XCrossingEvent *event);
void decor_handle_leave(const XCrossingEvent *event);
void decor_button_press(Client *client, ClientPart part);
void decor_button_release(Client *client);
void decor_rebuild(Client *client, ClientDecor previous);
void decor_update_cursors(Client *client);
int decor_outer_width(const Client *client);
int decor_outer_height(const Client *client);
int decor_client_root_x(const Client *client);
int decor_client_root_y(const Client *client);
int decor_border_width(const Client *client);
int decor_title_height(const Client *client);

/* bar.c */
bool bar_initialize(void);
void bar_cleanup(void);
void bar_update(void);
void bar_reconfigure(void);
void bar_raise(void);
bool bar_handle_button_press(const XButtonEvent *event);
void bar_handle_expose(const XExposeEvent *event);
int bar_next_timeout_ms(void);
void bar_tick(void);

/* taskbar.c */
bool taskbar_initialize(void);
void taskbar_cleanup(void);
void taskbar_update(void);
void taskbar_reconfigure(void);
void taskbar_raise(void);
bool taskbar_handle_button_press(const XButtonEvent *event);
bool taskbar_handle_button_release(const XButtonEvent *event);
bool taskbar_handle_motion(const XMotionEvent *event);
bool taskbar_handle_enter(const XCrossingEvent *event);
bool taskbar_handle_leave(const XCrossingEvent *event);
void taskbar_handle_expose(const XExposeEvent *event);

/* ewmh.c */
void ewmh_initialize(void);
void ewmh_cleanup(void);
unsigned int ewmh_get_window_desktop(Window window, unsigned int fallback);
void ewmh_read_client_state(Client *client);
void ewmh_set_client_desktop(const Client *client);
void ewmh_set_allowed_actions(const Client *client);
void ewmh_set_frame_extents(const Client *client);
void ewmh_update_client_state(const Client *client);
void ewmh_clear_client_properties(const Client *client);
void ewmh_update_active_window(void);
void ewmh_update_client_list(void);
void ewmh_update_desktop_properties(void);
void ewmh_refresh_strut(Client *client);
void ewmh_recalculate_workarea(void);
void ewmh_get_workarea(unsigned int desktop, WorkArea *area);
void ewmh_get_monitor_workarea(unsigned int desktop,
                               const Monitor *monitor,
                               WorkArea *area);
void ewmh_get_bar_geometry(int *x, int *y, int *width, int *height);
void ewmh_get_taskbar_geometry(int *x, int *y, int *width, int *height);
bool ewmh_handle_client_message(const XClientMessageEvent *event);

/* hints.c */
void hints_initialize_client(Client *client);
void hints_refresh_transient(Client *client);
void hints_refresh_window_type(Client *client);
void hints_refresh_focus_model(Client *client);
void hints_refresh_urgency(Client *client);
void hints_apply_size(Client *client, int *width, int *height);
bool hints_position_is_explicit(const Client *client);
bool hints_wants_initial_iconic(const Client *client);
ClientDecor hints_requested_decor(const Client *client);

/* stacking.c */
void stacking_raise(Client *client);
void stacking_lower(Client *client);
void stacking_enforce(void);
void stacking_restack_request(Client *client, Window sibling, int detail);

#endif
