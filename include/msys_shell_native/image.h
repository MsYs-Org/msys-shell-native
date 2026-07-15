#ifndef MSYS_SHELL_NATIVE_IMAGE_H
#define MSYS_SHELL_NATIVE_IMAGE_H

#include <X11/Xlib.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct msys_native_ppm {
    unsigned char *rgb;
    int width;
    int height;
    int maximum;
} msys_native_ppm;

/* P6-only by design: launcher icons and WM thumbnails use this tiny format. */
int msys_native_ppm_load(
    const char *path,
    size_t maximum_bytes,
    msys_native_ppm *image
);

void msys_native_ppm_free(msys_native_ppm *image);

/*
 * Sample one resized pixel.  Minification uses an exact box average to avoid
 * screenshot aliasing; magnification uses bilinear interpolation.  The
 * implementation needs no full-size intermediate image.
 */
int msys_native_ppm_sample_resized(
    const msys_native_ppm *source,
    int width,
    int height,
    int x,
    int y,
    unsigned char output[3]
);

/* Antialiased conversion into the target server's native TrueColor image. */
XImage *msys_native_ppm_ximage(
    Display *display,
    Visual *visual,
    int depth,
    const msys_native_ppm *source,
    int width,
    int height
);

#ifdef __cplusplus
}
#endif

#endif
