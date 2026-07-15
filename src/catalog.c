#include "msys_shell_native/catalog.h"

#include "msys/mipc.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define OBJECT_CAPACITY 8192u

static const char *skip_space(const char *cursor, const char *end)
{
    while (cursor < end && isspace((unsigned char)*cursor) != 0) {
        cursor++;
    }
    return cursor;
}

static int next_object(
    const char *array,
    size_t length,
    size_t *offset,
    const char **object,
    size_t *object_length
)
{
    const char *begin = array;
    const char *end = array + length;
    const char *cursor;
    const char *start;
    unsigned int depth = 0u;
    int in_string = 0;
    int escaped = 0;
    if (array == NULL || offset == NULL || object == NULL || object_length == NULL) {
        return -1;
    }
    cursor = skip_space(begin + *offset, end);
    if (*offset == 0u) {
        if (cursor >= end || *cursor != '[') {
            return -1;
        }
        cursor = skip_space(cursor + 1, end);
    } else if (cursor < end && *cursor == ',') {
        cursor = skip_space(cursor + 1, end);
    }
    if (cursor < end && *cursor == ']') {
        *offset = (size_t)(cursor + 1 - begin);
        return 0;
    }
    if (cursor >= end || *cursor != '{') {
        return -1;
    }
    start = cursor;
    for (; cursor < end; cursor++) {
        char value = *cursor;
        if (in_string != 0) {
            if (escaped != 0) {
                escaped = 0;
            } else if (value == '\\') {
                escaped = 1;
            } else if (value == '"') {
                in_string = 0;
            }
            continue;
        }
        if (value == '"') {
            in_string = 1;
        } else if (value == '{' || value == '[') {
            depth++;
        } else if (value == '}' || value == ']') {
            if (depth == 0u) {
                return -1;
            }
            depth--;
            if (depth == 0u) {
                *object = start;
                *object_length = (size_t)(cursor + 1 - start);
                *offset = (size_t)(cursor + 1 - begin);
                return 1;
            }
        }
    }
    return -1;
}

static int copy_object(const char *raw, size_t length, char *object)
{
    if (length == 0u || length >= OBJECT_CAPACITY) {
        return 0;
    }
    memcpy(object, raw, length);
    object[length] = '\0';
    return 1;
}

static int get_optional_string(
    const char *object,
    const char *key,
    char *output,
    size_t capacity
)
{
    int result = msys_mipc_json_get_string(object, key, output, capacity, NULL);
    if (result == MSYS_MIPC_NOT_FOUND) {
        output[0] = '\0';
        return 1;
    }
    if (result != MSYS_MIPC_OK) {
        const char *raw = NULL;
        size_t length = 0u;
        if (
            msys_mipc_json_get_raw(object, key, &raw, &length) == MSYS_MIPC_OK &&
            length == 4u && memcmp(raw, "null", 4u) == 0
        ) {
            output[0] = '\0';
            return 1;
        }
    }
    return result == MSYS_MIPC_OK;
}

static void copy_truncated(char *output, size_t capacity, const char *value)
{
    size_t length;
    if (output == NULL || capacity == 0u) {
        return;
    }
    length = strlen(value);
    if (length >= capacity) {
        length = capacity - 1u;
    }
    memcpy(output, value, length);
    output[length] = '\0';
}

static int safe_relative_path(const char *value)
{
    const char *cursor = value;
    if (value == NULL || *value == '\0' || *value == '/') return 0;
    while (*cursor != '\0') {
        const char *segment = cursor;
        size_t length;
        while (*cursor != '\0' && *cursor != '/') cursor++;
        length = (size_t)(cursor - segment);
        if (length == 0u || (length == 1u && segment[0] == '.') ||
            (length == 2u && segment[0] == '.' && segment[1] == '.')) {
            return 0;
        }
        if (*cursor == '/') cursor++;
    }
    return 1;
}

