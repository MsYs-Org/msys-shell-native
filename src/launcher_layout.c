#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/launcher_layout.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LAYOUT_FILE "launcher-layout.v1"
#define LAYOUT_MAGIC "MSYS-LAUNCHER-1"
#define LAYOUT_FILE_LIMIT 32768u

static int app_available(
    const msys_native_app *apps,
    size_t count,
    const char *component
)
{
    size_t index;
    for (index = 0u; index < count; index++) {
        if (strcmp(apps[index].component, component) == 0) return 1;
    }
    return 0;
}

static int component_present(
    const msys_native_launcher_layout *layout,
    const char *component
)
{
    size_t item;
    for (item = 0u; item < layout->count; item++) {
        size_t member;
        if (
            layout->items[item].kind == MSYS_NATIVE_LAUNCHER_APP &&
            strcmp(layout->items[item].id, component) == 0
        ) return 1;
        for (member = 0u; member < layout->items[item].member_count; member++) {
            if (strcmp(layout->items[item].members[member], component) == 0) return 1;
        }
    }
    return 0;
}

void msys_native_launcher_layout_init(msys_native_launcher_layout *layout)
{
    if (layout == NULL) return;
    memset(layout, 0, sizeof(*layout));
    layout->next_folder_id = 1u;
}

static int safe_text(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    if (value == NULL || *value == '\0') return 0;
    while (*cursor != '\0') {
        if (*cursor < 0x20u || *cursor == 0x7fu) return 0;
        cursor++;
    }
    return 1;
}

static int encode(const char *input, char *output, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0u;
    const unsigned char *cursor = (const unsigned char *)input;
    if (input == NULL || output == NULL || capacity == 0u) return 0;
    while (*cursor != '\0') {
        int plain = (*cursor >= 0x20u && *cursor != '%' && *cursor != '\t' &&
            *cursor != '\r' && *cursor != '\n');
        if (plain != 0) {
            if (used + 2u > capacity) return 0;
            output[used++] = (char)*cursor;
        } else {
            if (used + 4u > capacity) return 0;
            output[used++] = '%';
            output[used++] = hex[*cursor >> 4u];
            output[used++] = hex[*cursor & 15u];
        }
        cursor++;
    }
    output[used] = '\0';
    return 1;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static int decode(const char *input, char *output, size_t capacity)
{
    size_t used = 0u;
    while (*input != '\0') {
        unsigned char value = (unsigned char)*input++;
        if (value == '%') {
            int high = hex_value(input[0]);
            int low = hex_value(input[1]);
            if (high < 0 || low < 0) return 0;
            value = (unsigned char)((high << 4u) | low);
            input += 2;
        }
        if (value == '\0' || used + 1u >= capacity) return 0;
        output[used++] = (char)value;
    }
    output[used] = '\0';
    return safe_text(output);
}

static char *next_field(char **cursor)
{
    char *field;
    char *separator;
    if (cursor == NULL || *cursor == NULL) return NULL;
    field = *cursor;
    separator = strchr(field, '\t');
    if (separator != NULL) {
        *separator = '\0';
        *cursor = separator + 1;
    } else {
        *cursor = NULL;
    }
    return field;
}

static int parse_unsigned(const char *value, unsigned int *result)
{
    char *end = NULL;
    unsigned long parsed;
    if (value == NULL || *value == '\0') return 0;
    errno = 0;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > 65535u) return 0;
    *result = (unsigned int)parsed;
    return 1;
}

static int parse_u64(const char *value, uint64_t *result)
{
    char *end = NULL;
    unsigned long long parsed;
    if (value == NULL || *value == '\0') return 0;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || *end != '\0') return 0;
    *result = (uint64_t)parsed;
    return 1;
}

