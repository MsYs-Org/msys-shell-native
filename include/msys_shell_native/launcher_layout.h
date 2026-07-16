#ifndef MSYS_SHELL_NATIVE_LAUNCHER_LAYOUT_H
#define MSYS_SHELL_NATIVE_LAUNCHER_LAYOUT_H

#include "msys_shell_native/catalog.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSYS_NATIVE_LAUNCHER_MAX_ITEMS MSYS_NATIVE_MAX_APPS
#define MSYS_NATIVE_LAUNCHER_MAX_FOLDER_MEMBERS 12u
#define MSYS_NATIVE_LAUNCHER_FOLDER_ID_CAPACITY 40u

enum msys_native_launcher_item_kind {
    MSYS_NATIVE_LAUNCHER_APP = 1,
    MSYS_NATIVE_LAUNCHER_FOLDER = 2
};

typedef struct msys_native_launcher_item {
    enum msys_native_launcher_item_kind kind;
    unsigned int page;
    char id[MSYS_NATIVE_COMPONENT_CAPACITY];
    char name[MSYS_NATIVE_NAME_CAPACITY];
    size_t member_count;
    char members[MSYS_NATIVE_LAUNCHER_MAX_FOLDER_MEMBERS]
        [MSYS_NATIVE_COMPONENT_CAPACITY];
} msys_native_launcher_item;

typedef struct msys_native_launcher_layout {
    uint64_t revision;
    unsigned int next_folder_id;
    msys_native_launcher_item items[MSYS_NATIVE_LAUNCHER_MAX_ITEMS];
    size_t count;
    char path[MSYS_NATIVE_PATH_CAPACITY];
} msys_native_launcher_layout;

void msys_native_launcher_layout_init(msys_native_launcher_layout *layout);

/* State is a bounded, percent-escaped line format and is replaced atomically. */
int msys_native_launcher_layout_load(
    msys_native_launcher_layout *layout,
    const char *state_directory
);

int msys_native_launcher_layout_commit(
    msys_native_launcher_layout *layout
);

/* Preserve user order/folders, remove unavailable members, and append new apps. */
int msys_native_launcher_layout_reconcile(
    msys_native_launcher_layout *layout,
    const msys_native_app *apps,
    size_t app_count,
    size_t page_capacity
);

unsigned int msys_native_launcher_page_count(
    const msys_native_launcher_layout *layout
);

size_t msys_native_launcher_page_items(
    const msys_native_launcher_layout *layout,
    unsigned int page,
    size_t *indices,
    size_t capacity
);

int msys_native_launcher_find_app(
    const msys_native_launcher_layout *layout,
    const char *component,
    size_t *item_index,
    size_t *member_index
);

int msys_native_launcher_move(
    msys_native_launcher_layout *layout,
    size_t source,
    unsigned int target_page,
    size_t target_position,
    size_t page_capacity,
    size_t *new_index
);

int msys_native_launcher_swap(
    msys_native_launcher_layout *layout,
    size_t first,
    size_t second
);

int msys_native_launcher_make_folder(
    msys_native_launcher_layout *layout,
    size_t source,
    size_t target,
    const char *default_name,
    size_t page_capacity,
    size_t *folder_index
);

int msys_native_launcher_add_to_folder(
    msys_native_launcher_layout *layout,
    size_t source,
    size_t folder,
    size_t page_capacity,
    size_t *folder_index
);

int msys_native_launcher_rename_folder(
    msys_native_launcher_layout *layout,
    const char *folder_id,
    const char *name
);

/* Remove empty page numbers while retaining page and item order. */
void msys_native_launcher_compact_pages(
    msys_native_launcher_layout *layout,
    size_t page_capacity
);

#ifdef __cplusplus
}
#endif

#endif
