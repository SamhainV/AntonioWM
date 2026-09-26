#include "wm.h"

#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>

#include <stdint.h>
#include <stdlib.h>

static bool same_geometry(const Monitor *left, const Monitor *right)
{
    return left->x == right->x && left->y == right->y &&
           left->width == right->width && left->height == right->height;
}

static bool append_monitor(Monitor **monitors,
                           size_t *count,
                           const Monitor *candidate)
{
    Monitor *resized;
    size_t index;

    if (candidate->width <= 0 || candidate->height <= 0) {
        return true;
    }
    for (index = 0U; index < *count; ++index) {
        if (same_geometry(&(*monitors)[index], candidate)) {
            (*monitors)[index].primary =
                (*monitors)[index].primary || candidate->primary;
            return true;
        }
    }
    if (*count == SIZE_MAX / sizeof(**monitors)) {
        return false;
    }
    resized = realloc(*monitors, (*count + 1U) * sizeof(**monitors));
    if (resized == NULL) {
        return false;
    }
    resized[*count] = *candidate;
    *monitors = resized;
    ++*count;
    return true;
}

static bool query_randr_monitors(Monitor **monitors, size_t *count)
{
    XRRMonitorInfo *information;
    int monitor_count = 0;
    int index;
    int major = 1;
    int minor = 5;

    if (!wm.randr_available ||
        XRRQueryVersion(wm.display, &major, &minor) == 0 ||
        major < 1 || (major == 1 && minor < 5)) {
        return false;
    }

    information = XRRGetMonitors(wm.display, wm.root, True, &monitor_count);
    if (information == NULL || monitor_count <= 0) {
        if (information != NULL) {
            XRRFreeMonitors(information);
        }
        return false;
    }

    for (index = 0; index < monitor_count; ++index) {
        Monitor candidate;

        candidate.x = information[index].x;
        candidate.y = information[index].y;
        candidate.width = information[index].width;
        candidate.height = information[index].height;
        candidate.primary = information[index].primary != False;
        if (!append_monitor(monitors, count, &candidate)) {
            free(*monitors);
            *monitors = NULL;
            *count = 0U;
            XRRFreeMonitors(information);
            return false;
        }
    }
    XRRFreeMonitors(information);
    return *count > 0U;
}

static bool query_xinerama_monitors(Monitor **monitors, size_t *count)
{
    XineramaScreenInfo *screens;
    int screen_count = 0;
    int index;

    if (XineramaIsActive(wm.display) == 0) {
        return false;
    }
    screens = XineramaQueryScreens(wm.display, &screen_count);
    if (screens == NULL || screen_count <= 0) {
        if (screens != NULL) {
            XFree(screens);
        }
        return false;
    }

    for (index = 0; index < screen_count; ++index) {
        Monitor candidate;

        candidate.x = screens[index].x_org;
        candidate.y = screens[index].y_org;
        candidate.width = screens[index].width;
        candidate.height = screens[index].height;
        candidate.primary = screens[index].screen_number == 0;
        if (!append_monitor(monitors, count, &candidate)) {
            free(*monitors);
            *monitors = NULL;
            *count = 0U;
            XFree(screens);
            return false;
        }
    }
    XFree(screens);
    return *count > 0U;
}

void monitor_refresh(void)
{
    Monitor *monitors = NULL;
    size_t count = 0U;

    if (!query_randr_monitors(&monitors, &count) &&
        !query_xinerama_monitors(&monitors, &count)) {
        Monitor fallback;

        fallback.x = 0;
        fallback.y = 0;
        fallback.width = wm.screen_width > 0 ? wm.screen_width : 1;
        fallback.height = wm.screen_height > 0 ? wm.screen_height : 1;
        fallback.primary = true;
        if (!append_monitor(&monitors, &count, &fallback)) {
            return;
        }
    }

    free(wm.monitors);
    wm.monitors = monitors;
    wm.monitor_count = count;
}

void monitor_initialize(void)
{
    int error_base;

    wm.randr_event_base = 0;
    wm.randr_available =
        XRRQueryExtension(wm.display, &wm.randr_event_base, &error_base) != 0;
    if (wm.randr_available) {
        XRRSelectInput(wm.display,
                       wm.root,
                       RRScreenChangeNotifyMask |
                           RRCrtcChangeNotifyMask |
                           RROutputChangeNotifyMask |
                           RRResourceChangeNotifyMask);
    }
    monitor_refresh();
}

void monitor_cleanup(void)
{
    free(wm.monitors);
    wm.monitors = NULL;
    wm.monitor_count = 0U;
}