static int parse_line(msys_native_launcher_layout *layout, char *line)
{
    char *cursor = line;
    char *kind = next_field(&cursor);
    char *page_text = next_field(&cursor);
    msys_native_launcher_item item;
    unsigned int page;
    char *field;
    if (
        layout->count >= MSYS_NATIVE_LAUNCHER_MAX_ITEMS ||
        kind == NULL || page_text == NULL || !parse_unsigned(page_text, &page)
    ) return 0;
    memset(&item, 0, sizeof(item));
    item.page = page;
    field = next_field(&cursor);
    if (field == NULL || !decode(field, item.id, sizeof(item.id))) return 0;
    if (strcmp(kind, "A") == 0) {
        if (cursor != NULL || strchr(item.id, ':') == NULL) return 0;
        item.kind = MSYS_NATIVE_LAUNCHER_APP;
    } else if (strcmp(kind, "F") == 0 || strcmp(kind, "B") == 0) {
        field = next_field(&cursor);
        if (field == NULL || !decode(field, item.name, sizeof(item.name))) return 0;
        item.kind = MSYS_NATIVE_LAUNCHER_FOLDER;
        item.large = strcmp(kind, "B") == 0;
        while ((field = next_field(&cursor)) != NULL) {
            if (
                item.member_count >= MSYS_NATIVE_LAUNCHER_MAX_FOLDER_MEMBERS ||
                !decode(
                    field,
                    item.members[item.member_count],
                    sizeof(item.members[item.member_count])
                ) || strchr(item.members[item.member_count], ':') == NULL
            ) return 0;
            item.member_count++;
        }
        if (item.member_count == 0u) return 0;
    } else {
        return 0;
    }
    layout->items[layout->count++] = item;
    return 1;
}

int msys_native_launcher_layout_load(
    msys_native_launcher_layout *layout,
    const char *state_directory
)
{
    char *buffer;
    FILE *stream;
    size_t length;
    char *line;
    char *save = NULL;
    uint64_t revision;
    unsigned int next_folder;
    if (layout == NULL) return 0;
    msys_native_launcher_layout_init(layout);
    if (state_directory == NULL || state_directory[0] != '/') return 1;
    if (snprintf(layout->path, sizeof(layout->path), "%s/%s", state_directory, LAYOUT_FILE) >=
        (int)sizeof(layout->path)) {
        layout->path[0] = '\0';
        return 0;
    }
    stream = fopen(layout->path, "rb");
    if (stream == NULL) return errno == ENOENT;
    buffer = (char *)malloc(LAYOUT_FILE_LIMIT + 1u);
    if (buffer == NULL) {
        (void)fclose(stream);
        return 0;
    }
    length = fread(buffer, 1u, LAYOUT_FILE_LIMIT + 1u, stream);
    if (fclose(stream) != 0 || length > LAYOUT_FILE_LIMIT) {
        free(buffer);
        return 0;
    }
    buffer[length] = '\0';
    line = strtok_r(buffer, "\n", &save);
    if (line == NULL) {
        free(buffer);
        return 0;
    }
    {
        char *cursor = line;
        char *magic = next_field(&cursor);
        char *revision_text = next_field(&cursor);
        char *next_text = next_field(&cursor);
        if (
            magic == NULL || strcmp(magic, LAYOUT_MAGIC) != 0 ||
            revision_text == NULL || next_text == NULL || cursor != NULL ||
            !parse_u64(revision_text, &revision) ||
            !parse_unsigned(next_text, &next_folder)
        ) {
            free(buffer);
            return 0;
        }
    }
    layout->revision = revision;
    layout->next_folder_id = next_folder > 0u ? next_folder : 1u;
    while ((line = strtok_r(NULL, "\n", &save)) != NULL) {
        size_t line_length = strlen(line);
        if (line_length > 0u && line[line_length - 1u] == '\r') line[--line_length] = '\0';
        if (line_length > 0u && !parse_line(layout, line)) {
            free(buffer);
            msys_native_launcher_layout_init(layout);
            return 0;
        }
    }
    free(buffer);
    return 1;
}

