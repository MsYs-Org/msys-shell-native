#ifndef MSYS_SHELL_NATIVE_CATALOG_H
#define MSYS_SHELL_NATIVE_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSYS_NATIVE_MAX_APPS 24u
#define MSYS_NATIVE_MAX_TASKS 16u
#define MSYS_NATIVE_MAX_PROCESSES 64u
#define MSYS_NATIVE_COMPONENT_CAPACITY 161u
#define MSYS_NATIVE_NAME_CAPACITY 97u
#define MSYS_NATIVE_SUMMARY_CAPACITY 161u
#define MSYS_NATIVE_WINDOW_ID_CAPACITY 161u
#define MSYS_NATIVE_IDENTITY_CAPACITY 161u
#define MSYS_NATIVE_PATH_CAPACITY 1024u
#define MSYS_NATIVE_WINDOW_META_CAPACITY 48u
#define MSYS_NATIVE_VERSION_CAPACITY 33u
#define MSYS_NATIVE_MAX_QUICK_ACTIONS 3u
#define MSYS_NATIVE_ACTION_ID_CAPACITY 65u
#define MSYS_NATIVE_INTENT_CAPACITY 161u

typedef struct msys_native_quick_action {
    char id[MSYS_NATIVE_ACTION_ID_CAPACITY];
    char label[MSYS_NATIVE_NAME_CAPACITY];
    char action[MSYS_NATIVE_INTENT_CAPACITY];
} msys_native_quick_action;

typedef struct msys_native_app {
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    char name[MSYS_NATIVE_NAME_CAPACITY];
    char summary[MSYS_NATIVE_SUMMARY_CAPACITY];
    char icon_path[MSYS_NATIVE_PATH_CAPACITY];
    char version[MSYS_NATIVE_VERSION_CAPACITY];
    msys_native_quick_action quick_actions[MSYS_NATIVE_MAX_QUICK_ACTIONS];
    size_t quick_action_count;
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
    char component_state[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char lifecycle[MSYS_NATIVE_WINDOW_META_CAPACITY];
    uint64_t rss_kib;
    uint64_t pss_kib;
    int rss_available;
    int pss_available;
    char thumbnail[MSYS_NATIVE_PATH_CAPACITY];
} msys_native_task;

typedef struct msys_native_process {
    uint64_t pid;
    uint64_t ppid;
    uint64_t uid;
    uint64_t rss_kib;
    uint64_t generation;
    char name[MSYS_NATIVE_NAME_CAPACITY];
    char state[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char source[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    char component_state[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char runtime[MSYS_NATIVE_WINDOW_META_CAPACITY];
    char lifecycle[MSYS_NATIVE_WINDOW_META_CAPACITY];
    int msys_owned;
    int rss_available;
} msys_native_process;

typedef struct msys_native_process_metadata {
    uint64_t managed_count;
    uint64_t system_count;
    int managed_truncated;
    int system_truncated;
} msys_native_process_metadata;

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

int msys_native_apply_task_resources(
    const char *payload,
    msys_native_task *items,
    size_t count
);

int msys_native_parse_processes(
    const char *payload,
    msys_native_process *items,
    size_t capacity,
    size_t *count,
    msys_native_process_metadata *metadata
);

int msys_native_parse_wifi_device(
    const char *payload,
    char *device_id,
    size_t capacity
);

int msys_native_parse_wifi_state(
    const char *payload,
    int *connected,
    int *signal_known,
    int *signal_dbm
);

const char *msys_native_task_display_name(
    const msys_native_task *task,
    const msys_native_app *apps,
    size_t app_count
);

int msys_native_json_escape(
    const char *value,
    char *output,
    size_t capacity
);

/* Build the exact payload accepted by msys.core.activate. */
int msys_native_quick_action_payload(
    const msys_native_quick_action *action,
    const char *component,
    char *output,
    size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
