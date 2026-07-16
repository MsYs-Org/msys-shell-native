#include "msys_shell_native/notification.h"

#include "msys/mipc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void bounded_copy(char *output, size_t capacity, const char *value)
{
    size_t length;
    if (!output || capacity == 0u) return;
    if (!value) value = "";
    length = strlen(value);
    if (length >= capacity) length = capacity - 1u;
    memcpy(output, value, length);
    output[length] = '\0';
}

static int optional_string(
    const char *payload,
    const char *first,
    const char *second,
    const char *third,
    char *output,
    size_t capacity
)
{
    if (msys_mipc_json_get_string(
        payload, first, output, capacity, NULL
    ) == MSYS_MIPC_OK) return 1;
    if (second && msys_mipc_json_get_string(
        payload, second, output, capacity, NULL
    ) == MSYS_MIPC_OK) return 1;
    if (third && msys_mipc_json_get_string(
        payload, third, output, capacity, NULL
    ) == MSYS_MIPC_OK) return 1;
    output[0] = '\0';
    return 0;
}

static void scalar_message(const char *payload, char *output, size_t capacity)
{
    const char *cursor;
    size_t used = 0u;
    if (!output || capacity == 0u) return;
    output[0] = '\0';
    if (!payload || payload[0] != '"') return;
    cursor = payload + 1;
    while (*cursor && *cursor != '"' && used + 1u < capacity) {
        char value = *cursor++;
        if (value == '\\' && *cursor) {
            value = *cursor++;
            if (value == 'n' || value == 'r' || value == 't') value = ' ';
        }
        output[used++] = value;
    }
    output[used] = '\0';
}

static int append_text(
    char *output,
    size_t capacity,
    size_t *used,
    const char *format,
    ...
)
{
    va_list arguments;
    int written;
    if (!output || !used || *used >= capacity) return 0;
    va_start(arguments, format);
    written = vsnprintf(output + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *used) return 0;
    *used += (size_t)written;
    return 1;
}

static int append_json_string(
    char *output,
    size_t capacity,
    size_t *used,
    const char *value
)
{
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    if (!append_text(output, capacity, used, "\"")) return 0;
    while (*cursor) {
        const char *escape = NULL;
        char unicode[7];
        size_t length;
        if (*cursor == '"') escape = "\\\"";
        else if (*cursor == '\\') escape = "\\\\";
        else if (*cursor == '\n') escape = "\\n";
        else if (*cursor == '\r') escape = "\\r";
        else if (*cursor == '\t') escape = "\\t";
        else if (*cursor < 0x20u) {
            (void)snprintf(unicode, sizeof(unicode), "\\u%04x", *cursor);
            escape = unicode;
        }
        if (escape) {
            length = strlen(escape);
            if (*used + length >= capacity) return 0;
            memcpy(output + *used, escape, length);
            *used += length;
            output[*used] = '\0';
        } else {
            if (*used + 2u > capacity) return 0;
            output[(*used)++] = (char)*cursor;
            output[*used] = '\0';
        }
        cursor++;
    }
    return append_text(output, capacity, used, "\"");
}

void msys_native_notification_history_init(
    msys_native_notification_history *history,
    size_t limit
)
{
    if (!history) return;
    memset(history, 0, sizeof(*history));
    if (limit == 0u) limit = 1u;
    if (limit > MSYS_NATIVE_MAX_NOTIFICATIONS) {
        limit = MSYS_NATIVE_MAX_NOTIFICATIONS;
    }
    history->limit = limit;
    history->sequence = 1u;
}

