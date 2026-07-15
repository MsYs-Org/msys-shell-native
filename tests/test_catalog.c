#include "msys_shell_native/catalog.h"

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
    msys_native_app apps[MSYS_NATIVE_MAX_APPS];
    msys_native_task tasks[MSYS_NATIVE_MAX_TASKS];
    size_t count = 0u;
    size_t app_count = 0u;
    char escaped[64];
    CHECK(msys_native_parse_apps(
        "{\"apps\":[{\"id\":\"org.msys.apps:notes\",\"name\":\"便笺\",\"summary\":\"First\",\"package_root\":\"/opt/msys/packages/notes\",\"icons\":[{\"path\":\"@package/files/icon.ppm\"}]},{\"id\":\"bad\",\"name\":\"skip\"},{\"id\":\"org.example:beta\"}]}",
        apps,
        MSYS_NATIVE_MAX_APPS,
        &count
    ));
    CHECK(count == 2u);
    CHECK(strcmp(apps[0].component, "org.msys.apps:notes") == 0);
    CHECK(strcmp(apps[0].name, "便笺") == 0);
    CHECK(strcmp(apps[0].summary, "First") == 0);
    CHECK(strcmp(apps[0].icon_path, "/opt/msys/packages/notes/files/icon.ppm") == 0);
    CHECK(strcmp(apps[1].name, "org.example:beta") == 0);
    app_count = count;
    CHECK(msys_native_parse_tasks(
        "{\"schema\":\"msys.window-list.v1\",\"windows\":[{\"component\":\"org.msys.apps:notes\",\"title\":\"MSYS Notes - clipped\",\"kind\":\"application\",\"role\":\"application\",\"native_id\":\"0x2a\",\"thumbnail\":\"/tmp/notes.ppm\"},{\"component\":null,\"identity\":null,\"id\":\"msys.x11-window.v1:7:1\",\"title\":\"External\"},{\"id\":\"msys.x11-window.v1:8:1\",\"title\":\"Chrome\",\"kind\":\"overlay\"},{\"title\":\"skip\"}]}",
        tasks,
        MSYS_NATIVE_MAX_TASKS,
        &count
    ));
    CHECK(count == 2u);
    CHECK(strcmp(tasks[1].window_id, "msys.x11-window.v1:7:1") == 0);
    CHECK(strcmp(tasks[0].thumbnail, "/tmp/notes.ppm") == 0);
    CHECK(strcmp(tasks[0].native_id, "0x2a") == 0);
    CHECK(strcmp(msys_native_task_display_name(&tasks[0], apps, app_count), "便笺") == 0);
    CHECK(strcmp(msys_native_task_display_name(&tasks[1], apps, app_count), "External") == 0);
    CHECK(strcmp(msys_native_task_display_name(&tasks[0], NULL, 0u), "MSYS Notes - clipped") == 0);
    CHECK(msys_native_parse_tasks(
        "{\"windows\":["
        "{\"component\":\"org.example:beta\",\"id\":\"beta-hidden\",\"title\":\"Beta stale\",\"state\":\"hidden\"},"
        "{\"component\":\"org.example:alpha\",\"id\":\"alpha\",\"title\":\"Alpha\",\"state\":\"visible\"},"
        "{\"component\":\"org.example:beta\",\"id\":\"beta\",\"title\":\"Beta\",\"state\":\"visible\"},"
        "{\"component\":\"org.example:gamma\",\"id\":\"gamma\",\"title\":\"Gamma\",\"state\":\"minimized\"},"
        "{\"id\":\"external\",\"title\":\"External\",\"state\":\"hidden\"},"
        "{\"id\":\"external\",\"title\":\"External duplicate\",\"state\":\"hidden\"}"
        "]}",
        tasks,
        MSYS_NATIVE_MAX_TASKS,
        &count
    ));
    CHECK(count == 4u);
    CHECK(strcmp(tasks[0].component, "org.example:alpha") == 0);
    CHECK(strcmp(tasks[1].component, "org.example:beta") == 0);
    CHECK(strcmp(tasks[1].window_id, "beta") == 0);
    CHECK(strcmp(tasks[2].component, "org.example:gamma") == 0);
    CHECK(strcmp(tasks[3].window_id, "external") == 0);
    CHECK(msys_native_apply_task_resources(
        "{\"windows\":["
        "{\"component\":\"org.example:alpha\",\"state\":\"ready\",\"lifecycle\":\"manual\","
        "\"resources\":{\"rss_kib\":12000,\"pss_kib\":9000}},"
        "{\"component\":\"org.example:beta\",\"state\":\"ready\",\"lifecycle\":\"background\","
        "\"resources\":{\"rss_kib\":7000,\"pss_kib\":null}}"
        "]}",
        tasks,
        count
    ));
    CHECK(strcmp(tasks[0].component_state, "ready") == 0);
    CHECK(strcmp(tasks[0].lifecycle, "manual") == 0);
    CHECK(tasks[0].rss_available && tasks[0].rss_kib == 12000u);
    CHECK(tasks[0].pss_available && tasks[0].pss_kib == 9000u);
    CHECK(strcmp(tasks[1].lifecycle, "background") == 0);
    CHECK(tasks[1].rss_available && tasks[1].rss_kib == 7000u);
    CHECK(!tasks[1].pss_available);
    CHECK(!tasks[3].rss_available);
    CHECK(!msys_native_apply_task_resources("{\"windows\":{}}", tasks, count));
    CHECK(!msys_native_parse_apps("{\"apps\":{}}", apps, 1u, &count));
    CHECK(msys_native_json_escape("a\"b\\c\n", escaped, sizeof(escaped)));
    CHECK(strcmp(escaped, "a\\\"b\\\\c\\n") == 0);
    puts("native shell catalog tests passed");
    return 0;
}