static void parse_icon_path(const char *object, msys_native_app *item)
{
    const char *icons = NULL;
    size_t icons_length = 0u;
    size_t offset = 0u;
    const char *raw = NULL;
    size_t raw_length = 0u;
    char icon[OBJECT_CAPACITY];
    char path[MSYS_NATIVE_PATH_CAPACITY];
    char package_root[MSYS_NATIVE_PATH_CAPACITY];
    const char *relative;
    item->icon_path[0] = '\0';
    if (
        get_optional_string(
            object, "package_root", package_root, sizeof(package_root)
        ) == 0 || package_root[0] != '/' ||
        msys_mipc_json_get_raw(object, "icons", &icons, &icons_length) != MSYS_MIPC_OK ||
        next_object(icons, icons_length, &offset, &raw, &raw_length) != 1 ||
        copy_object(raw, raw_length, icon) == 0 ||
        get_optional_string(icon, "path", path, sizeof(path)) == 0
    ) {
        return;
    }
    if (path[0] == '\0') return;
    relative = strlen(path) >= 9u && memcmp(path, "@package/", 9u) == 0
        ? path + 9 : path;
    if (
        safe_relative_path(relative) == 0 ||
        snprintf(
            item->icon_path,
            sizeof(item->icon_path),
            "%s/%s",
            package_root,
            relative
        ) >= (int)sizeof(item->icon_path)
    ) {
        item->icon_path[0] = '\0';
    }
}

int msys_native_parse_apps(
    const char *payload,
    msys_native_app *items,
    size_t capacity,
    size_t *count
)
{
    const char *array = NULL;
    size_t array_length = 0u;
    size_t offset = 0u;
    size_t used = 0u;
    int next;
    if (payload == NULL || items == NULL || count == NULL) {
        return 0;
    }
    *count = 0u;
    if (
        msys_mipc_json_get_raw(payload, "apps", &array, &array_length) != MSYS_MIPC_OK
    ) {
        return 0;
    }
    while (used < capacity && used < MSYS_NATIVE_MAX_APPS) {
        const char *raw = NULL;
        size_t raw_length = 0u;
        char object[OBJECT_CAPACITY];
        msys_native_app item;
        memset(&item, 0, sizeof(item));
        next = next_object(array, array_length, &offset, &raw, &raw_length);
        if (next == 0) {
            break;
        }
        if (next < 0) {
            return 0;
        }
        if (copy_object(raw, raw_length, object) == 0) {
            continue;
        }
        if (
            msys_mipc_json_get_string(
                object,
                "id",
                item.component,
                sizeof(item.component),
                NULL
            ) != MSYS_MIPC_OK ||
            strchr(item.component, ':') == NULL
        ) {
            continue;
        }
        if (
            get_optional_string(object, "name", item.name, sizeof(item.name)) == 0 ||
            get_optional_string(object, "summary", item.summary, sizeof(item.summary)) == 0
        ) {
            continue;
        }
        if (item.summary[0] == '\0') {
            if (
                get_optional_string(
                    object,
                    "package_summary",
                    item.summary,
                    sizeof(item.summary)
                ) == 0
            ) {
                continue;
            }
        }
        if (item.name[0] == '\0') {
            copy_truncated(item.name, sizeof(item.name), item.component);
        }
        parse_icon_path(object, &item);
        items[used++] = item;
    }
    *count = used;
    return 1;
}

static int task_state_rank(const msys_native_task *task)
{
    if (strcmp(task->state, "visible") == 0)
        return 0;
    if (strcmp(task->state, "minimized") == 0)
        return 1;
    return 2;
}