static int64_t intersection_area(const Monitor *monitor,
                                 int x,
                                 int y,
                                 int width,
                                 int height)
{
    int64_t left = x > monitor->x ? x : monitor->x;
    int64_t top = y > monitor->y ? y : monitor->y;
    int64_t window_right = (int64_t)x + width;
    int64_t window_bottom = (int64_t)y + height;
    int64_t monitor_right = (int64_t)monitor->x + monitor->width;
    int64_t monitor_bottom = (int64_t)monitor->y + monitor->height;
    int64_t right = window_right < monitor_right ? window_right : monitor_right;
    int64_t bottom = window_bottom < monitor_bottom ? window_bottom : monitor_bottom;

    if (right <= left || bottom <= top) {
        return 0;
    }
    return (right - left) * (bottom - top);
}

static bool contains_scaled_point(const Monitor *monitor,
                                  int64_t x_times_two,
                                  int64_t y_times_two)
{
    int64_t left = (int64_t)monitor->x * 2;
    int64_t top = (int64_t)monitor->y * 2;
    int64_t right = ((int64_t)monitor->x + monitor->width) * 2;
    int64_t bottom = ((int64_t)monitor->y + monitor->height) * 2;

    return x_times_two >= left && x_times_two < right &&
           y_times_two >= top && y_times_two < bottom;
}

static int64_t distance_to_monitor(const Monitor *monitor,
                                   int64_t x_times_two,
                                   int64_t y_times_two)
{
    int64_t left = (int64_t)monitor->x * 2;
    int64_t top = (int64_t)monitor->y * 2;
    int64_t right = ((int64_t)monitor->x + monitor->width) * 2;
    int64_t bottom = ((int64_t)monitor->y + monitor->height) * 2;
    int64_t delta_x = 0;
    int64_t delta_y = 0;

    if (x_times_two < left) {
        delta_x = left - x_times_two;
    } else if (x_times_two > right) {
        delta_x = x_times_two - right;
    }
    if (y_times_two < top) {
        delta_y = top - y_times_two;
    } else if (y_times_two > bottom) {
        delta_y = y_times_two - bottom;
    }
    return delta_x * delta_x + delta_y * delta_y;
}

void monitor_for_rectangle(int x,
                           int y,
                           int width,
                           int height,
                           Monitor *monitor)
{
    size_t best = 0U;
    size_t index;
    int64_t best_area = -1;
    int64_t center_x;
    int64_t center_y;
    bool best_contains_center = false;
    bool found_intersection = false;

    if (monitor == NULL) {
        return;
    }
    if (wm.monitors == NULL || wm.monitor_count == 0U) {
        monitor->x = 0;
        monitor->y = 0;
        monitor->width = wm.screen_width > 0 ? wm.screen_width : 1;
        monitor->height = wm.screen_height > 0 ? wm.screen_height : 1;
        monitor->primary = true;
        return;
    }

    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    center_x = (int64_t)x * 2 + width;
    center_y = (int64_t)y * 2 + height;

    for (index = 0U; index < wm.monitor_count; ++index) {
        int64_t area = intersection_area(&wm.monitors[index],
                                         x,
                                         y,
                                         width,
                                         height);
        bool contains_center = contains_scaled_point(&wm.monitors[index],
                                                     center_x,
                                                     center_y);

        if (area > best_area ||
            (area == best_area && contains_center && !best_contains_center) ||
            (area == best_area && contains_center == best_contains_center &&
             wm.monitors[index].primary && !wm.monitors[best].primary)) {
            best = index;
            best_area = area;
            best_contains_center = contains_center;
        }
        if (area > 0) {
            found_intersection = true;
        }
    }

    if (!found_intersection) {
        bool found_center = false;
        int64_t best_distance = INT64_MAX;

        for (index = 0U; index < wm.monitor_count; ++index) {
            bool contains_center = contains_scaled_point(&wm.monitors[index],
                                                         center_x,
                                                         center_y);
            int64_t distance = distance_to_monitor(&wm.monitors[index],
                                                   center_x,
                                                   center_y);

            if ((contains_center && !found_center) ||
                (contains_center == found_center && distance < best_distance) ||
                (contains_center == found_center && distance == best_distance &&
                 wm.monitors[index].primary && !wm.monitors[best].primary)) {
                best = index;
                best_distance = distance;
            }
            found_center = found_center || contains_center;
        }
    }
    *monitor = wm.monitors[best];
}

void monitor_for_client(const Client *client, Monitor *monitor)
{
    if (client == NULL) {
        monitor_for_rectangle(0, 0, 1, 1, monitor);
        return;
    }
    monitor_for_rectangle(client->x,
                          client->y,
                          decor_outer_width(client),
                          decor_outer_height(client),
                          monitor);
}

bool monitor_handle_event(XEvent *event)
{
    if (!wm.randr_available || event == NULL) {
        return false;
    }
    if (event->type == wm.randr_event_base + RRScreenChangeNotify) {
        (void)XRRUpdateConfiguration(event);
        return true;
    }
    if (event->type == wm.randr_event_base + RRNotify) {
        return true;
    }
    return false;
}
