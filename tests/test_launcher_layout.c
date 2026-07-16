#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/launcher_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static void app(msys_native_app *value, const char *component)
{
    memset(value, 0, sizeof(*value));
    (void)snprintf(value->component, sizeof(value->component), "%s", component);
}

int main(void)
{
    char directory[] = "/tmp/msys-launcher-layout-XXXXXX";
    char path[MSYS_NATIVE_PATH_CAPACITY];
    msys_native_launcher_layout layout;
    msys_native_launcher_layout loaded;
    msys_native_app apps[5];
    size_t index;
    size_t member;
    size_t page[4];
    size_t folder;
    int changed;
    CHECK(mkdtemp(directory) != NULL);
    app(&apps[0], "org.example:one");
    app(&apps[1], "org.example:two");
    app(&apps[2], "org.example:three");
    app(&apps[3], "org.example:four");
    app(&apps[4], "org.example:five");
    msys_native_launcher_layout_init(&layout);
    CHECK(msys_native_launcher_layout_load(&layout, directory));
    changed = msys_native_launcher_layout_reconcile(&layout, apps, 5u, 4u);
    CHECK(changed && layout.count == 5u);
    CHECK(msys_native_launcher_page_count(&layout) == 2u);
    CHECK(msys_native_launcher_page_items(&layout, 0u, page, 4u) == 4u);
    CHECK(msys_native_launcher_page_items(&layout, 1u, page, 4u) == 1u);
    {
        msys_native_launcher_layout overflow = layout;
        size_t position;
        for (position = 0u; position < overflow.count; position++) {
            overflow.items[position].page = 0u;
        }
        /* One overflowing explicit page is split into as many bounded pages
         * as needed, rather than moving every overflow item to page one. */
        msys_native_launcher_compact_pages(&overflow, 2u);
        CHECK(msys_native_launcher_page_count(&overflow) == 3u);
        CHECK(msys_native_launcher_page_items(&overflow, 0u, page, 4u) == 2u);
        CHECK(msys_native_launcher_page_items(&overflow, 1u, page, 4u) == 2u);
        CHECK(msys_native_launcher_page_items(&overflow, 2u, page, 4u) == 1u);
        overflow.items[0].page = 0u;
        for (position = 1u; position < overflow.count; position++) {
            overflow.items[position].page = 2u;
        }
        msys_native_launcher_compact_pages(&overflow, 4u);
        CHECK(msys_native_launcher_page_count(&overflow) == 2u);
        CHECK(msys_native_launcher_page_items(&overflow, 1u, page, 4u) == 4u);
    }
    CHECK(msys_native_launcher_move(&layout, 4u, 0u, 1u, 4u, &index));
    CHECK(index == 1u && strcmp(layout.items[1].id, "org.example:five") == 0);
    CHECK(msys_native_launcher_swap(&layout, 0u, 1u));
    CHECK(strcmp(layout.items[0].id, "org.example:five") == 0);
    CHECK(msys_native_launcher_swap(&layout, 0u, 1u));
    CHECK(msys_native_launcher_make_folder(
        &layout, 1u, 2u, "Tools 工具", 4u, &folder
    ));
    CHECK(layout.items[folder].kind == MSYS_NATIVE_LAUNCHER_FOLDER);
    CHECK(layout.items[folder].large == 0);
    CHECK(layout.items[folder].member_count == 2u);
    CHECK(msys_native_launcher_rename_folder(
        &layout, layout.items[folder].id, "常用"
    ));
    CHECK(msys_native_launcher_find_app(
        &layout, "org.example:five", &index, &member
    ));
    CHECK(index == folder && member == 1u);
    CHECK(msys_native_launcher_add_to_folder(
        &layout, layout.count - 1u, folder, 4u, &folder
    ));
    CHECK(layout.items[folder].member_count == 3u);
    layout.items[folder].large = 1;
    CHECK(msys_native_launcher_layout_commit(&layout));
    msys_native_launcher_layout_init(&loaded);
    CHECK(msys_native_launcher_layout_load(&loaded, directory));
    CHECK(loaded.count == layout.count);
    CHECK(loaded.items[folder].large == 1);
    CHECK(strcmp(loaded.items[folder].name, "常用") == 0);
    CHECK(loaded.items[folder].member_count == 3u);
    CHECK(msys_native_launcher_layout_reconcile(&loaded, apps, 2u, 4u));
    CHECK(loaded.count <= layout.count);
    (void)snprintf(path, sizeof(path), "%s/launcher-layout.v1", directory);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("native launcher layout tests passed");
    return 0;
}
