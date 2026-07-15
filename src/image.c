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

typedef struct sample_axis {
    int first;
    int last;
    uint64_t denominator;
    uint64_t interval_start;
    uint64_t interval_end;
    uint64_t source_step;
    int linear;
} sample_axis;

static int sample_axis_prepare(
    int source_size,
    int target_size,
    int target_position,
    sample_axis *axis
)
{
    int64_t position;
    int64_t denominator;
    if (
        axis == NULL || source_size < 1 || target_size < 1 ||
        target_position < 0 || target_position >= target_size
    ) return 0;
    memset(axis, 0, sizeof(*axis));
    if (target_size < source_size) {
        axis->interval_start = (uint64_t)target_position * (uint64_t)source_size;
        axis->interval_end = (uint64_t)(target_position + 1) * (uint64_t)source_size;
        axis->source_step = (uint64_t)target_size;
        axis->first = (int)(axis->interval_start / axis->source_step);
        axis->last = (int)((axis->interval_end - 1u) / axis->source_step);
        if (axis->last >= source_size) axis->last = source_size - 1;
        axis->denominator = (uint64_t)source_size;
        return 1;
    }

    /* Pixel-centre mapping for bilinear magnification. */
    denominator = (int64_t)target_size * 2;
    position = ((int64_t)target_position * 2 + 1) * (int64_t)source_size -
        (int64_t)target_size;
    axis->linear = 1;
    axis->denominator = (uint64_t)denominator;
    if (position <= 0) {
        axis->first = 0;
        axis->last = 0;
        return 1;
    }
    if (position >= (int64_t)(source_size - 1) * denominator) {
        axis->first = source_size - 1;
        axis->last = axis->first;
        return 1;
    }
    axis->first = (int)(position / denominator);
    axis->last = axis->first + 1;
    axis->interval_start = (uint64_t)(position - (int64_t)axis->first * denominator);
    return 1;
}

static uint64_t sample_axis_weight(const sample_axis *axis, int source_position)
{
    if (axis->first == axis->last) return axis->denominator;
    if (axis->linear != 0) {
        return source_position == axis->first
            ? axis->denominator - axis->interval_start
            : axis->interval_start;
    }
    {
        uint64_t source_start = (uint64_t)source_position * axis->source_step;
        uint64_t source_end = source_start + axis->source_step;
        uint64_t overlap_start = axis->interval_start > source_start
            ? axis->interval_start : source_start;
        uint64_t overlap_end = axis->interval_end < source_end
            ? axis->interval_end : source_end;
        return overlap_end > overlap_start ? overlap_end - overlap_start : 0u;
    }
}

static unsigned char normalized_channel(
    uint64_t total,
    uint64_t weight,
    unsigned int maximum
)
{
    uint64_t divisor = weight * (uint64_t)maximum;
    uint64_t value = (total * 255u + divisor / 2u) / divisor;
    return (unsigned char)(value > 255u ? 255u : value);
}

int msys_native_ppm_sample_resized(
    const msys_native_ppm *source,
    int width,
    int height,
    int x,
    int y,
    unsigned char output[3]
)
{
    sample_axis horizontal;
    sample_axis vertical;
    uint64_t totals[3] = {0u, 0u, 0u};
    uint64_t denominator;
    int source_x;
    int source_y;
    if (
        source == NULL || source->rgb == NULL || source->width < 1 ||
        source->height < 1 || source->maximum < 1 || output == NULL ||
        !sample_axis_prepare(source->width, width, x, &horizontal) ||
        !sample_axis_prepare(source->height, height, y, &vertical)
    ) return 0;
    denominator = horizontal.denominator * vertical.denominator;
    for (source_y = vertical.first; source_y <= vertical.last; source_y++) {
        uint64_t weight_y = sample_axis_weight(&vertical, source_y);
        for (source_x = horizontal.first; source_x <= horizontal.last; source_x++) {
            uint64_t weight = weight_y * sample_axis_weight(&horizontal, source_x);
            size_t offset = (
                (size_t)source_y * (size_t)source->width + (size_t)source_x
            ) * 3u;
            totals[0] += (uint64_t)source->rgb[offset] * weight;
            totals[1] += (uint64_t)source->rgb[offset + 1u] * weight;
            totals[2] += (uint64_t)source->rgb[offset + 2u] * weight;
        }
    }
    output[0] = normalized_channel(
        totals[0], denominator, (unsigned int)source->maximum
    );
    output[1] = normalized_channel(
        totals[1], denominator, (unsigned int)source->maximum
    );
    output[2] = normalized_channel(
        totals[2], denominator, (unsigned int)source->maximum
    );
    return 1;
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
        for (x = 0; x < width; x++) {
            unsigned char sample[3];
            unsigned long pixel;
            if (!msys_native_ppm_sample_resized(
                source, width, height, x, y, sample
            )) {
                XDestroyImage(target);
                return NULL;
            }
            pixel = channel_pixel(sample[0], visual->red_mask) |
                channel_pixel(sample[1], visual->green_mask) |
                channel_pixel(sample[2], visual->blue_mask);
            XPutPixel(target, x, y, pixel);
        }
    }
    return target;
}
