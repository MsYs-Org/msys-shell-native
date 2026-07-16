#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int channel(unsigned long pixel, unsigned long mask)
{
    unsigned int shift = 0u;
    unsigned long maximum;
    if(mask == 0u) return 0u;
    while((mask & 1u) == 0u) {
        mask >>= 1u;
        shift++;
    }
    maximum = mask;
    return (unsigned int)((((pixel >> shift) & maximum) * 255u +
                           maximum / 2u) / maximum);
}

int main(int argc, char **argv)
{
    Display *display;
    XWindowAttributes attributes;
    XImage *image;
    Window window;
    char *end = NULL;
    unsigned long parsed;
    unsigned long required;
    unsigned long matched = 0u;
    int x;
    int y;
    if(argc != 3) {
        fprintf(stderr, "usage: %s WINDOW MIN_GREEN_PIXELS\n", argv[0]);
        return 2;
    }
    errno = 0;
    parsed = strtoul(argv[1], &end, 0);
    if(errno != 0 || end == argv[1] || *end != '\0') return 2;
    window = (Window)parsed;
    errno = 0;
    required = strtoul(argv[2], &end, 10);
    if(errno != 0 || end == argv[2] || *end != '\0' || required == 0u) return 2;
    display = XOpenDisplay(NULL);
    if(display == NULL) {
        fputs("x11-pixel-probe: cannot open display\n", stderr);
        return 1;
    }
    if(XGetWindowAttributes(display, window, &attributes) == 0 ||
       attributes.width < 1 || attributes.height < 1) {
        fputs("x11-pixel-probe: cannot read window geometry\n", stderr);
        XCloseDisplay(display);
        return 1;
    }
    image = XGetImage(display, window, 0, 0,
                      (unsigned int)attributes.width,
                      (unsigned int)attributes.height,
                      AllPlanes, ZPixmap);
    if(image == NULL) {
        fputs("x11-pixel-probe: XGetImage failed\n", stderr);
        XCloseDisplay(display);
        return 1;
    }
    for(y = 0; y < attributes.height; y++) {
        for(x = 0; x < attributes.width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            unsigned int red = channel(pixel, image->red_mask);
            unsigned int green = channel(pixel, image->green_mask);
            unsigned int blue = channel(pixel, image->blue_mask);
            if(red <= 24u && green >= 224u && blue <= 24u) matched++;
        }
    }
    XDestroyImage(image);
    XCloseDisplay(display);
    if(matched < required) {
        fprintf(stderr, "x11-pixel-probe: green pixels %lu < %lu\n",
                matched, required);
        return 1;
    }
    printf("x11-pixel-probe: green pixels %lu\n", matched);
    return 0;
}
