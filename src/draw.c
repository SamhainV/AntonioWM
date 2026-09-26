#include "wm.h"
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int text_length(const char *text)
{
    size_t length = text != NULL ? strlen(text) : 0U;

    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

bool draw_initialize(void)
{
    char **missing = NULL;
    int missing_count = 0;
    char *default_string = NULL;
    XFontSetExtents *extents;
    char xft_pattern[256];
    int pattern_length;

    wm.draw_gc = XCreateGC(wm.display, wm.root, 0UL, NULL);
    if (wm.draw_gc == NULL) {
        fprintf(stderr, "%s: no se pudo crear el contexto de dibujo\n", AWM_NAME);
        return false;
    }

    pattern_length = snprintf(xft_pattern,
                              sizeof(xft_pattern),
                              "%s:size=%u:antialias=true:hinting=true",
                              AWM_FONT_FAMILY,
                              AWM_FONT_SIZE);
    if (pattern_length > 0 && (size_t)pattern_length < sizeof(xft_pattern)) {
        wm.xft_font = XftFontOpenName(wm.display, wm.screen, xft_pattern);
    }
    if (wm.xft_font == NULL && strcmp(AWM_FONT_FAMILY, "sans") != 0) {
        pattern_length = snprintf(xft_pattern,
                                  sizeof(xft_pattern),
                                  "sans:size=%u:antialias=true:hinting=true",
                                  AWM_FONT_SIZE);
        if (pattern_length > 0 && (size_t)pattern_length < sizeof(xft_pattern)) {
            wm.xft_font = XftFontOpenName(wm.display, wm.screen, xft_pattern);
        }
    }
    if (wm.xft_font != NULL) {
        wm.font_ascent = wm.xft_font->ascent;
        wm.font_descent = wm.xft_font->descent;
        return true;
    }

    fprintf(stderr,
            "%s: no se pudo cargar la fuente Xft '%s'; se usara el respaldo X11\n",
            AWM_NAME,
            AWM_FONT_FAMILY);
    wm.font_set = XCreateFontSet(wm.display,
                                 AWM_FONT_FALLBACK,
                                 &missing,
                                 &missing_count,
                                 &default_string);
    if (missing != NULL) {
        XFreeStringList(missing);
    }

    if (wm.font_set == NULL && strcmp(AWM_FONT_FALLBACK, "fixed") != 0) {
        missing = NULL;
        missing_count = 0;
        default_string = NULL;
        wm.font_set = XCreateFontSet(wm.display,
                                     "fixed",
                                     &missing,
                                     &missing_count,
                                     &default_string);
        if (missing != NULL) {
            XFreeStringList(missing);
        }
    }

    if (wm.font_set != NULL) {
        extents = XExtentsOfFontSet(wm.font_set);
        wm.font_ascent = -extents->max_logical_extent.y;
        wm.font_descent = extents->max_logical_extent.height - wm.font_ascent;
        if (wm.font_ascent < 1) {
            wm.font_ascent = 10;
        }
        if (wm.font_descent < 0) {
            wm.font_descent = 3;
        }
        return true;
    }

    wm.fallback_font = XLoadQueryFont(wm.display, AWM_FONT_FALLBACK);
    if (wm.fallback_font == NULL) {
        wm.fallback_font = XLoadQueryFont(wm.display, "fixed");
    }
    if (wm.fallback_font != NULL) {
        wm.font_ascent = wm.fallback_font->ascent;
        wm.font_descent = wm.fallback_font->descent;
        XSetFont(wm.display, wm.draw_gc, wm.fallback_font->fid);
        return true;
    }

    wm.font_ascent = 10;
    wm.font_descent = 3;
    fprintf(stderr,
            "%s: no se pudo cargar la fuente '%s'; se usara la fuente del GC\n",
            AWM_NAME,
            AWM_FONT_FALLBACK);
    return true;
}

void draw_cleanup(void)
{
    if (wm.xft_font != NULL) {
        XftFontClose(wm.display, wm.xft_font);
        wm.xft_font = NULL;
    }
    if (wm.font_set != NULL) {
        XFreeFontSet(wm.display, wm.font_set);
        wm.font_set = NULL;
    }
    if (wm.fallback_font != NULL) {
        XFreeFont(wm.display, wm.fallback_font);
        wm.fallback_font = NULL;
    }
    if (wm.draw_gc != NULL) {
        XFreeGC(wm.display, wm.draw_gc);
        wm.draw_gc = NULL;
    }
}

int draw_text_width(const char *text)
{
    int length = text_length(text);

    if (text == NULL || length == 0) {
        return 0;
    }
    if (wm.xft_font != NULL) {
        XGlyphInfo extents;

        XftTextExtentsUtf8(wm.display,
                           wm.xft_font,
                           (const FcChar8 *)text,
                           length,
                           &extents);
        return (int)extents.xOff;
    }
    if (wm.font_set != NULL) {
        return Xutf8TextEscapement(wm.font_set, text, length);
    }
    if (wm.fallback_font != NULL) {
        return XTextWidth(wm.fallback_font, text, length);
    }
    return length * 8;
}

int draw_text_baseline(int area_height)
{
    int text_height = wm.font_ascent + wm.font_descent;
    int padding = area_height > text_height ? (area_height - text_height) / 2 : 0;

    return padding + wm.font_ascent;
}

void draw_fill(Drawable drawable,
               unsigned long color,
               int x,
               int y,
               unsigned int width,
               unsigned int height)
{
    XSetForeground(wm.display, wm.draw_gc, color);
    XFillRectangle(wm.display, drawable, wm.draw_gc, x, y, width, height);
}

void draw_line(Drawable drawable,
               unsigned long color,
               int x1,
               int y1,
               int x2,
               int y2)
{
    XSetForeground(wm.display, wm.draw_gc, color);
    XDrawLine(wm.display, drawable, wm.draw_gc, x1, y1, x2, y2);
}

void draw_rectangle(Drawable drawable,
                    unsigned long color,
                    int x,
                    int y,
                    unsigned int width,
                    unsigned int height)
{
    XSetForeground(wm.display, wm.draw_gc, color);
    XDrawRectangle(wm.display, drawable, wm.draw_gc, x, y, width, height);
}

void draw_text_clipped(Drawable drawable,
                       unsigned long color,
                       int x,
                       int baseline,
                       int clip_x,
                       int clip_y,
                       unsigned int clip_width,
                       unsigned int clip_height,
                       const char *text)
{
    XRectangle clip;
    int length = text_length(text);

    if (text == NULL || length == 0 || clip_width == 0U || clip_height == 0U) {
        return;
    }

    clip.x = (short)clip_x;
    clip.y = (short)clip_y;
    clip.width = (unsigned short)clip_width;
    clip.height = (unsigned short)clip_height;

    if (wm.xft_font != NULL) {
        XftDraw *xft_draw;
        XColor queried;
        XftColor xft_color;

        xft_draw = XftDrawCreate(wm.display,
                                 drawable,
                                 DefaultVisual(wm.display, wm.screen),
                                 DefaultColormap(wm.display, wm.screen));
        if (xft_draw != NULL) {
            queried.pixel = color;
            if (XQueryColor(wm.display,
                            DefaultColormap(wm.display, wm.screen),
                            &queried) != 0) {
                xft_color.pixel = color;
                xft_color.color.red = queried.red;
                xft_color.color.green = queried.green;
                xft_color.color.blue = queried.blue;
                xft_color.color.alpha = 0xffffU;
                XftDrawSetClipRectangles(xft_draw, 0, 0, &clip, 1);
                XftDrawStringUtf8(xft_draw,
                                  &xft_color,
                                  wm.xft_font,
                                  x,
                                  baseline,
                                  (const FcChar8 *)text,
                                  length);
                XftDrawDestroy(xft_draw);
                return;
            }
            XftDrawDestroy(xft_draw);
        }
    }

    XSetClipRectangles(wm.display, wm.draw_gc, 0, 0, &clip, 1, Unsorted);
    XSetForeground(wm.display, wm.draw_gc, color);

    if (wm.font_set != NULL) {
        Xutf8DrawString(wm.display,
                        drawable,
                        wm.font_set,
                        wm.draw_gc,
                        x,
                        baseline,
                        text,
                        length);
    } else {
        XDrawString(wm.display,
                    drawable,
                    wm.draw_gc,
                    x,
                    baseline,
                    text,
                    length);
    }

    XSetClipMask(wm.display, wm.draw_gc, None);
}

static size_t previous_utf8_boundary(const char *text, size_t length)
{
    if (length == 0U) {
        return 0U;
    }

    --length;
    while (length > 0U &&
           ((unsigned char)text[length] & 0xc0U) == 0x80U) {
        --length;
    }
    return length;
}

void draw_text_ellipsized(Drawable drawable,
                          unsigned long color,
                          int x,
                          int baseline,
                          int clip_x,
                          int clip_y,
                          unsigned int clip_width,
                          unsigned int clip_height,
                          const char *text)
{
    char shortened[1024];
    const char *marker;
    size_t marker_length;
    size_t source_length;
    size_t prefix_length;
    int available;

    if (text == NULL || clip_width == 0U || clip_height == 0U) {
        return;
    }

    available = (int)clip_width - (x - clip_x);
    if (available <= 0) {
        return;
    }
    if (draw_text_width(text) <= available) {
        draw_text_clipped(drawable,
                          color,
                          x,
                          baseline,
                          clip_x,
                          clip_y,
                          clip_width,
                          clip_height,
                          text);
        return;
    }

    marker = wm.xft_font != NULL ? "…" : "...";
    marker_length = strlen(marker);
    source_length = strlen(text);
    prefix_length = source_length;
    if (prefix_length > sizeof(shortened) - marker_length - 1U) {
        prefix_length = sizeof(shortened) - marker_length - 1U;
        while (prefix_length > 0U &&
               ((unsigned char)text[prefix_length] & 0xc0U) == 0x80U) {
            --prefix_length;
        }
    }

    for (;;) {
        memcpy(shortened, text, prefix_length);
        memcpy(shortened + prefix_length, marker, marker_length + 1U);
        if (prefix_length == 0U || draw_text_width(shortened) <= available) {
            break;
        }
        prefix_length = previous_utf8_boundary(text, prefix_length);
    }

    draw_text_clipped(drawable,
                      color,
                      x,
                      baseline,
                      clip_x,
                      clip_y,
                      clip_width,
                      clip_height,
                      shortened);
}
