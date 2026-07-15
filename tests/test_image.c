#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/image.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    static const unsigned char document[] = {
        'P', '6', '\n', '#', ' ', 't', 'e', 's', 't', '\n',
        '2', ' ', '1', '\n', '1', '2', '6', '\n',
        126, 0, 0, 0, 126, 0
    };
    char path[] = "/tmp/msys-native-image-XXXXXX";
    msys_native_ppm image;
    unsigned char sample[3];
    int descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, document, sizeof(document)) == (ssize_t)sizeof(document));
    CHECK(close(descriptor) == 0);
    memset(&image, 0, sizeof(image));
    CHECK(msys_native_ppm_load(path, 1024u, &image));
    CHECK(image.width == 2 && image.height == 1 && image.maximum == 126);
    CHECK(image.rgb[0] == 126 && image.rgb[4] == 126);
    CHECK(msys_native_ppm_sample_resized(&image, 1, 1, 0, 0, sample));
    CHECK(sample[0] >= 127 && sample[0] <= 128);
    CHECK(sample[1] >= 127 && sample[1] <= 128);
    CHECK(sample[2] == 0);
    CHECK(msys_native_ppm_sample_resized(&image, 4, 1, 1, 0, sample));
    CHECK(sample[0] > sample[1] && sample[0] < 255 && sample[1] > 0);
    msys_native_ppm_free(&image);
    CHECK(unlink(path) == 0);
    puts("native shell image tests passed");
    return 0;
}
