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
    char escaped[64];
    CHECK(msys_native_parse_apps(
        "{\"apps\":[{\"id\":\"org.example:alpha\",\"name\":\"Alpha\",\"summary\":\"First\",\"package_root\":\"/opt/msys/packages/alpha\",\"icons\":[{\"path\":\"@package/files/icon.ppm\"}]},{\"id\":\"bad\",\"name\":\"skip\"},{\"id\":\"org.example:beta\"}]}",
        apps,
        MSYS_NATIVE_MAX_APPS,
        &count
    ));
    CHECK(count == 2u);
    CHECK(strcmp(apps[0].component, "org.example:alpha") == 0);
    CHECK(strcmp(apps[0].summary, "First") == 0);
    CHECK(strcmp(apps[0].icon_path, "/opt/msys/packages/alpha/files/icon.ppm") == 0);
    CHECK(strcmp(apps[1].name, "org.example:beta") == 0);
    CHECK(msys_native_parse_tasks(
        "{\"schema\":\"msys.window-list.v1\",\"windows\":[{\"component\":\"org.example:alpha\",\"title\":\"Alpha\",\"kind\":\"application\",\"role\":\"application\",\"native_id\":\"0x2a\",\"thumbnail\":\"/tmp/alpha.ppm\"},{\"component\":null,\"identity\":null,\"id\":\"msys.x11-window.v1:7:1\",\"title\":\"External\"},{\"id\":\"msys.x11-window.v1:8:1\",\"title\":\"Chrome\",\"kind\":\"overlay\"},{\"title\":\"skip\"}]}",
        tasks,
        MSYS_NATIVE_MAX_TASKS,
        &count
    ));
    CHECK(count == 2u);
    CHECK(strcmp(tasks[1].window_id, "msys.x11-window.v1:7:1") == 0);
    CHECK(strcmp(tasks[0].thumbnail, "/tmp/alpha.ppm") == 0);
    CHECK(strcmp(tasks[0].native_id, "0x2a") == 0);
    CHECK(!msys_native_parse_apps("{\"apps\":{}}", apps, 1u, &count));
    CHECK(msys_native_json_escape("a\"b\\c\n", escaped, sizeof(escaped)));
    CHECK(strcmp(escaped, "a\\\"b\\\\c\\n") == 0);
    puts("native shell catalog tests passed");
    return 0;
}
