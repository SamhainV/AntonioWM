#include "wm.h"

#include <X11/Xutil.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PNG_MAX_CHUNK_SIZE (32U * 1024U * 1024U)
#define PNG_MAX_DIMENSION 2048U
#define PNG_MAX_PIXELS (4U * 1024U * 1024U)

typedef struct {
    unsigned int width;
    unsigned int height;
    unsigned int bit_depth;
    unsigned int color_type;
    unsigned int interlace;
    unsigned char *palette;
    size_t palette_length;
    unsigned char *transparency;
    size_t transparency_length;
    unsigned char *compressed;
    size_t compressed_length;
    bool has_header;
    bool has_end;
} PngData;

static uint32_t read_u32(const unsigned char bytes[4])
{
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static unsigned int read_u16(const unsigned char bytes[2])
{
    return ((unsigned int)bytes[0] << 8U) | (unsigned int)bytes[1];
}

static bool multiply_size(size_t first, size_t second, size_t *result)
{
    if (first != 0U && second > SIZE_MAX / first) {
        return false;
    }
    *result = first * second;
    return true;
}

static bool append_bytes(unsigned char **destination,
                         size_t *destination_length,
                         const unsigned char *source,
                         size_t source_length)
{
    unsigned char *resized;
    size_t total;

    if (source_length > PNG_MAX_CHUNK_SIZE ||
        *destination_length > PNG_MAX_CHUNK_SIZE - source_length) {
        return false;
    }
    total = *destination_length + source_length;
    resized = realloc(*destination, total > 0U ? total : 1U);
    if (resized == NULL) {
        return false;
    }
    if (source_length > 0U) {
        memcpy(resized + *destination_length, source, source_length);
    }
    *destination = resized;
    *destination_length = total;
    return true;
}

static void png_data_free(PngData *png)
{
    free(png->palette);
    free(png->transparency);
    free(png->compressed);
    memset(png, 0, sizeof(*png));
}

static bool replace_chunk(unsigned char **destination,
                          size_t *destination_length,
                          const unsigned char *source,
                          size_t source_length)
{
    unsigned char *copy = NULL;

    if (source_length > 0U) {
        copy = malloc(source_length);
        if (copy == NULL) {
            return false;
        }
        memcpy(copy, source, source_length);
    }
    free(*destination);
    *destination = copy;
    *destination_length = source_length;
    return true;
}

static bool parse_header(PngData *png,
                         const unsigned char *chunk,
                         size_t length)
{
    uint32_t width;
    uint32_t height;

    if (length != 13U || png->has_header) {
        return false;
    }
    width = read_u32(chunk);
    height = read_u32(chunk + 4U);
    if (width == 0U || height == 0U || width > PNG_MAX_DIMENSION ||
        height > PNG_MAX_DIMENSION ||
        (uint64_t)width * (uint64_t)height > PNG_MAX_PIXELS ||
        chunk[10] != 0U || chunk[11] != 0U) {
        return false;
    }
    png->width = width;
    png->height = height;
    png->bit_depth = chunk[8];
    png->color_type = chunk[9];
    png->interlace = chunk[12];
    png->has_header = true;
    return true;
}

static bool read_png(FILE *file, PngData *png)
{
    static const unsigned char signature[8] = {
        137U, 80U, 78U, 71U, 13U, 10U, 26U, 10U
    };
    unsigned char file_signature[8];

    if (fread(file_signature, 1U, sizeof(file_signature), file) !=
            sizeof(file_signature) ||
        memcmp(file_signature, signature, sizeof(signature)) != 0) {
        return false;
    }

    while (!png->has_end) {
        unsigned char length_bytes[4];
        unsigned char type[4];
        unsigned char crc_bytes[4];
        unsigned char *chunk = NULL;
        uint32_t length;
        uLong computed_crc;
        bool recognized = true;
        bool valid = true;

        if (fread(length_bytes, 1U, sizeof(length_bytes), file) !=
                sizeof(length_bytes) ||
            fread(type, 1U, sizeof(type), file) != sizeof(type)) {
            return false;
        }
        length = read_u32(length_bytes);
        if (length > PNG_MAX_CHUNK_SIZE) {
            return false;
        }
        if (length > 0U) {
            chunk = malloc((size_t)length);
            if (chunk == NULL ||
                fread(chunk, 1U, (size_t)length, file) != (size_t)length) {
                free(chunk);
                return false;
            }
        }
        if (fread(crc_bytes, 1U, sizeof(crc_bytes), file) !=
            sizeof(crc_bytes)) {
            free(chunk);
            return false;
        }
        computed_crc = crc32(0L, Z_NULL, 0);
        computed_crc = crc32(computed_crc, type, (uInt)sizeof(type));
        if (length > 0U) {
            computed_crc = crc32(computed_crc, chunk, (uInt)length);
        }
        if ((uint32_t)computed_crc != read_u32(crc_bytes)) {
            free(chunk);
            return false;
        }

        if (memcmp(type, "IHDR", 4U) == 0) {
            valid = parse_header(png, chunk, (size_t)length);
        } else if (memcmp(type, "PLTE", 4U) == 0) {
            valid = png->has_header && length >= 3U && length <= 768U &&
                    length % 3U == 0U &&
                    replace_chunk(&png->palette,
                                  &png->palette_length,
                                  chunk,
                                  (size_t)length);
        } else if (memcmp(type, "tRNS", 4U) == 0) {
            valid = png->has_header && length <= 256U &&
                    replace_chunk(&png->transparency,
                                  &png->transparency_length,
                                  chunk,
                                  (size_t)length);
        } else if (memcmp(type, "IDAT", 4U) == 0) {
            valid = png->has_header &&
                    append_bytes(&png->compressed,
                                 &png->compressed_length,
                                 chunk,
                                 (size_t)length);
        } else if (memcmp(type, "IEND", 4U) == 0) {
            valid = length == 0U && png->has_header;
            png->has_end = valid;
        } else {
            recognized = false;
        }
        free(chunk);
        if (!valid || (!recognized && (type[0] & 0x20U) == 0U)) {
            return false;
        }
    }
    return png->has_header && png->compressed_length > 0U;
}

static bool color_format(const PngData *png,
                         unsigned int *channels,
                         unsigned int *bits_per_pixel)
{
    switch (png->color_type) {
    case 0U:
        *channels = 1U;
        if (png->bit_depth != 1U && png->bit_depth != 2U &&
            png->bit_depth != 4U && png->bit_depth != 8U &&
            png->bit_depth != 16U) {
            return false;
        }
        break;
    case 2U:
        *channels = 3U;
        if (png->bit_depth != 8U && png->bit_depth != 16U) {
            return false;
        }
        break;
    case 3U:
        *channels = 1U;
        if ((png->bit_depth != 1U && png->bit_depth != 2U &&
             png->bit_depth != 4U && png->bit_depth != 8U) ||
            png->palette == NULL || png->palette_length == 0U) {
            return false;
        }
        break;
    case 4U:
        *channels = 2U;
        if (png->bit_depth != 8U && png->bit_depth != 16U) {
            return false;
        }
        break;
    case 6U:
        *channels = 4U;
        if (png->bit_depth != 8U && png->bit_depth != 16U) {
            return false;
        }
        break;
    default:
        return false;
    }
    *bits_per_pixel = *channels * png->bit_depth;
    return png->interlace == 0U;
}

static unsigned char paeth_predictor(unsigned char left,
                                     unsigned char above,
                                     unsigned char upper_left)
{
    int prediction = (int)left + (int)above - (int)upper_left;
    int left_distance = abs(prediction - (int)left);
    int above_distance = abs(prediction - (int)above);
    int corner_distance = abs(prediction - (int)upper_left);

    if (left_distance <= above_distance && left_distance <= corner_distance) {
        return left;
    }
    if (above_distance <= corner_distance) {
        return above;
    }
    return upper_left;
}

static bool unfilter_rows(const PngData *png,
                          unsigned int bits_per_pixel,
                          const unsigned char *filtered,
                          size_t row_bytes,
                          unsigned char *pixels)
{
    size_t filter_bytes = (bits_per_pixel + 7U) / 8U;
    unsigned int row;

    if (filter_bytes == 0U) {
        filter_bytes = 1U;
    }
    for (row = 0U; row < png->height; ++row) {
        const unsigned char *source = filtered +
                                      (size_t)row * (row_bytes + 1U) + 1U;
        unsigned char filter = source[-1];
        unsigned char *destination = pixels + (size_t)row * row_bytes;
        const unsigned char *previous = row > 0U ? destination - row_bytes : NULL;
        size_t column;

        if (filter > 4U) {
            return false;
        }
        for (column = 0U; column < row_bytes; ++column) {
            unsigned char left = column >= filter_bytes
                                     ? destination[column - filter_bytes]
                                     : 0U;
            unsigned char above = previous != NULL ? previous[column] : 0U;
            unsigned char upper_left = previous != NULL && column >= filter_bytes
                                           ? previous[column - filter_bytes]
                                           : 0U;
            unsigned int value = source[column];

            if (filter == 1U) {
                value += left;
            } else if (filter == 2U) {
                value += above;
            } else if (filter == 3U) {
                value += ((unsigned int)left + (unsigned int)above) / 2U;
            } else if (filter == 4U) {
                value += paeth_predictor(left, above, upper_left);
            }
            destination[column] = (unsigned char)(value & 0xffU);
        }
    }
    return true;
}

static unsigned int packed_sample(const unsigned char *row,
                                  unsigned int pixel,
                                  unsigned int bit_depth)
{
    unsigned int bit_offset = pixel * bit_depth;
    unsigned int shift = 8U - bit_depth - bit_offset % 8U;
    unsigned int mask = (1U << bit_depth) - 1U;

    return ((unsigned int)row[bit_offset / 8U] >> shift) & mask;
}

static unsigned char scale_sample(unsigned int sample, unsigned int bit_depth)
{
    unsigned int maximum = bit_depth == 16U ? 65535U :
                           (1U << bit_depth) - 1U;

    return (unsigned char)((sample * 255U + maximum / 2U) / maximum);
}

static void grayscale_pixel(const PngData *png,
                            const unsigned char *row,
                            unsigned int x,
                            unsigned char rgba[4])
{
    unsigned int sample;
    unsigned int transparent = 65536U;

    if (png->bit_depth == 16U) {
        sample = read_u16(row + (size_t)x * 2U);
    } else if (png->bit_depth == 8U) {
        sample = row[x];
    } else {
        sample = packed_sample(row, x, png->bit_depth);
    }
    if (png->transparency_length >= 2U) {
        transparent = read_u16(png->transparency);
    }
    rgba[0] = scale_sample(sample, png->bit_depth);
    rgba[1] = rgba[0];
    rgba[2] = rgba[0];
    rgba[3] = sample == transparent ? 0U : 255U;
}

static void truecolor_pixel(const PngData *png,
                            const unsigned char *row,
                            unsigned int x,
                            unsigned char rgba[4])
{
    unsigned int bytes_per_sample = png->bit_depth / 8U;
    const unsigned char *source = row +
                                  (size_t)x * 3U * bytes_per_sample;
    unsigned int red;
    unsigned int green;
    unsigned int blue;

    if (png->bit_depth == 16U) {
        red = read_u16(source);
        green = read_u16(source + 2U);
        blue = read_u16(source + 4U);
    } else {
        red = source[0];
        green = source[1];
        blue = source[2];
    }
    rgba[0] = scale_sample(red, png->bit_depth);
    rgba[1] = scale_sample(green, png->bit_depth);
    rgba[2] = scale_sample(blue, png->bit_depth);
    rgba[3] = 255U;
    if (png->transparency_length >= 6U &&
        red == read_u16(png->transparency) &&
        green == read_u16(png->transparency + 2U) &&
        blue == read_u16(png->transparency + 4U)) {
        rgba[3] = 0U;
    }
}

static void palette_pixel(const PngData *png,
                          const unsigned char *row,
                          unsigned int x,
                          unsigned char rgba[4])
{
    unsigned int index = png->bit_depth == 8U
                             ? (unsigned int)row[x]
                             : packed_sample(row, x, png->bit_depth);
    size_t offset = (size_t)index * 3U;

    if (offset + 2U >= png->palette_length) {
        rgba[0] = 0U;
        rgba[1] = 0U;
        rgba[2] = 0U;
        rgba[3] = 0U;
        return;
    }
    rgba[0] = png->palette[offset];
    rgba[1] = png->palette[offset + 1U];
    rgba[2] = png->palette[offset + 2U];
    rgba[3] = index < png->transparency_length
                  ? png->transparency[index]
                  : 255U;
}

static void alpha_pixel(const PngData *png,
                        const unsigned char *row,
                        unsigned int x,
                        unsigned char rgba[4])
{
    unsigned int channels = png->color_type == 4U ? 2U : 4U;
    unsigned int bytes_per_sample = png->bit_depth / 8U;
    const unsigned char *source = row +
                                  (size_t)x * channels * bytes_per_sample;

    if (png->color_type == 4U) {
        rgba[0] = source[0];
        rgba[1] = source[0];
        rgba[2] = source[0];
        rgba[3] = source[bytes_per_sample];
    } else {
        rgba[0] = source[0U * bytes_per_sample];
        rgba[1] = source[1U * bytes_per_sample];
        rgba[2] = source[2U * bytes_per_sample];
        rgba[3] = source[3U * bytes_per_sample];
    }
}

static bool convert_rgba(const PngData *png,
                         const unsigned char *pixels,
                         size_t row_bytes,
                         IconImage *image)
{
    size_t pixel_count;
    size_t rgba_size;
    unsigned int y;

    if (!multiply_size((size_t)png->width,
                       (size_t)png->height,
                       &pixel_count) ||
        !multiply_size(pixel_count, 4U, &rgba_size)) {
        return false;
    }
    image->rgba = malloc(rgba_size);
    if (image->rgba == NULL) {
        return false;
    }
    image->width = png->width;
    image->height = png->height;

    for (y = 0U; y < png->height; ++y) {
        const unsigned char *row = pixels + (size_t)y * row_bytes;
        unsigned int x;

        for (x = 0U; x < png->width; ++x) {
            unsigned char *rgba = image->rgba +
                                  ((size_t)y * png->width + x) * 4U;

            if (png->color_type == 0U) {
                grayscale_pixel(png, row, x, rgba);
            } else if (png->color_type == 2U) {
                truecolor_pixel(png, row, x, rgba);
            } else if (png->color_type == 3U) {
                palette_pixel(png, row, x, rgba);
            } else {
                alpha_pixel(png, row, x, rgba);
            }
        }
    }
    return true;
}

bool icon_load_png(const char *path, IconImage *image)
{
    PngData png;
    FILE *file;
    unsigned int channels;
    unsigned int bits_per_pixel;
    size_t row_bits;
    size_t row_bytes;
    size_t filtered_row_bytes;
    size_t filtered_size;
    size_t pixel_size;
    unsigned char *filtered = NULL;
    unsigned char *pixels = NULL;
    uLongf destination_length;
    bool parsed;
    bool closed;
    bool success = false;

    if (path == NULL || path[0] == '\0' || image == NULL) {
        return false;
    }
    memset(image, 0, sizeof(*image));
    memset(&png, 0, sizeof(png));
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    parsed = read_png(file, &png);
    closed = fclose(file) == 0;
    if (!parsed || !closed ||
        !color_format(&png, &channels, &bits_per_pixel)) {
        png_data_free(&png);
        return false;
    }
    (void)channels;

    if (!multiply_size((size_t)png.width,
                       (size_t)bits_per_pixel,
                       &row_bits)) {
        goto cleanup;
    }
    row_bytes = (row_bits + 7U) / 8U;
    filtered_row_bytes = row_bytes + 1U;
    if (!multiply_size(filtered_row_bytes,
                       (size_t)png.height,
                       &filtered_size) ||
        !multiply_size(row_bytes, (size_t)png.height, &pixel_size) ||
        filtered_size > (size_t)ULONG_MAX ||
        png.compressed_length > (size_t)ULONG_MAX) {
        goto cleanup;
    }
    filtered = malloc(filtered_size);
    pixels = malloc(pixel_size);
    if (filtered == NULL || pixels == NULL) {
        goto cleanup;
    }
    destination_length = (uLongf)filtered_size;
    if (uncompress(filtered,
                   &destination_length,
                   png.compressed,
                   (uLong)png.compressed_length) != Z_OK ||
        destination_length != (uLongf)filtered_size ||
        !unfilter_rows(&png,
                       bits_per_pixel,
                       filtered,
                       row_bytes,
                       pixels) ||
        !convert_rgba(&png, pixels, row_bytes, image)) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(filtered);
    free(pixels);
    png_data_free(&png);
    if (!success) {
        icon_free(image);
    }
    return success;
}

void icon_free(IconImage *image)
{
    if (image == NULL) {
        return;
    }
    free(image->rgba);
    memset(image, 0, sizeof(*image));
}

static unsigned int mask_shift(unsigned long mask)
{
    unsigned int shift = 0U;

    while (mask != 0UL && (mask & 1UL) == 0UL) {
        mask >>= 1U;
        ++shift;
    }
    return shift;
}

static unsigned int pixel_component(unsigned long pixel, unsigned long mask)
{
    unsigned int shift;
    unsigned long maximum;

    if (mask == 0UL) {
        return 0U;
    }
    shift = mask_shift(mask);
    maximum = mask >> shift;
    return (unsigned int)((((pixel & mask) >> shift) * 255UL + maximum / 2UL) /
                          maximum);
}

static unsigned long pack_component(unsigned int component, unsigned long mask)
{
    unsigned int shift;
    unsigned long maximum;

    if (mask == 0UL) {
        return 0UL;
    }
    shift = mask_shift(mask);
    maximum = mask >> shift;
    return (((unsigned long)component * maximum + 127UL) / 255UL << shift) &
           mask;
}

bool icon_draw(Drawable drawable,
               const IconImage *image,
               int x,
               int y,
               unsigned int width,
               unsigned int height,
               unsigned long background)
{
    Visual *visual;
    XImage *ximage;
    size_t data_size;
    unsigned int background_red;
    unsigned int background_green;
    unsigned int background_blue;
    unsigned int destination_y;

    if (wm.display == NULL || drawable == None || image == NULL ||
        image->rgba == NULL || image->width == 0U || image->height == 0U ||
        width == 0U || height == 0U) {
        return false;
    }
    visual = DefaultVisual(wm.display, wm.screen);
    if (visual == NULL || visual->class != TrueColor) {
        return false;
    }
    ximage = XCreateImage(wm.display,
                          visual,
                          (unsigned int)DefaultDepth(wm.display, wm.screen),
                          ZPixmap,
                          0,
                          NULL,
                          width,
                          height,
                          32,
                          0);
    if (ximage == NULL ||
        !multiply_size((size_t)ximage->bytes_per_line,
                       (size_t)height,
                       &data_size)) {
        if (ximage != NULL) {
            XDestroyImage(ximage);
        }
        return false;
    }
    ximage->data = calloc(data_size, 1U);
    if (ximage->data == NULL) {
        XDestroyImage(ximage);
        return false;
    }

    background_red = pixel_component(background, visual->red_mask);
    background_green = pixel_component(background, visual->green_mask);
    background_blue = pixel_component(background, visual->blue_mask);
    for (destination_y = 0U; destination_y < height; ++destination_y) {
        unsigned int source_y = (unsigned int)(((uint64_t)destination_y *
                                                image->height) /
                                               height);
        unsigned int destination_x;

        for (destination_x = 0U; destination_x < width; ++destination_x) {
            unsigned int source_x = (unsigned int)(((uint64_t)destination_x *
                                                    image->width) /
                                                   width);
            const unsigned char *rgba = image->rgba +
                                        ((size_t)source_y * image->width +
                                         source_x) * 4U;
            unsigned int alpha = rgba[3];
            unsigned int red = ((unsigned int)rgba[0] * alpha +
                                background_red * (255U - alpha) + 127U) /
                               255U;
            unsigned int green = ((unsigned int)rgba[1] * alpha +
                                  background_green * (255U - alpha) + 127U) /
                                 255U;
            unsigned int blue = ((unsigned int)rgba[2] * alpha +
                                 background_blue * (255U - alpha) + 127U) /
                                255U;
            unsigned long pixel = pack_component(red, visual->red_mask) |
                                  pack_component(green, visual->green_mask) |
                                  pack_component(blue, visual->blue_mask);

            XPutPixel(ximage, (int)destination_x, (int)destination_y, pixel);
        }
    }
    XPutImage(wm.display,
              drawable,
              wm.draw_gc,
              ximage,
              0,
              0,
              x,
              y,
              width,
              height);
    XDestroyImage(ximage);
    /* XPutImage queues an asynchronous X request and has no success value. */
    return true;
}

/* Xlib represents format-32 properties as native unsigned longs, even on LP64.
 * Bound the entire request as well as each image before doing arithmetic. */
static bool icon_load_net(Window window, unsigned int size, IconImage *image)
{
    Atom type;
    int format;
    unsigned long count = 0UL, after = 0UL;
    unsigned char *data = NULL;
    unsigned long *words;
    size_t offset = 0U, best = 0U, best_pixels = 0U;
    unsigned long best_score = ULONG_MAX;
    unsigned int width = 0U, height = 0U;
    const long limit = 1024L * 1024L;

    icon_free(image);
    if (XGetWindowProperty(wm.display, window, wm.atoms.net_wm_icon,
                           0L, limit, False, XA_CARDINAL, &type, &format,
                           &count, &after, &data) != Success ||
        type != XA_CARDINAL || format != 32 || after != 0UL || data == NULL) {
        if (data != NULL) XFree(data);
        return false;
    }
    words = (unsigned long *)data;
    while (offset + 2U <= count) {
        unsigned long w = words[offset++], h = words[offset++];
        size_t pixels;
        unsigned long score;
        if (w == 0UL || h == 0UL || w > 2048UL || h > 2048UL ||
            w > (count - offset) / h) break;
        pixels = (size_t)(w * h);
        /* Prefer the smallest image meeting both requested dimensions. */
        score = w >= size && h >= size ? w + h : 8192UL - w - h;
        if (score < best_score) {
            best_score = score;
            best = offset;
            best_pixels = pixels;
            width = (unsigned int)w;
            height = (unsigned int)h;
        }
        offset += pixels;
    }
    if (best_pixels != 0U) {
        image->rgba = malloc(best_pixels * 4U);
        if (image->rgba != NULL) {
            size_t index;
            image->width = width;
            image->height = height;
            for (index = 0U; index < best_pixels; ++index) {
                uint32_t argb = (uint32_t)words[best + index];
                image->rgba[index * 4U] = (unsigned char)(argb >> 16U);
                image->rgba[index * 4U + 1U] = (unsigned char)(argb >> 8U);
                image->rgba[index * 4U + 2U] = (unsigned char)argb;
                image->rgba[index * 4U + 3U] = (unsigned char)(argb >> 24U);
            }
        }
    }
    XFree(data);
    return image->rgba != NULL;
}

/* A client owns these pixmaps and may free them at any time. Trap only this
 * synchronous read, after draining preceding WM requests with its usual handler. */
static bool hints_read_failed;

static int hints_read_error(Display *display, XErrorEvent *error)
{
    (void)display;
    (void)error;
    hints_read_failed = true;
    return 0;
}

static bool icon_load_hints(Window window, IconImage *image)
{
    XWMHints *hints = XGetWMHints(wm.display, window);
    XImage *pixels = NULL, *mask = NULL;
    Window root;
    int x, y;
    unsigned int width, height, border, depth;
    int (*previous)(Display *, XErrorEvent *);
    Visual *visual;
    bool success = false;

    if (hints == NULL) return false;
    visual = DefaultVisual(wm.display, wm.screen);
    if ((hints->flags & IconPixmapHint) == 0 || hints->icon_pixmap == None) {
        XFree(hints);
        return false;
    }
    XSync(wm.display, False);
    hints_read_failed = false;
    previous = XSetErrorHandler(hints_read_error);
    if (!XGetGeometry(wm.display, hints->icon_pixmap, &root, &x, &y,
                      &width, &height, &border, &depth) || hints_read_failed ||
        width == 0U || height == 0U || width > 2048U || height > 2048U ||
        (size_t)width * height > 1024U * 1024U ||
        (depth != 1U && (depth != (unsigned int)DefaultDepth(wm.display, wm.screen) ||
                        visual->class != TrueColor))) goto done;
    pixels = XGetImage(wm.display, hints->icon_pixmap, 0, 0,
                       width, height, AllPlanes, ZPixmap);
    if (pixels == NULL || hints_read_failed) goto done;
    if ((hints->flags & IconMaskHint) != 0 && hints->icon_mask != None) {
        unsigned int mw, mh, md;
        if (!XGetGeometry(wm.display, hints->icon_mask, &root, &x, &y,
                          &mw, &mh, &border, &md) || hints_read_failed ||
            mw < width || mh < height || md != 1U) goto done;
        mask = XGetImage(wm.display, hints->icon_mask, 0, 0,
                         width, height, 1UL, ZPixmap);
        if (mask == NULL || hints_read_failed) goto done;
    }
    image->rgba = malloc((size_t)width * height * 4U);
    if (image->rgba == NULL) goto done;
    image->width = width;
    image->height = height;
    for (unsigned int row = 0U; row < height; ++row) {
        for (unsigned int column = 0U; column < width; ++column) {
            unsigned long pixel = XGetPixel(pixels, (int)column, (int)row);
            unsigned char *rgba = image->rgba + ((size_t)row * width + column) * 4U;
            rgba[0] = (unsigned char)(depth == 1U ? (pixel ? 255U : 0U) :
                                     pixel_component(pixel, visual->red_mask));
            rgba[1] = (unsigned char)(depth == 1U ? rgba[0] :
                                     pixel_component(pixel, visual->green_mask));
            rgba[2] = (unsigned char)(depth == 1U ? rgba[0] :
                                     pixel_component(pixel, visual->blue_mask));
            rgba[3] = mask == NULL || XGetPixel(mask, (int)column, (int)row) ? 255U : 0U;
        }
    }
    success = true;
done:
    if (pixels != NULL) XDestroyImage(pixels);
    if (mask != NULL) XDestroyImage(mask);
    XSync(wm.display, False);
    XSetErrorHandler(previous);
    XFree(hints);
    if (!success) icon_free(image);
    return success;
}

bool icon_load_client(Window window, unsigned int size, IconImage *image)
{
    return icon_load_net(window, size, image) || icon_load_hints(window, image);
}
