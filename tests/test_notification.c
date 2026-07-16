#include "msys_shell_native/notification.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void)
{
    msys_native_notification_history history;
    const msys_native_notification *item;
    char json[16384];
    size_t removed;

    msys_native_notification_history_init(&history, 3u);
    CHECK(history.limit == 3u && history.count == 0u);
    CHECK(msys_native_notification_append(
        &history,
        "msys.notification.post",
        "{\"summary\":\"Build\",\"body\":\"Complete\","
        "\"application\":\"org.example.builder\",\"timestamp_ms\":9}",
        "",
        1u
    ));
    CHECK(msys_native_notification_append(
        &history, "msys.role.notification-presenter",
        "{\"message\":\"second\"}", "org.example.sender", 10u
    ));
    CHECK(msys_native_notification_append(
        &history, "msys.notification.post", "\"scalar\"", "", 11u
    ));
    CHECK(msys_native_notification_append(
        &history, "msys.notification.post",
        "{\"message\":\"newest\"}", "", 12u
    ));
    CHECK(history.count == 3u);
    item = msys_native_notification_newest(&history, 0u);
    CHECK(item && strcmp(item->message, "newest") == 0);
    item = msys_native_notification_newest(&history, 1u);
    CHECK(item && strcmp(item->message, "scalar") == 0);
    item = msys_native_notification_newest(&history, 2u);
    CHECK(item && strcmp(item->source, "org.example.sender") == 0);
    CHECK(msys_native_notification_newest(&history, 3u) == NULL);

    CHECK(msys_native_notification_list_json(
        &history, 2u, 1, json, sizeof(json)
    ));
    CHECK(strstr(json, "\"schema\":\"msys.notification-list.v1\"") != NULL);
    CHECK(strstr(json, "\"count\":3") != NULL);
    CHECK(strstr(json, "\"returned\":2") != NULL);
    CHECK(strstr(json, "\"visible\":true") != NULL);
    CHECK(strstr(json, "newest") < strstr(json, "scalar"));
    CHECK(!msys_native_notification_list_json(&history, 3u, 0, json, 12u));

    removed = msys_native_notification_clear(&history);
    CHECK(removed == 3u && history.count == 0u);
    CHECK(msys_native_notification_clear(&history) == 0u);
    puts("test_notification: ok");
    return 0;
}