static int write_all(int fd, const char *data, size_t length)
{
    while (length > 0u) {
        ssize_t written = write(fd, data, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return 0;
        data += (size_t)written;
        length -= (size_t)written;
    }
    return 1;
}

static int write_field(int fd, const char *value)
{
    char encoded[MSYS_NATIVE_COMPONENT_CAPACITY * 3u];
    return encode(value, encoded, sizeof(encoded)) &&
        write_all(fd, encoded, strlen(encoded));
}

int msys_native_launcher_layout_commit(msys_native_launcher_layout *layout)
{
    char temporary[MSYS_NATIVE_PATH_CAPACITY + 40u];
    char header[96];
    size_t index;
    int fd;
    int ok;
    if (layout == NULL || layout->path[0] != '/') return 1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", layout->path, (long)getpid()) >=
        (int)sizeof(temporary)) return 0;
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return 0;
    layout->revision++;
    (void)snprintf(
        header, sizeof(header), "%s\t%llu\t%u\n",
        LAYOUT_MAGIC, (unsigned long long)layout->revision,
        layout->next_folder_id
    );
    ok = write_all(fd, header, strlen(header));
    for (index = 0u; ok != 0 && index < layout->count; index++) {
        const msys_native_launcher_item *item = &layout->items[index];
        char prefix[32];
        size_t member;
        (void)snprintf(
            prefix, sizeof(prefix), "%c\t%u\t",
            item->kind == MSYS_NATIVE_LAUNCHER_FOLDER
                ? (item->large != 0 ? 'B' : 'F') : 'A',
            item->page
        );
        ok = write_all(fd, prefix, strlen(prefix)) && write_field(fd, item->id);
        if (ok != 0 && item->kind == MSYS_NATIVE_LAUNCHER_FOLDER) {
            ok = write_all(fd, "\t", 1u) && write_field(fd, item->name);
            for (member = 0u; ok != 0 && member < item->member_count; member++) {
                ok = write_all(fd, "\t", 1u) && write_field(fd, item->members[member]);
            }
        }
        if (ok != 0) ok = write_all(fd, "\n", 1u);
    }
    if (ok != 0) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok != 0 && rename(temporary, layout->path) != 0) ok = 0;
    if (ok == 0) {
        layout->revision--;
        (void)unlink(temporary);
    }
    return ok;
}

unsigned int msys_native_launcher_page_count(
    const msys_native_launcher_layout *layout
)
{
    size_t index;
    unsigned int pages = 1u;
    if (layout == NULL) return pages;
    for (index = 0u; index < layout->count; index++) {
        if (layout->items[index].page + 1u > pages) pages = layout->items[index].page + 1u;
    }
    return pages;
}

size_t msys_native_launcher_page_items(
    const msys_native_launcher_layout *layout,
    unsigned int page,
    size_t *indices,
    size_t capacity
)
{
    size_t index;
    size_t used = 0u;
    if (layout == NULL) return 0u;
    for (index = 0u; index < layout->count; index++) {
        if (layout->items[index].page != page) continue;
        if (used < capacity && indices != NULL) indices[used] = index;
        used++;
    }
    return used;
}

void msys_native_launcher_compact_pages(
    msys_native_launcher_layout *layout,
    size_t page_capacity
)
{
    unsigned int old_page;
    unsigned int new_page = 0u;
    unsigned int pages;
    msys_native_launcher_item ordered[MSYS_NATIVE_LAUNCHER_MAX_ITEMS];
    size_t used = 0u;
    if (layout == NULL) return;
    if (page_capacity == 0u) page_capacity = 1u;
    pages = msys_native_launcher_page_count(layout);
    for (old_page = 0u; old_page < pages; old_page++) {
        size_t index;
        size_t on_page = 0u;
        for (index = 0u; index < layout->count; index++) {
            if (layout->items[index].page == old_page) on_page++;
        }
        if (on_page == 0u) continue;
        on_page = 0u;
        for (index = 0u; index < layout->count; index++) {
            if (layout->items[index].page != old_page) continue;
            ordered[used] = layout->items[index];
            ordered[used].page = new_page +
                (unsigned int)(on_page / page_capacity);
            used++;
            on_page++;
        }
        new_page += (unsigned int)((on_page + page_capacity - 1u) /
            page_capacity);
    }
    memcpy(layout->items, ordered, used * sizeof(ordered[0]));
    layout->count = used;
}

