#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/image.h"

#include <X11/Xutil.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PPM_DIMENSION 2048
#define MAX_PPM_PIXELS (2048u * 2048u)

static int read_token(FILE *stream, char *output, size_t capacity)
{
    int value;
    size_t used = 0u;
    if (stream == NULL || output == NULL || capacity < 2u) return 0;
    for (;;) {
        value = fgetc(stream);
        if (value == EOF) return 0;
        if (isspace((unsigned char)value) != 0) continue;
        if (value == '#') {
            while (value != '\n' && value != EOF) value = fgetc(stream);
            continue;
        }
        break;
    }
    for (;;) {
        if (value == EOF || isspace((unsigned char)value) != 0 || value == '#') {
            if (value == '\r') {
                int next = fgetc(stream);
                if (next != '\n' && next != EOF) (void)ungetc(next, stream);
            } else if (value == '#') {
                while (value != '\n' && value != EOF) value = fgetc(stream);
            }
            break;
        }
        if (used + 1u >= capacity) return 0;
        output[used++] = (char)value;
        value = fgetc(stream);
    }
    output[used] = '\0';
    return used > 0u;
}

static int parse_positive(const char *text, int maximum, int *result)
{
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (
        errno != 0 || end == text || *end != '\0' || value < 1 ||
        value > maximum
    ) return 0;
    *result = (int)value;
    return 1;
}

void msys_native_ppm_free(msys_native_ppm *image)
{
    if (image == NULL) return;
    free(image->rgb);
    memset(image, 0, sizeof(*image));
}

int msys_native_ppm_load(
    const char *path,
    size_t maximum_bytes,
    msys_native_ppm *image
)
{
    FILE *stream = NULL;
    struct stat status;
    char token[32];
    size_t bytes;
    msys_native_ppm loaded;
    memset(&loaded, 0, sizeof(loaded));
    if (
        path == NULL || path[0] != '/' || image == NULL || maximum_bytes < 3u ||
        stat(path, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 8 ||
        (uintmax_t)status.st_size > (uintmax_t)maximum_bytes
    ) return 0;
    stream = fopen(path, "rb");
    if (stream == NULL) return 0;
    if (!read_token(stream, token, sizeof(token)) || strcmp(token, "P6") != 0 ||
        !read_token(stream, token, sizeof(token)) ||
        !parse_positive(token, MAX_PPM_DIMENSION, &loaded.width) ||
        !read_token(stream, token, sizeof(token)) ||
        !parse_positive(token, MAX_PPM_DIMENSION, &loaded.height) ||
        !read_token(stream, token, sizeof(token)) ||
        !parse_positive(token, 255, &loaded.maximum) ||
        (size_t)loaded.width > MAX_PPM_PIXELS / (size_t)loaded.height) {
        (void)fclose(stream);
        return 0;
    }
    bytes = (size_t)loaded.width * (size_t)loaded.height * 3u;
    if (bytes > maximum_bytes) {
        (void)fclose(stream);
        return 0;
    }
    loaded.rgb = (unsigned char *)malloc(bytes);
    if (loaded.rgb == NULL || fread(loaded.rgb, 1u, bytes, stream) != bytes) {
        free(loaded.rgb);
        (void)fclose(stream);
        return 0;
    }
    if (fclose(stream) != 0) {
        free(loaded.rgb);
        return 0;
    }
    *image = loaded;
    return 1;
}

static unsigned int mask_shift(unsigned long mask)
{
    unsigned int shift = 0u;
    if (mask == 0u) return 0u;
    while ((mask & 1u) == 0u) {
        shift++;
        mask >>= 1u;
    }
    return shift;
}

static unsigned long channel_pixel(unsigned int channel, unsigned long mask)
{
    unsigned int shift = mask_shift(mask);
    unsigned long maximum = mask >> shift;
    return (((unsigned long)channel * maximum + 127u) / 255u << shift) & mask;
}

XImage *msys_native_ppm_ximage(
    Display *display,
    Visual *visual,
    int depth,
    const msys_native_ppm *source,
    int width,
    int height
)
{
    XImage *target;
    size_t target_bytes;
    int x;
    int y;
    if (
        display == NULL || visual == NULL || source == NULL || source->rgb == NULL ||
        source->width < 1 || source->height < 1 || source->maximum < 1 ||
        width < 1 || height < 1 || width > MAX_PPM_DIMENSION || height > MAX_PPM_DIMENSION
    ) return NULL;
    target = XCreateImage(
        display, visual, (unsigned int)depth, ZPixmap, 0, NULL,
        (unsigned int)width, (unsigned int)height, 32, 0
    );
    if (target == NULL || target->bytes_per_line <= 0 ||
        (size_t)target->bytes_per_line > SIZE_MAX / (size_t)height) {
        if (target != NULL) XDestroyImage(target);
        return NULL;
    }
    target_bytes = (size_t)target->bytes_per_line * (size_t)height;
    target->data = (char *)calloc(1u, target_bytes);
    if (target->data == NULL) {
        XDestroyImage(target);
        return NULL;
    }
    for (y = 0; y < height; y++) {
        int source_y = y * source->height / height;
        for (x = 0; x < width; x++) {
            int source_x = x * source->width / width;
            size_t offset = ((size_t)source_y * (size_t)source->width + (size_t)source_x) * 3u;
            unsigned int red = (unsigned int)source->rgb[offset] * 255u /
                (unsigned int)source->maximum;
            unsigned int green = (unsigned int)source->rgb[offset + 1u] * 255u /
                (unsigned int)source->maximum;
            unsigned int blue = (unsigned int)source->rgb[offset + 2u] * 255u /
                (unsigned int)source->maximum;
            unsigned long pixel = channel_pixel(red, visual->red_mask) |
                channel_pixel(green, visual->green_mask) |
                channel_pixel(blue, visual->blue_mask);
            XPutPixel(target, x, y, pixel);
        }
    }
    return target;
}