int msys_native_parse_tasks(
    const char *payload,
    msys_native_task *items,
    size_t capacity,
    size_t *count
)
{
    const char *array = NULL;
    size_t array_length = 0u;
    size_t offset = 0u;
    size_t used = 0u;
    int next;

    if (payload == NULL || items == NULL || count == NULL) {
        return 0;
    }
    *count = 0u;
    if (
        msys_mipc_json_get_raw(payload, "windows", &array, &array_length) != MSYS_MIPC_OK
    ) {
        return 0;
    }
    while (used < capacity && used < MSYS_NATIVE_MAX_TASKS) {
        const char *raw = NULL;
        size_t raw_length = 0u;
        char object[OBJECT_CAPACITY];
        msys_native_task item;
        memset(&item, 0, sizeof(item));
        next = next_object(array, array_length, &offset, &raw, &raw_length);
        if (next == 0) {
            break;
        }
        if (next < 0) {
            return 0;
        }
        if (copy_object(raw, raw_length, object) == 0) {
            continue;
        }
        if (
            get_optional_string(
                object,
                "component",
                item.component,
                sizeof(item.component)
            ) == 0 ||
            get_optional_string(object, "id", item.window_id, sizeof(item.window_id)) == 0 ||
            get_optional_string(object, "title", item.title, sizeof(item.title)) == 0 ||
            get_optional_string(
                object,
                "identity",
                item.identity,
                sizeof(item.identity)
            ) == 0 ||
            get_optional_string(object, "native_id", item.native_id, sizeof(item.native_id)) == 0 ||
            get_optional_string(object, "role", item.role, sizeof(item.role)) == 0 ||
            get_optional_string(object, "kind", item.kind, sizeof(item.kind)) == 0 ||
            get_optional_string(object, "state", item.state, sizeof(item.state)) == 0 ||
            get_optional_string(object, "thumbnail", item.thumbnail, sizeof(item.thumbnail)) == 0
        ) {
            continue;
        }
        if (
            (item.kind[0] != '\0' && strcmp(item.kind, "application") != 0) ||
            (item.role[0] != '\0' && strcmp(item.role, "application") != 0)
        ) {
            continue;
        }
        if (item.thumbnail[0] != '\0' && item.thumbnail[0] != '/') {
            item.thumbnail[0] = '\0';
        }
        if (item.component[0] == '\0' && item.window_id[0] == '\0') {
            continue;
        }
        if (item.title[0] == '\0') {
            copy_truncated(
                item.title,
                sizeof(item.title),
                item.component[0] != '\0' ? item.component : item.window_id
            );
        }
        {
            size_t duplicate = used;
            size_t position;

            for (position = 0u; position < used; position++) {
                int same_component = item.component[0] != '\0' &&
                    strcmp(items[position].component, item.component) == 0;
                int same_external = item.component[0] == '\0' &&
                    items[position].component[0] == '\0' &&
                    strcmp(items[position].window_id, item.window_id) == 0;

                if (same_component || same_external) {
                    duplicate = position;
                    break;
                }
            }
            if (duplicate < used) {
                if (task_state_rank(&item) < task_state_rank(&items[duplicate])) {
                    size_t position;

                    /* The better replacement appears at its own place in the
                     * authoritative stack, not at the stale duplicate's old
                     * position. */
                    for (position = duplicate; position + 1u < used; position++)
                        items[position] = items[position + 1u];
                    items[used - 1u] = item;
                }
                continue;
            }
        }
        items[used++] = item;
    }
    {
        size_t position;

        /* Stable insertion sort: visible, minimized, hidden.  Equal-ranked
         * tasks retain the authoritative X11 top-to-bottom ordering. */
        for (position = 1u; position < used; position++) {
            msys_native_task item = items[position];
            size_t insertion = position;

            while (insertion > 0u &&
                    task_state_rank(&item) < task_state_rank(&items[insertion - 1u])) {
                items[insertion] = items[insertion - 1u];
                insertion--;
            }
            items[insertion] = item;
        }
    }
    *count = used;
    return 1;
}

