#define _POSIX_C_SOURCE 200809L

#include <msys/i18n.h>
#include "shell_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    char locale[MSYS_I18N_LOCALE_CAPACITY];
    const char *message;
    CHECK(setenv("MSYS_LOCALE", "zh_CN.UTF-8", 1) == 0);
    CHECK(msys_i18n_locale_from_environment(locale, sizeof(locale)) == MSYS_I18N_OK);
    CHECK(strcmp(locale, "zh-CN") == 0);
    message = msys_i18n_lookup(&shell_catalog, locale, "launcher.title");
    CHECK(message != NULL && strcmp(message, "\xe5\xba\x94\xe7\x94\xa8") == 0);
    message = msys_i18n_lookup(&shell_catalog, "zh-Hans-CN", "controls.title");
    CHECK(message != NULL && strcmp(message, "\xe5\xbf\xab\xe6\x8d\xb7\xe6\x8e\xa7\xe5\x88\xb6") == 0);
    message = msys_i18n_lookup(&shell_catalog, "zh-Hans-CN", "controls.audio_muted");
    CHECK(message != NULL && strcmp(message, "\xe5\xb7\xb2\xe9\x9d\x99\xe9\x9f\xb3") == 0);
    message = msys_i18n_lookup(&shell_catalog, "zh-Hans-CN", "process.title");
    CHECK(message != NULL && strcmp(
        message,
        "\xe5\x90\x8e\xe5\x8f\xb0\xe8\xbf\x9b\xe7\xa8\x8b"
    ) == 0);
    message = msys_i18n_lookup(
        &shell_catalog, "zh-Hans-CN", "metrics.memory_short"
    );
    CHECK(message != NULL && strcmp(message, "MEM") == 0);
    message = msys_i18n_lookup(
        &shell_catalog, "zh-Hans-CN", "process.lifecycle_on_demand"
    );
    CHECK(message != NULL && strcmp(
        message, "\xe6\x8c\x89\xe9\x9c\x80"
    ) == 0);
    CHECK(strcmp(msys_i18n_lookup(&shell_catalog, "C", "launcher.title"), "Apps") == 0);
    puts("native shell i18n tests passed");
    return 0;
}
