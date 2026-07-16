#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/preferences.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PREFERENCE_FILE "launcher-preferences.json"
#define MAX_PREFERENCE_FILE 4096u

typedef struct json_cursor {
    const char *current;
} json_cursor;

enum preference_field {
    FIELD_LAYOUT = 1u << 0,
    FIELD_WALLPAPER = 1u << 1,
    FIELD_ACCENT = 1u << 2,
    FIELD_ICON_SIZE = 1u << 3,
    FIELD_SHOW_LABELS = 1u << 4,
    FIELD_SORT = 1u << 5,
    FIELD_WALLPAPER_PATH = 1u << 6,
    FIELD_GRID_COLUMNS = 1u << 7,
    FIELD_GRID_ROWS = 1u << 8,
    FIELD_ACRYLIC = 1u << 9,
    FIELD_REQUIRED_V1 = (1u << 6) - 1u,
    FIELD_ALL = (1u << 10) - 1u
};

static void skip_space(json_cursor *cursor)
{
    while (isspace((unsigned char)*cursor->current) != 0) {
        cursor->current++;
    }
}

static int take(json_cursor *cursor, char value)
{
    skip_space(cursor);
    if (*cursor->current != value) {
        return 0;
    }
    cursor->current++;
    return 1;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    value = (char)tolower((unsigned char)value);
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

static int json_string(json_cursor *cursor, char *output, size_t capacity)
{
    size_t used = 0u;
    skip_space(cursor);
    if (*cursor->current++ != '"') {
        return 0;
    }
    while (*cursor->current != '\0' && *cursor->current != '"') {
        unsigned char value = (unsigned char)*cursor->current++;
        if (value < 0x20u) {
            return 0;
        }
        if (value == '\\') {
            char escaped = *cursor->current++;
            if (escaped == 'u') {
                unsigned codepoint = 0u;
                unsigned index;
                for (index = 0u; index < 4u; index++) {
                    int digit = hex_digit(*cursor->current++);
                    if (digit < 0) {
                        return 0;
                    }
                    codepoint = (codepoint << 4u) | (unsigned)digit;
                }
                if (codepoint > 0x7fu || codepoint < 0x20u) {
                    return 0;
                }
                value = (unsigned char)codepoint;
            } else {
                switch (escaped) {
                case '"': value = '"'; break;
                case '\\': value = '\\'; break;
                case '/': value = '/'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'n': value = '\n'; break;
                case 'r': value = '\r'; break;
                case 't': value = '\t'; break;
                default: return 0;
                }
            }
        }
        if (used + 1u >= capacity) {
            return 0;
        }
        output[used++] = (char)value;
    }
    if (*cursor->current != '"') {
        return 0;
    }
    cursor->current++;
    output[used] = '\0';
    return 1;
}

static int json_u64(json_cursor *cursor, uint64_t *output)
{
    uint64_t value = 0u;
    const char *begin;
    skip_space(cursor);
    begin = cursor->current;
    if (*cursor->current == '0') {
        cursor->current++;
        if (isdigit((unsigned char)*cursor->current) != 0) {
            return 0;
        }
    } else {
        if (*cursor->current < '1' || *cursor->current > '9') {
            return 0;
        }
        while (isdigit((unsigned char)*cursor->current) != 0) {
            unsigned digit = (unsigned)(*cursor->current - '0');
            if (value > (UINT64_MAX - digit) / 10u) {
                return 0;
            }
            value = value * 10u + digit;
            cursor->current++;
        }
    }
    if (cursor->current == begin) {
        return 0;
    }
    *output = value;
    return 1;
}

static int json_boolean(json_cursor *cursor, int *output)
{
    skip_space(cursor);
    if (strncmp(cursor->current, "true", 4u) == 0) {
        cursor->current += 4;
        *output = 1;
        return 1;
    }
    if (strncmp(cursor->current, "false", 5u) == 0) {
        cursor->current += 5;
        *output = 0;
        return 1;
    }
    return 0;
}

static int valid_color(const char *value)
{
    size_t index;
    if (strlen(value) != 7u || value[0] != '#') {
        return 0;
    }
    for (index = 1u; index < 7u; index++) {
        if (hex_digit(value[index]) < 0) {
            return 0;
        }
    }
    return 1;
}

static unsigned field_bit(const char *name)
{
    if (strcmp(name, "layout") == 0) return FIELD_LAYOUT;
    if (strcmp(name, "wallpaper_color") == 0) return FIELD_WALLPAPER;
    if (strcmp(name, "accent_color") == 0) return FIELD_ACCENT;
    if (strcmp(name, "icon_size") == 0) return FIELD_ICON_SIZE;
    if (strcmp(name, "show_labels") == 0) return FIELD_SHOW_LABELS;
    if (strcmp(name, "sort") == 0) return FIELD_SORT;
    if (strcmp(name, "wallpaper_path") == 0) return FIELD_WALLPAPER_PATH;
    if (strcmp(name, "grid_columns") == 0) return FIELD_GRID_COLUMNS;
    if (strcmp(name, "grid_rows") == 0) return FIELD_GRID_ROWS;
    if (strcmp(name, "acrylic") == 0) return FIELD_ACRYLIC;
    return 0u;
}

static int valid_layout(const char *value)
{
    return strcmp(value, "profile") == 0 || strcmp(value, "auto") == 0 ||
        strcmp(value, "mobile") == 0 || strcmp(value, "desktop") == 0 ||
        strcmp(value, "kiosk") == 0 || strcmp(value, "embedded") == 0;
}

static int valid_wallpaper_path(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    if (*cursor == '\0') return 1;
    if (*cursor != '/') return 0;
    while (*cursor != '\0') {
        /* Keep persisted JSON dependency-free by accepting paths which do not
         * need string escaping. Spaces and UTF-8 remain valid. */
        if (*cursor < 0x20u || *cursor == 0x7fu || *cursor == '"' || *cursor == '\\') {
            return 0;
        }
        cursor++;
    }
    return 1;
}

static enum msys_native_preferences_result parse_preferences_object(
    json_cursor *cursor,
    msys_native_preferences *preferences,
    int complete,
    unsigned *fields
)
{
    unsigned seen = 0u;
    if (!take(cursor, '{')) {
        return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
    }
    skip_space(cursor);
    if (*cursor->current == '}') {
        cursor->current++;
        return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
    }
    for (;;) {
        char name[32];
        unsigned bit;
        if (!json_string(cursor, name, sizeof(name)) || !take(cursor, ':')) {
            return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        }
        bit = field_bit(name);
        if (bit == 0u || (seen & bit) != 0u) {
            return bit == 0u ? MSYS_NATIVE_PREFERENCES_BAD_VALUE : MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        }
        seen |= bit;
        if (bit == FIELD_LAYOUT) {
            if (!json_string(cursor, preferences->layout, sizeof(preferences->layout)) ||
                !valid_layout(preferences->layout)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else if (bit == FIELD_WALLPAPER || bit == FIELD_ACCENT) {
            char *target = bit == FIELD_WALLPAPER
                ? preferences->wallpaper_color : preferences->accent_color;
            if (!json_string(cursor, target, 8u) || !valid_color(target)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else if (bit == FIELD_WALLPAPER_PATH) {
            if (!json_string(
                cursor,
                preferences->wallpaper_path,
                sizeof(preferences->wallpaper_path)
            ) || !valid_wallpaper_path(preferences->wallpaper_path)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else if (bit == FIELD_ICON_SIZE) {
            uint64_t value;
            if (!json_u64(cursor, &value) || value < 40u || value > 96u) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
            preferences->icon_size = (int)value;
        } else if (bit == FIELD_GRID_COLUMNS || bit == FIELD_GRID_ROWS) {
            uint64_t value;
            uint64_t maximum = bit == FIELD_GRID_COLUMNS ? 8u : 6u;
            if (!json_u64(cursor, &value) || value > maximum) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
            if (bit == FIELD_GRID_COLUMNS) preferences->grid_columns = (int)value;
            else preferences->grid_rows = (int)value;
        } else if (bit == FIELD_SHOW_LABELS) {
            if (!json_boolean(cursor, &preferences->show_labels)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else if (bit == FIELD_ACRYLIC) {
            if (!json_boolean(cursor, &preferences->acrylic)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else {
            if (!json_string(cursor, preferences->sort, sizeof(preferences->sort)) ||
                (strcmp(preferences->sort, "name") != 0 &&
                 strcmp(preferences->sort, "component") != 0)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        }
        skip_space(cursor);
        if (*cursor->current == '}') {
            cursor->current++;
            break;
        }
        if (*cursor->current++ != ',') {
            return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        }
    }
    if (
        (complete != 0 && (seen & FIELD_REQUIRED_V1) != FIELD_REQUIRED_V1) ||
        (complete == 0 && seen == 0u)
    ) {
        return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
    }
    *fields = seen;
    return MSYS_NATIVE_PREFERENCES_OK;
}

void msys_native_preferences_defaults(msys_native_preferences *preferences)
{
    memset(preferences, 0, sizeof(*preferences));
    (void)snprintf(preferences->layout, sizeof(preferences->layout), "profile");
    (void)snprintf(preferences->wallpaper_color, sizeof(preferences->wallpaper_color), "#F4F6FA");
    preferences->wallpaper_path[0] = '\0';
    (void)snprintf(preferences->accent_color, sizeof(preferences->accent_color), "#6750A4");
    preferences->icon_size = 64;
    preferences->grid_columns = 0;
    preferences->grid_rows = 0;
    preferences->show_labels = 1;
    preferences->acrylic = 0;
    (void)snprintf(preferences->sort, sizeof(preferences->sort), "name");
}

int msys_native_preferences_empty_request(const char *json)
{
    json_cursor cursor;
    if (json == NULL) {
        return 0;
    }
    cursor.current = json;
    if (!take(&cursor, '{') || !take(&cursor, '}')) {
        return 0;
    }
    skip_space(&cursor);
    return *cursor.current == '\0';
}

enum msys_native_preferences_result msys_native_preferences_merge(
    const char *json,
    const msys_native_preferences *base,
    msys_native_preferences *updated
)
{
    json_cursor cursor;
    unsigned fields = 0u;
    enum msys_native_preferences_result result;
    msys_native_preferences candidate;
    if (json == NULL || base == NULL || updated == NULL) {
        return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
    }
    candidate = *base;
    cursor.current = json;
    skip_space(&cursor);
    if (*cursor.current != '{') {
        return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
    }
    {
        json_cursor probe = cursor;
        char key[32];
        (void)take(&probe, '{');
        skip_space(&probe);
        if (*probe.current == '}') {
            return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
        }
        if (!json_string(&probe, key, sizeof(key)) || !take(&probe, ':')) {
            return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        }
        if (strcmp(key, "preferences") == 0) {
            cursor = probe;
            result = parse_preferences_object(&cursor, &candidate, 0, &fields);
            if (result != MSYS_NATIVE_PREFERENCES_OK) {
                return result;
            }
            skip_space(&cursor);
            if (*cursor.current++ != '}') {
                return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
            }
        } else {
            result = parse_preferences_object(&cursor, &candidate, 0, &fields);
            if (result != MSYS_NATIVE_PREFERENCES_OK) {
                return result;
            }
        }
    }
    skip_space(&cursor);
    if (*cursor.current != '\0') {
        return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
    }
    *updated = candidate;
    return MSYS_NATIVE_PREFERENCES_OK;
}

static int state_json(
    const msys_native_preferences *preferences,
    int event,
    int reset,
    char *output,
    size_t capacity
)
{
    int written = snprintf(
        output,
        capacity,
        event != 0
            ? "{\"revision\":%llu,\"preferences\":{\"layout\":\"%s\",\"wallpaper_color\":\"%s\",\"wallpaper_path\":\"%s\",\"accent_color\":\"%s\",\"icon_size\":%d,\"grid_columns\":%d,\"grid_rows\":%d,\"show_labels\":%s,\"acrylic\":%s,\"sort\":\"%s\"}%s}"
            : "{\"schema\":\"msys.shell-preferences.v1\",\"revision\":%llu,\"preferences\":{\"layout\":\"%s\",\"wallpaper_color\":\"%s\",\"wallpaper_path\":\"%s\",\"accent_color\":\"%s\",\"icon_size\":%d,\"grid_columns\":%d,\"grid_rows\":%d,\"show_labels\":%s,\"acrylic\":%s,\"sort\":\"%s\"}%s}",
        (unsigned long long)preferences->revision,
        preferences->layout,
        preferences->wallpaper_color,
        preferences->wallpaper_path,
        preferences->accent_color,
        preferences->icon_size,
        preferences->grid_columns,
        preferences->grid_rows,
        preferences->show_labels != 0 ? "true" : "false",
        preferences->acrylic != 0 ? "true" : "false",
        preferences->sort,
        event != 0 && reset != 0 ? ",\"reset\":true" : ""
    );
    return written >= 0 && (size_t)written < capacity;
}

int msys_native_preferences_state_json(
    const msys_native_preferences *preferences,
    char *output,
    size_t capacity
)
{
    return state_json(preferences, 0, 0, output, capacity);
}

int msys_native_preferences_event_json(
    const msys_native_preferences *preferences,
    int reset,
    char *output,
    size_t capacity
)
{
    return state_json(preferences, 1, reset, output, capacity);
}

static enum msys_native_preferences_result parse_state(
    const char *json,
    msys_native_preferences *preferences
)
{
    json_cursor cursor = {json};
    unsigned seen = 0u;
    unsigned preference_fields = 0u;
    if (!take(&cursor, '{')) return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
    for (;;) {
        char key[32];
        unsigned bit;
        skip_space(&cursor);
        if (*cursor.current == '}') {
            cursor.current++;
            break;
        }
        if (!json_string(&cursor, key, sizeof(key)) || !take(&cursor, ':')) {
            return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        }
        if (strcmp(key, "schema") == 0) bit = 1u;
        else if (strcmp(key, "revision") == 0) bit = 2u;
        else if (strcmp(key, "preferences") == 0) bit = 4u;
        else return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
        if ((seen & bit) != 0u) return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        seen |= bit;
        if (bit == 1u) {
            char schema[40];
            if (!json_string(&cursor, schema, sizeof(schema)) ||
                strcmp(schema, "msys.shell-preferences.v1") != 0) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else if (bit == 2u) {
            if (!json_u64(&cursor, &preferences->revision)) {
                return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
            }
        } else {
            enum msys_native_preferences_result result = parse_preferences_object(
                &cursor, preferences, 1, &preference_fields
            );
            if (result != MSYS_NATIVE_PREFERENCES_OK) return result;
        }
        skip_space(&cursor);
        if (*cursor.current == '}') {
            cursor.current++;
            break;
        }
        if (*cursor.current++ != ',') return MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
    }
    skip_space(&cursor);
    return seen == 7u && *cursor.current == '\0'
        ? MSYS_NATIVE_PREFERENCES_OK : MSYS_NATIVE_PREFERENCES_BAD_VALUE;
}

static int write_all(int descriptor, const char *data, size_t length)
{
    while (length > 0u) {
        ssize_t written = write(descriptor, data, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return 0;
        data += (size_t)written;
        length -= (size_t)written;
    }
    return 1;
}

enum msys_native_preferences_result msys_native_preferences_commit(
    const char *path,
    const msys_native_preferences *preferences
)
{
    char document[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
    char temporary[MSYS_NATIVE_PREFERENCES_PATH_CAPACITY + 40u];
    int descriptor;
    int ok;
    if (path == NULL || path[0] != '/' ||
        !msys_native_preferences_state_json(preferences, document, sizeof(document)) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(temporary)) {
        return MSYS_NATIVE_PREFERENCES_IO_ERROR;
    }
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) return MSYS_NATIVE_PREFERENCES_IO_ERROR;
    ok = write_all(descriptor, document, strlen(document)) &&
        write_all(descriptor, "\n", 1u) && fsync(descriptor) == 0;
    if (close(descriptor) != 0) ok = 0;
    if (ok != 0 && rename(temporary, path) != 0) ok = 0;
    if (ok == 0) (void)unlink(temporary);
    return ok != 0 ? MSYS_NATIVE_PREFERENCES_OK : MSYS_NATIVE_PREFERENCES_IO_ERROR;
}

enum msys_native_preferences_result msys_native_preferences_load(
    msys_native_preferences *preferences,
    char *path,
    size_t path_capacity
)
{
    const char *base = getenv("MSYS_COMPONENT_STATE_DIR");
    char buffer[MAX_PREFERENCE_FILE + 1u];
    int descriptor;
    ssize_t length;
    msys_native_preferences_defaults(preferences);
    path[0] = '\0';
    if (base == NULL || *base == '\0') base = getenv("MSYS_APP_STATE_DIR");
    if (base == NULL || base[0] != '/') return MSYS_NATIVE_PREFERENCES_NO_STATE_DIR;
    if (snprintf(path, path_capacity, "%s/%s", base, PREFERENCE_FILE) >= (int)path_capacity) {
        path[0] = '\0';
        return MSYS_NATIVE_PREFERENCES_NO_STATE_DIR;
    }
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return errno == ENOENT ? MSYS_NATIVE_PREFERENCES_OK : MSYS_NATIVE_PREFERENCES_IO_ERROR;
    }
    length = read(descriptor, buffer, MAX_PREFERENCE_FILE + 1u);
    if (close(descriptor) != 0 || length < 0 || (size_t)length > MAX_PREFERENCE_FILE) {
        return MSYS_NATIVE_PREFERENCES_IO_ERROR;
    }
    buffer[length] = '\0';
    if (parse_state(buffer, preferences) != MSYS_NATIVE_PREFERENCES_OK) {
        msys_native_preferences_defaults(preferences);
        return MSYS_NATIVE_PREFERENCES_BAD_VALUE;
    }
    return MSYS_NATIVE_PREFERENCES_OK;
}
