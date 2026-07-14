#ifndef MSYS_SHELL_NATIVE_MODEL_H
#define MSYS_SHELL_NATIVE_MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum msys_native_navigation_action {
    MSYS_NATIVE_NAV_NONE = 0,
    MSYS_NATIVE_NAV_BACK,
    MSYS_NATIVE_NAV_HOME,
    MSYS_NATIVE_NAV_APPS
};

enum msys_native_shell_profile {
    MSYS_NATIVE_PROFILE_EMBEDDED = 0,
    MSYS_NATIVE_PROFILE_MOBILE,
    MSYS_NATIVE_PROFILE_DESKTOP
};

enum msys_native_method_action {
    MSYS_NATIVE_METHOD_UNKNOWN = 0,
    MSYS_NATIVE_METHOD_STATUS,
    MSYS_NATIVE_METHOD_LIST_APPS,
    MSYS_NATIVE_METHOD_GET_PREFERENCES,
    MSYS_NATIVE_METHOD_SET_PREFERENCES,
    MSYS_NATIVE_METHOD_RESET_PREFERENCES,
    MSYS_NATIVE_METHOD_SHOW_RECENTS,
    MSYS_NATIVE_METHOD_SHOW_NOTIFICATION,
    MSYS_NATIVE_METHOD_HIDE_OVERLAYS,
    MSYS_NATIVE_METHOD_NOT_IMPLEMENTED
};

typedef struct msys_native_gesture {
    int active;
    int latched;
    int start_y;
    int current_y;
    uint64_t started_ms;
} msys_native_gesture;

typedef struct msys_native_layout {
    int width;
    int height;
    int bar_height;
    int content_y;
    int content_height;
} msys_native_layout;

typedef struct msys_native_grid_layout {
    int margin;
    int top;
    int gap;
    int columns;
    int cell_width;
    int cell_height;
    int icon_size;
    int viewport_height;
    int content_height;
} msys_native_grid_layout;

typedef struct msys_native_recents_layout {
    int margin;
    int top;
    int gap;
    int columns;
    int card_width;
    int card_height;
    int preview_height;
    int viewport_height;
    int content_height;
} msys_native_recents_layout;

void msys_native_gesture_begin(
    msys_native_gesture *gesture,
    int y,
    uint64_t now_ms
);

enum msys_native_navigation_action msys_native_gesture_motion(
    msys_native_gesture *gesture,
    int y,
    uint64_t now_ms
);

enum msys_native_navigation_action msys_native_gesture_release(
    msys_native_gesture *gesture,
    int y,
    uint64_t now_ms
);

int msys_native_gesture_inward(const msys_native_gesture *gesture);

enum msys_native_navigation_action msys_native_button_action(int x, int width);

/* Resolve horizontal bottom bars and vertical right-edge bars identically. */
enum msys_native_navigation_action msys_native_button_action_at(
    int x,
    int y,
    int width,
    int height
);

/*
 * Return the horizontal centre of one of the three navigation slots.  Keeping
 * this independent of the X11 client geometry lets the shell follow a policy
 * manager which has resized the navigation surface after it was mapped.
 */
int msys_native_navigation_slot_center(int width, int slot);

/*
 * Convert text glyph extents (relative to its baseline) into a baseline which
 * is vertically centred in a surface.  glyph_y is normally negative for an
 * ascender, as returned by Xft/Xlib text extent APIs.
 */
int msys_native_center_baseline(int height, int glyph_y, int glyph_height);

enum msys_native_shell_profile msys_native_profile_resolve(
    const char *preference,
    const char *profile_environment,
    int width,
    int height
);

const char *msys_native_profile_name(enum msys_native_shell_profile profile);

void msys_native_grid_compute(
    msys_native_grid_layout *grid,
    enum msys_native_shell_profile profile,
    int width,
    int height,
    int requested_icon_size,
    size_t item_count
);

void msys_native_recents_compute(
    msys_native_recents_layout *layout,
    enum msys_native_shell_profile profile,
    int width,
    int height,
    int top_inset,
    int right_inset,
    int bottom_inset,
    size_t item_count
);

int msys_native_scroll_clamp(int requested, int content_height, int viewport_height);

int msys_native_grid_hit(
    int x,
    int y,
    int scroll,
    const msys_native_grid_layout *grid,
    size_t item_count,
    size_t *index
);

int msys_native_recents_hit(
    int x,
    int y,
    int scroll,
    const msys_native_recents_layout *layout,
    size_t item_count,
    size_t *index
);

/* Hit-test the right-hand action in the Overview header. */
int msys_native_recents_exit_hit(
    int x,
    int y,
    int width,
    int top_inset,
    int right_inset,
    int header_bottom
);

int msys_native_recents_close_hit(
    int x,
    int y,
    int card_x,
    int card_y,
    const msys_native_recents_layout *layout
);

enum msys_native_method_action msys_native_route_method(
    const char *method,
    int payload_has_message
);

void msys_native_layout_compute(
    msys_native_layout *layout,
    int width,
    int height
);

#ifdef __cplusplus
}
#endif

#endif
