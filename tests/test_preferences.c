#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    msys_native_preferences state;
    msys_native_preferences changed;
    msys_native_preferences loaded;
    char root[] = "/tmp/msys-shell-preferences-XXXXXX";
    char path[MSYS_NATIVE_PREFERENCES_PATH_CAPACITY];
    char json[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
    CHECK(mkdtemp(root) != NULL);
    CHECK(setenv("MSYS_COMPONENT_STATE_DIR", root, 1) == 0);
    msys_native_preferences_defaults(&state);
    CHECK(msys_native_preferences_empty_request(" { } ") != 0);
    CHECK(msys_native_preferences_empty_request("{\"extra\":1}") == 0);
    CHECK(msys_native_preferences_merge(
        "{\"layout\":\"embedded\"}", &state, &changed
    ) == MSYS_NATIVE_PREFERENCES_OK);
    CHECK(msys_native_preferences_merge(
        "{\"preferences\":{\"layout\":\"desktop\",\"accent_color\":\"#123ABC\",\"icon_size\":72,\"show_labels\":false}}",
        &state,
        &changed
    ) == MSYS_NATIVE_PREFERENCES_OK);
    CHECK(strcmp(changed.layout, "desktop") == 0);
    CHECK(changed.icon_size == 72 && changed.show_labels == 0);
    CHECK(msys_native_preferences_merge(
        "{\"icon_size\":39}", &state, &changed
    ) == MSYS_NATIVE_PREFERENCES_BAD_VALUE);
    CHECK(msys_native_preferences_merge(
        "{\"layout\":\"mobile\",\"layout\":\"desktop\"}", &state, &changed
    ) == MSYS_NATIVE_PREFERENCES_BAD_REQUEST);
    changed.revision = 7u;
    CHECK(msys_native_preferences_load(&loaded, path, sizeof(path)) == MSYS_NATIVE_PREFERENCES_OK);
    CHECK(loaded.revision == 0u);
    CHECK(msys_native_preferences_commit(path, &changed) == MSYS_NATIVE_PREFERENCES_OK);
    CHECK(msys_native_preferences_load(&loaded, path, sizeof(path)) == MSYS_NATIVE_PREFERENCES_OK);
    CHECK(loaded.revision == 7u && strcmp(loaded.layout, "desktop") == 0);
    CHECK(msys_native_preferences_state_json(&loaded, json, sizeof(json)) != 0);
    CHECK(strstr(json, "\"revision\":7") != NULL);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(root) == 0);
    puts("native shell preference tests passed");
    return 0;
}
