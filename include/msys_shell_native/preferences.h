#ifndef MSYS_SHELL_NATIVE_PREFERENCES_H
#define MSYS_SHELL_NATIVE_PREFERENCES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSYS_NATIVE_PREFERENCES_PATH_CAPACITY 1024u
#define MSYS_NATIVE_PREFERENCES_JSON_CAPACITY 2048u

typedef struct msys_native_preferences {
    uint64_t revision;
    char layout[16];
    char wallpaper_color[8];
    char wallpaper_path[MSYS_NATIVE_PREFERENCES_PATH_CAPACITY];
    char accent_color[8];
    int icon_size;
    int grid_columns;
    int grid_rows;
    int show_labels;
    int acrylic;
    char navigation_mode[16];
    char navigation_visibility[16];
    char status_visibility[16];
    int icon_spacing;
    int folders_enabled;
    int large_folders_enabled;
    int animations_enabled;
    int reduce_motion;
    char sort[16];
} msys_native_preferences;

enum msys_native_preferences_result {
    MSYS_NATIVE_PREFERENCES_OK = 0,
    MSYS_NATIVE_PREFERENCES_BAD_REQUEST,
    MSYS_NATIVE_PREFERENCES_BAD_VALUE,
    MSYS_NATIVE_PREFERENCES_IO_ERROR,
    MSYS_NATIVE_PREFERENCES_NO_STATE_DIR
};

void msys_native_preferences_defaults(msys_native_preferences *preferences);

/* Resolve an absolute, supervisor-owned state directory and load its state. */
enum msys_native_preferences_result msys_native_preferences_load(
    msys_native_preferences *preferences,
    char *path,
    size_t path_capacity
);

int msys_native_preferences_empty_request(const char *json);

enum msys_native_preferences_result msys_native_preferences_merge(
    const char *json,
    const msys_native_preferences *base,
    msys_native_preferences *updated
);

enum msys_native_preferences_result msys_native_preferences_commit(
    const char *path,
    const msys_native_preferences *preferences
);

int msys_native_preferences_state_json(
    const msys_native_preferences *preferences,
    char *output,
    size_t capacity
);

int msys_native_preferences_event_json(
    const msys_native_preferences *preferences,
    int reset,
    char *output,
    size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