int msys_native_apply_task_resources(
    const char *payload,
    msys_native_task *items,
    size_t count
)
{
    const char *array = NULL;
    size_t array_length = 0u;
    size_t offset = 0u;
    size_t position;
    int next;
    if (payload == NULL || items == NULL) return 0;
    for (position = 0u; position < count; position++) {
        items[position].component_state[0] = '\0';
        items[position].lifecycle[0] = '\0';
        items[position].rss_kib = 0u;
        items[position].pss_kib = 0u;
        items[position].rss_available = 0;
        items[position].pss_available = 0;
    }
    if (
        msys_mipc_json_get_raw(payload, "windows", &array, &array_length) !=
        MSYS_MIPC_OK
    ) {
        return 0;
    }
    for (;;) {
        const char *raw = NULL;
        size_t raw_length = 0u;
        const char *resource_raw = NULL;
        size_t resource_length = 0u;
        char object[OBJECT_CAPACITY];
        char resources[OBJECT_CAPACITY];
        char component[MSYS_NATIVE_COMPONENT_CAPACITY];
        next = next_object(array, array_length, &offset, &raw, &raw_length);
        if (next == 0) break;
        if (next < 0) return 0;
        if (
            copy_object(raw, raw_length, object) == 0 ||
            get_optional_string(
                object, "component", component, sizeof(component)
            ) == 0 ||
            component[0] == '\0'
        ) {
            continue;
        }
        for (position = 0u; position < count; position++) {
            if (strcmp(items[position].component, component) == 0) break;
        }
        if (position >= count) continue;
        if (
            get_optional_string(
                object,
                "state",
                items[position].component_state,
                sizeof(items[position].component_state)
            ) == 0 ||
            get_optional_string(
                object,
                "lifecycle",
                items[position].lifecycle,
                sizeof(items[position].lifecycle)
            ) == 0
        ) {
            items[position].component_state[0] = '\0';
            items[position].lifecycle[0] = '\0';
            continue;
        }
        if (
            msys_mipc_json_get_raw(
                object, "resources", &resource_raw, &resource_length
            ) != MSYS_MIPC_OK ||
            copy_object(resource_raw, resource_length, resources) == 0
        ) {
            continue;
        }
        items[position].rss_available = msys_mipc_json_get_u64(
            resources, "rss_kib", &items[position].rss_kib
        ) == MSYS_MIPC_OK;
        items[position].pss_available = msys_mipc_json_get_u64(
            resources, "pss_kib", &items[position].pss_kib
        ) == MSYS_MIPC_OK;
    }
    return 1;
}

const char *msys_native_task_display_name(
    const msys_native_task *task,
    const msys_native_app *apps,
    size_t app_count
)
{
    size_t index;

    if (task == NULL) {
        return "";
    }
    if (task->component[0] != '\0' && apps != NULL) {
        for (index = 0u; index < app_count; index++) {
            if (
                strcmp(task->component, apps[index].component) == 0 &&
                apps[index].name[0] != '\0'
            ) {
                return apps[index].name;
            }
        }
    }
    return task->title;
}

int msys_native_json_escape(
    const char *value,
    char *output,
    size_t capacity
)
{
    size_t used = 0u;
    const unsigned char *cursor = (const unsigned char *)value;
    if (value == NULL || output == NULL || capacity == 0u) {
        return 0;
    }
    while (*cursor != '\0') {
        const char *escape = NULL;
        char unicode[7];
        size_t length;
        if (*cursor == '"') {
            escape = "\\\"";
        } else if (*cursor == '\\') {
            escape = "\\\\";
        } else if (*cursor == '\n') {
            escape = "\\n";
        } else if (*cursor == '\r') {
            escape = "\\r";
        } else if (*cursor == '\t') {
            escape = "\\t";
        } else if (*cursor < 0x20u) {
            (void)snprintf(unicode, sizeof(unicode), "\\u%04x", *cursor);
            escape = unicode;
        }
        if (escape != NULL) {
            length = strlen(escape);
            if (used + length + 1u > capacity) {
                return 0;
            }
            memcpy(output + used, escape, length);
            used += length;
        } else {
            if (used + 2u > capacity) {
                return 0;
            }
            output[used++] = (char)*cursor;
        }
        cursor++;
    }
    output[used] = '\0';
    return 1;
}