int msys_native_notification_append(
    msys_native_notification_history *history,
    const char *topic,
    const char *payload,
    const char *source,
    uint64_t timestamp_ms
)
{
    msys_native_notification *item;
    size_t position;
    uint64_t supplied_timestamp = 0u;
    char payload_source[MSYS_NATIVE_NOTIFICATION_SOURCE_CAPACITY];
    if (!history || history->limit == 0u || !topic || !*topic || !payload) {
        return 0;
    }
    if (history->count < history->limit) {
        position = (history->start + history->count) % history->limit;
        history->count++;
    } else {
        position = history->start;
        history->start = (history->start + 1u) % history->limit;
    }
    item = &history->items[position];
    memset(item, 0, sizeof(*item));
    if (msys_mipc_json_get_u64(
        payload, "timestamp_ms", &supplied_timestamp
    ) == MSYS_MIPC_OK) {
        timestamp_ms = supplied_timestamp;
    }
    item->timestamp_ms = timestamp_ms;
    (void)snprintf(
        item->id,
        sizeof(item->id),
        "%llx-%llx",
        (unsigned long long)timestamp_ms,
        (unsigned long long)history->sequence++
    );
    bounded_copy(item->topic, sizeof(item->topic), topic);
    (void)optional_string(
        payload, "title", "summary", NULL,
        item->title, sizeof(item->title)
    );
    if (!optional_string(
        payload, "message", "body", "text",
        item->message, sizeof(item->message)
    )) {
        scalar_message(payload, item->message, sizeof(item->message));
    }
    if (!item->message[0] && item->title[0]) {
        bounded_copy(item->message, sizeof(item->message), item->title);
        item->title[0] = '\0';
    }
    payload_source[0] = '\0';
    (void)optional_string(
        payload, "source", "application", "app",
        payload_source, sizeof(payload_source)
    );
    bounded_copy(
        item->source,
        sizeof(item->source),
        source && *source ? source : payload_source
    );
    if (!optional_string(
        payload, "urgency", NULL, NULL,
        item->urgency, sizeof(item->urgency)
    )) {
        bounded_copy(item->urgency, sizeof(item->urgency), "normal");
    }
    return 1;
}

const msys_native_notification *msys_native_notification_newest(
    const msys_native_notification_history *history,
    size_t index
)
{
    size_t position;
    if (!history || history->limit == 0u || index >= history->count) return NULL;
    position = (
        history->start + history->count - 1u - index
    ) % history->limit;
    return &history->items[position];
}

size_t msys_native_notification_clear(msys_native_notification_history *history)
{
    size_t removed;
    if (!history) return 0u;
    removed = history->count;
    memset(history->items, 0, sizeof(history->items));
    history->start = 0u;
    history->count = 0u;
    return removed;
}

int msys_native_notification_list_json(
    const msys_native_notification_history *history,
    size_t requested,
    int visible,
    char *output,
    size_t capacity
)
{
    size_t used = 0u;
    size_t count;
    size_t index;
    if (!history || !output || capacity == 0u) return 0;
    count = requested < history->count ? requested : history->count;
    if (!append_text(
        output, capacity, &used,
        "{\"schema\":\"msys.notification-list.v1\",\"notifications\":["
    )) return 0;
    for (index = 0u; index < count; index++) {
        const msys_native_notification *item =
            msys_native_notification_newest(history, index);
        if (!item || !append_text(
            output, capacity, &used,
            "%s{\"id\":", index ? "," : ""
        ) || !append_json_string(output, capacity, &used, item->id) ||
            !append_text(
                output, capacity, &used,
                ",\"timestamp_ms\":%llu,\"topic\":",
                (unsigned long long)item->timestamp_ms
            ) || !append_json_string(output, capacity, &used, item->topic) ||
            !append_text(output, capacity, &used, ",\"source\":") ||
            !append_json_string(output, capacity, &used, item->source) ||
            !append_text(output, capacity, &used, ",\"title\":") ||
            !append_json_string(output, capacity, &used, item->title) ||
            !append_text(output, capacity, &used, ",\"message\":") ||
            !append_json_string(output, capacity, &used, item->message) ||
            !append_text(output, capacity, &used, ",\"urgency\":") ||
            !append_json_string(output, capacity, &used, item->urgency) ||
            !append_text(output, capacity, &used, "}")) return 0;
    }
    return append_text(
        output,
        capacity,
        &used,
        "],\"count\":%zu,\"limit\":%zu,\"returned\":%zu,\"visible\":%s}",
        history->count,
        history->limit,
        count,
        visible ? "true" : "false"
    );
}
