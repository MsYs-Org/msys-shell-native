#ifndef MSYS_SHELL_NATIVE_NOTIFICATION_H
#define MSYS_SHELL_NATIVE_NOTIFICATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSYS_NATIVE_MAX_NOTIFICATIONS 24u
#define MSYS_NATIVE_NOTIFICATION_ID_CAPACITY 48u
#define MSYS_NATIVE_NOTIFICATION_TOPIC_CAPACITY 64u
#define MSYS_NATIVE_NOTIFICATION_SOURCE_CAPACITY 80u
#define MSYS_NATIVE_NOTIFICATION_TITLE_CAPACITY 96u
#define MSYS_NATIVE_NOTIFICATION_MESSAGE_CAPACITY 256u
#define MSYS_NATIVE_NOTIFICATION_URGENCY_CAPACITY 16u

typedef struct msys_native_notification {
    uint64_t timestamp_ms;
    char id[MSYS_NATIVE_NOTIFICATION_ID_CAPACITY];
    char topic[MSYS_NATIVE_NOTIFICATION_TOPIC_CAPACITY];
    char source[MSYS_NATIVE_NOTIFICATION_SOURCE_CAPACITY];
    char title[MSYS_NATIVE_NOTIFICATION_TITLE_CAPACITY];
    char message[MSYS_NATIVE_NOTIFICATION_MESSAGE_CAPACITY];
    char urgency[MSYS_NATIVE_NOTIFICATION_URGENCY_CAPACITY];
} msys_native_notification;

typedef struct msys_native_notification_history {
    msys_native_notification items[MSYS_NATIVE_MAX_NOTIFICATIONS];
    size_t start;
    size_t count;
    size_t limit;
    uint64_t sequence;
} msys_native_notification_history;

void msys_native_notification_history_init(
    msys_native_notification_history *history,
    size_t limit
);

int msys_native_notification_append(
    msys_native_notification_history *history,
    const char *topic,
    const char *payload,
    const char *source,
    uint64_t timestamp_ms
);

const msys_native_notification *msys_native_notification_newest(
    const msys_native_notification_history *history,
    size_t index
);

size_t msys_native_notification_clear(msys_native_notification_history *history);

int msys_native_notification_list_json(
    const msys_native_notification_history *history,
    size_t requested,
    int visible,
    char *output,
    size_t capacity
);

#ifdef __cplusplus
}
#endif

#endif
