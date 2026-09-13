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

    wm.draw_gc = XCreateGC(wm.display, wm.root, 0UL, NULL);
    if (wm.draw_gc == NULL) {
        fprintf(stderr, "%s: no se pudo crear el contexto de dibujo\n", AWM_NAME);
        return false;
    }

    wm.font_set = XCreateFontSet(wm.display,
                                 AWM_FONT,
                                 &missing,
                                 &missing_count,
                                 &default_string);
    if (missing != NULL) {
        XFreeStringList(missing);
    }

    if (wm.font_set == NULL && strcmp(AWM_FONT, "fixed") != 0) {
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

    wm.fallback_font = XLoadQueryFont(wm.display, AWM_FONT);
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
            AWM_FONT);
    return true;
}

void draw_cleanup(void)
{
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