int msys_native_launcher_layout_reconcile(
    msys_native_launcher_layout *layout,
    const msys_native_app *apps,
    size_t app_count,
    size_t page_capacity
)
{
    size_t item = 0u;
    int changed = 0;
    if (layout == NULL || (apps == NULL && app_count != 0u)) return 0;
    while (item < layout->count) {
        msys_native_launcher_item *entry = &layout->items[item];
        if (entry->kind == MSYS_NATIVE_LAUNCHER_APP) {
            if (app_available(apps, app_count, entry->id)) {
                item++;
                continue;
            }
        } else {
            size_t member = 0u;
            while (member < entry->member_count) {
                if (app_available(apps, app_count, entry->members[member])) {
                    member++;
                } else {
                    memmove(
                        &entry->members[member],
                        &entry->members[member + 1u],
                        (entry->member_count - member - 1u) * sizeof(entry->members[0])
                    );
                    entry->member_count--;
                    changed = 1;
                }
            }
            if (entry->member_count > 0u) {
                item++;
                continue;
            }
        }
        memmove(
            &layout->items[item],
            &layout->items[item + 1u],
            (layout->count - item - 1u) * sizeof(layout->items[0])
        );
        layout->count--;
        changed = 1;
    }
    for (item = 0u; item < app_count && layout->count < MSYS_NATIVE_LAUNCHER_MAX_ITEMS; item++) {
        if (!component_present(layout, apps[item].component)) {
            msys_native_launcher_item *entry = &layout->items[layout->count];
            size_t capacity = page_capacity > 0u ? page_capacity : 1u;
            memset(entry, 0, sizeof(*entry));
            entry->kind = MSYS_NATIVE_LAUNCHER_APP;
            entry->page = layout->count == 0u ? 0u :
                (unsigned int)(layout->count / capacity);
            (void)snprintf(entry->id, sizeof(entry->id), "%s", apps[item].component);
            layout->count++;
            changed = 1;
        }
    }
    msys_native_launcher_compact_pages(layout, page_capacity);
    return changed;
}

int msys_native_launcher_find_app(
    const msys_native_launcher_layout *layout,
    const char *component,
    size_t *item_index,
    size_t *member_index
)
{
    size_t item;
    if (layout == NULL || component == NULL) return 0;
    for (item = 0u; item < layout->count; item++) {
        size_t member;
        if (
            layout->items[item].kind == MSYS_NATIVE_LAUNCHER_APP &&
            strcmp(layout->items[item].id, component) == 0
        ) {
            if (item_index != NULL) *item_index = item;
            if (member_index != NULL) *member_index = 0u;
            return 1;
        }
        for (member = 0u; member < layout->items[item].member_count; member++) {
            if (strcmp(layout->items[item].members[member], component) == 0) {
                if (item_index != NULL) *item_index = item;
                if (member_index != NULL) *member_index = member;
                return 1;
            }
        }
    }
    return 0;
}

static size_t page_insert_index(
    const msys_native_launcher_layout *layout,
    unsigned int page,
    size_t position
)
{
    size_t index;
    size_t ordinal = 0u;
    for (index = 0u; index < layout->count; index++) {
        if (layout->items[index].page > page) return index;
        if (layout->items[index].page == page) {
            if (ordinal == position) return index;
            ordinal++;
        }
    }
    return layout->count;
}

int msys_native_launcher_move(
    msys_native_launcher_layout *layout,
    size_t source,
    unsigned int target_page,
    size_t target_position,
    size_t page_capacity,
    size_t *new_index
)
{
    msys_native_launcher_item moved;
    size_t insertion;
    size_t on_page;
    if (layout == NULL || source >= layout->count || page_capacity == 0u) return 0;
    if (target_page > msys_native_launcher_page_count(layout)) return 0;
    on_page = msys_native_launcher_page_items(layout, target_page, NULL, 0u);
    if (target_position > on_page) target_position = on_page;
    moved = layout->items[source];
    memmove(
        &layout->items[source],
        &layout->items[source + 1u],
        (layout->count - source - 1u) * sizeof(layout->items[0])
    );
    layout->count--;
    moved.page = target_page;
    insertion = page_insert_index(layout, target_page, target_position);
    memmove(
        &layout->items[insertion + 1u],
        &layout->items[insertion],
        (layout->count - insertion) * sizeof(layout->items[0])
    );
    layout->items[insertion] = moved;
    layout->count++;
    msys_native_launcher_compact_pages(layout, page_capacity);
    if (new_index != NULL) {
        size_t index;
        *new_index = 0u;
        for (index = 0u; index < layout->count; index++) {
            if (strcmp(layout->items[index].id, moved.id) == 0) {
                *new_index = index;
                break;
            }
        }
    }
    return 1;
}

int msys_native_launcher_swap(
    msys_native_launcher_layout *layout,
    size_t first,
    size_t second
)
{
    msys_native_launcher_item item;
    if (layout == NULL || first >= layout->count || second >= layout->count) return 0;
    if (first == second) return 1;
    item = layout->items[first];
    layout->items[first] = layout->items[second];
    layout->items[second] = item;
    return 1;
}

