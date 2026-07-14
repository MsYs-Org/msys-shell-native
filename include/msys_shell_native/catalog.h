#ifndef MSYS_SHELL_NATIVE_CATALOG_H
#define MSYS_SHELL_NATIVE_CATALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSYS_NATIVE_MAX_APPS 24u
#define MSYS_NATIVE_MAX_TASKS 16u
#define MSYS_NATIVE_COMPONENT_CAPACITY 161u
#define MSYS_NATIVE_NAME_CAPACITY 97u
#define MSYS_NATIVE_SUMMARY_CAPACITY 161u
#define MSYS_NATIVE_WINDOW_ID_CAPACITY 161u
#define MSYS_NATIVE_IDENTITY_CAPACITY 161u
#define MSYS_NATIVE_PATH_CAPACITY 1024u
#define MSYS_NATIVE_WINDOW_META_CAPACITY 48u

typedef struct msys_native_app {
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    char name[MSYS_NATIVE_NAME_CAPACITY];
    char summary[MSYS_NATIVE_SUMMARY_CAPACITY];
    char icon_path[MSYS_NATIVE_PATH_CAPACITY];
} msys_native_app;

typedef struct msys_native_task {
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    char window_id[MSYS_NATIVE_WINDOW_ID_CAPACITY];
    char title[MSYS_NATIVE_NAME_CAPACITY];
    char identity[MSYS_NATIVE_IDENTITY_CAPACITY];
    char native_id[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char role[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char kind[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char state[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char thumbnail[MSYS_NATIVE_PATH_CAPACITY];
} msys_native_task;

int msys_native_parse_apps(
    const char *payload,
    msys_native_app *items,
    size_t capacity,
    size_t *count
);

int msys_native_parse_tasks(
    const char *payload,
    msys_native_task *items,
    size_t capacity,
    size_t *count
);

int msys_native_json_escape(
    const char *value,
    char *output,
    size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