int msys_native_launcher_make_folder(
    msys_native_launcher_layout *layout,
    size_t source,
    size_t target,
    const char *default_name,
    size_t page_capacity,
    size_t *folder_index
)
{
    msys_native_launcher_item folder;
    size_t low;
    size_t high;
    if (
        layout == NULL || source >= layout->count || target >= layout->count ||
        source == target || layout->items[source].kind != MSYS_NATIVE_LAUNCHER_APP ||
        layout->items[target].kind != MSYS_NATIVE_LAUNCHER_APP ||
        !safe_text(default_name)
    ) return 0;
    memset(&folder, 0, sizeof(folder));
    folder.kind = MSYS_NATIVE_LAUNCHER_FOLDER;
    folder.page = layout->items[target].page;
    (void)snprintf(folder.id, sizeof(folder.id), "folder-%u", layout->next_folder_id++);
    (void)snprintf(folder.name, sizeof(folder.name), "%s", default_name);
    (void)snprintf(folder.members[0], sizeof(folder.members[0]), "%s", layout->items[target].id);
    (void)snprintf(folder.members[1], sizeof(folder.members[1]), "%s", layout->items[source].id);
    folder.member_count = 2u;
    low = source < target ? source : target;
    high = source < target ? target : source;
    memmove(
        &layout->items[high], &layout->items[high + 1u],
        (layout->count - high - 1u) * sizeof(layout->items[0])
    );
    layout->count--;
    memmove(
        &layout->items[low], &layout->items[low + 1u],
        (layout->count - low - 1u) * sizeof(layout->items[0])
    );
    layout->count--;
    memmove(
        &layout->items[low + 1u], &layout->items[low],
        (layout->count - low) * sizeof(layout->items[0])
    );
    layout->items[low] = folder;
    layout->count++;
    msys_native_launcher_compact_pages(layout, page_capacity);
    if (folder_index != NULL) *folder_index = low;
    return 1;
}

int msys_native_launcher_add_to_folder(
    msys_native_launcher_layout *layout,
    size_t source,
    size_t folder,
    size_t page_capacity,
    size_t *folder_index
)
{
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    char folder_id[MSYS_NATIVE_LAUNCHER_FOLDER_ID_CAPACITY];
    size_t index;
    if (
        layout == NULL || source >= layout->count || folder >= layout->count ||
        layout->items[source].kind != MSYS_NATIVE_LAUNCHER_APP ||
        layout->items[folder].kind != MSYS_NATIVE_LAUNCHER_FOLDER ||
        layout->items[folder].member_count >= MSYS_NATIVE_LAUNCHER_MAX_FOLDER_MEMBERS
    ) return 0;
    (void)snprintf(component, sizeof(component), "%s", layout->items[source].id);
    (void)snprintf(folder_id, sizeof(folder_id), "%s", layout->items[folder].id);
    (void)snprintf(
        layout->items[folder].members[layout->items[folder].member_count++],
        sizeof(layout->items[folder].members[0]), "%s", component
    );
    memmove(
        &layout->items[source], &layout->items[source + 1u],
        (layout->count - source - 1u) * sizeof(layout->items[0])
    );
    layout->count--;
    msys_native_launcher_compact_pages(layout, page_capacity);
    for (index = 0u; index < layout->count; index++) {
        if (strcmp(layout->items[index].id, folder_id) == 0) {
            if (folder_index != NULL) *folder_index = index;
            return 1;
        }
    }
    return 0;
}

int msys_native_launcher_rename_folder(
    msys_native_launcher_layout *layout,
    const char *folder_id,
    const char *name
)
{
    size_t index;
    if (layout == NULL || folder_id == NULL || !safe_text(name)) return 0;
    if (strlen(name) >= MSYS_NATIVE_NAME_CAPACITY) return 0;
    for (index = 0u; index < layout->count; index++) {
        if (
            layout->items[index].kind == MSYS_NATIVE_LAUNCHER_FOLDER &&
            strcmp(layout->items[index].id, folder_id) == 0
        ) {
            (void)snprintf(layout->items[index].name, sizeof(layout->items[index].name), "%s", name);
            return 1;
        }
    }
    return 0;
}
