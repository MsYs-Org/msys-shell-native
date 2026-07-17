#include "msys_shell_native/model.h"

#include <stddef.h>
#include <string.h>

#define RECENTS_DISTANCE_PX 28
#define RECENTS_HOLD_MS 420u
#define BACK_DISTANCE_PX 10

static int inward_distance(int start_y, int current_y)
{
    int distance = start_y - current_y;
    return distance > 0 ? distance : 0;
}

void msys_native_gesture_begin(
    msys_native_gesture *gesture,
    int y,
    uint64_t now_ms
)
{
    if (gesture == NULL) {
        return;
    }
    gesture->active = 1;
    gesture->latched = 0;
    gesture->start_y = y;
    gesture->current_y = y;
    gesture->started_ms = now_ms;
}

int msys_native_gesture_inward(const msys_native_gesture *gesture)
{
    if (gesture == NULL || gesture->active == 0) {
        return 0;
    }
    return inward_distance(gesture->start_y, gesture->current_y);
}

enum msys_native_navigation_action msys_native_button_action(int x, int width)
{
    int bounded_x;
    int index;
    if (width <= 0) {
        return MSYS_NATIVE_NAV_NONE;
    }
    bounded_x = x < 0 ? 0 : (x >= width ? width - 1 : x);
    index = bounded_x * 3 / width;
    if (index == 0) {
        return MSYS_NATIVE_NAV_BACK;
    }
    return index == 1 ? MSYS_NATIVE_NAV_HOME : MSYS_NATIVE_NAV_APPS;
}

enum msys_native_navigation_action msys_native_button_action_at(
    int x,
    int y,
    int width,
    int height
)
{
    if (height > width * 2) {
        return msys_native_button_action(y, height);
    }
    return msys_native_button_action(x, width);
}

enum msys_native_navigation_action msys_native_button_release_action_at(
    enum msys_native_navigation_action pressed,
    int x,
    int y,
    int width,
    int height,
    int slop
)
{
    enum msys_native_navigation_action released;
    int bounded_x;
    int bounded_y;
    if (
        pressed == MSYS_NATIVE_NAV_NONE || width <= 0 || height <= 0 || slop < 0 ||
        x < -slop || y < -slop || x >= width + slop || y >= height + slop
    ) {
        return MSYS_NATIVE_NAV_NONE;
    }
    bounded_x = x < 0 ? 0 : (x >= width ? width - 1 : x);
    bounded_y = y < 0 ? 0 : (y >= height ? height - 1 : y);
    released = msys_native_button_action_at(
        bounded_x, bounded_y, width, height
    );
    return released == pressed ? released : MSYS_NATIVE_NAV_NONE;
}

int msys_native_navigation_slot_center(int width, int slot)
{
    int factor;
    if (width <= 0 || slot < 0 || slot > 2) {
        return 0;
    }
    factor = slot * 2 + 1;
    return (width / 6) * factor + (width % 6) * factor / 6;
}

int msys_native_center_baseline(int height, int glyph_y, int glyph_height)
{
    int bounded_height = height > 0 ? height : 1;
    int bounded_glyph_height = glyph_height > 0 ? glyph_height : 1;
    return (bounded_height - bounded_glyph_height) / 2 - glyph_y;
}

int msys_native_wifi_signal_level(
    int connected,
    int signal_known,
    int signal_dbm
)
{
    if (connected == 0) return 0;
    if (signal_known == 0) return 1;
    if (signal_dbm >= -55) return 3;
    return signal_dbm >= -70 ? 2 : 1;
}

static int text_equal(const char *left, const char *right)
{
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

enum msys_native_shell_profile msys_native_profile_resolve(
    const char *preference,
    const char *profile_environment,
    int width,
    int height
)
{
    const char *selected = preference;
    if (
        selected == NULL || *selected == '\0' || text_equal(selected, "profile") ||
        text_equal(selected, "auto")
    ) {
        selected = profile_environment;
    }
    if (text_equal(selected, "desktop")) {
        return MSYS_NATIVE_PROFILE_DESKTOP;
    }
    if (text_equal(selected, "embedded") || text_equal(selected, "kiosk")) {
        return MSYS_NATIVE_PROFILE_EMBEDDED;
    }
    if (text_equal(selected, "mobile")) {
        return MSYS_NATIVE_PROFILE_MOBILE;
    }
    return width >= 900 && height >= 600
        ? MSYS_NATIVE_PROFILE_DESKTOP : MSYS_NATIVE_PROFILE_MOBILE;
}

const char *msys_native_profile_name(enum msys_native_shell_profile profile)
{
    if (profile == MSYS_NATIVE_PROFILE_DESKTOP) {
        return "desktop";
    }
    if (profile == MSYS_NATIVE_PROFILE_EMBEDDED) {
        return "embedded";
    }
    return "mobile";
}

static int bounded(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    return value > maximum ? maximum : value;
}

void msys_native_grid_compute(
    msys_native_grid_layout *grid,
    enum msys_native_shell_profile profile,
    int width,
    int height,
    int requested_icon_size,
    size_t item_count
)
{
    int available;
    int target;
    int rows;
    if (grid == NULL) return;
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    grid->margin = profile == MSYS_NATIVE_PROFILE_DESKTOP ? 24 : 14;
    grid->top = profile == MSYS_NATIVE_PROFILE_DESKTOP ? 62 : 54;
    grid->gap = profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 8 : 12;
    grid->icon_size = bounded(requested_icon_size, 40, 96);
    target = profile == MSYS_NATIVE_PROFILE_DESKTOP
        ? 128 : (profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 112 : 88);
    available = width - grid->margin * 2;
    if (available < 1) available = 1;
    grid->columns = (available + grid->gap) / (target + grid->gap);
    grid->columns = bounded(grid->columns, 1, profile == MSYS_NATIVE_PROFILE_DESKTOP ? 8 : 5);
    grid->cell_width = (available - grid->gap * (grid->columns - 1)) / grid->columns;
    if (grid->cell_width < 1) grid->cell_width = 1;
    grid->cell_height = grid->icon_size + (profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 36 : 46);
    grid->viewport_height = height - grid->top - grid->margin;
    if (grid->viewport_height < 1) grid->viewport_height = 1;
    rows = item_count == 0u
        ? 0 : ((int)item_count + grid->columns - 1) / grid->columns;
    grid->content_height = rows > 0
        ? rows * grid->cell_height + (rows - 1) * grid->gap : 0;
    grid->rows = rows;
    grid->page_capacity = rows * grid->columns;
    grid->page_count = grid->page_capacity > 0
        ? ((int)item_count + grid->page_capacity - 1) / grid->page_capacity : 1;
    if (grid->page_count < 1) grid->page_count = 1;
    grid->indicator_y = grid->top + grid->viewport_height + 4;
}

void msys_native_launcher_grid_compute(
    msys_native_grid_layout *grid,
    enum msys_native_shell_profile profile,
    int width,
    int height,
    int requested_icon_size,
    int requested_columns,
    int requested_rows,
    size_t item_count
)
{
    int available_width;
    int available_height;
    int target;
    if (grid == NULL) return;
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    grid->margin = profile == MSYS_NATIVE_PROFILE_DESKTOP ? 24 : 14;
    grid->top = profile == MSYS_NATIVE_PROFILE_DESKTOP ? 62 : 54;
    grid->gap = profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 8 : 12;
    grid->icon_size = bounded(requested_icon_size, 40, 96);
    available_width = width - grid->margin * 2;
    if (available_width < 1) available_width = 1;
    target = profile == MSYS_NATIVE_PROFILE_DESKTOP
        ? 128 : (profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 112 : 88);
    grid->columns = requested_columns > 0
        ? bounded(requested_columns, 1, 8)
        : bounded(
            (available_width + grid->gap) / (target + grid->gap),
            1,
            profile == MSYS_NATIVE_PROFILE_DESKTOP ? 8 : 5
        );
    grid->cell_width = (
        available_width - grid->gap * (grid->columns - 1)
    ) / grid->columns;
    if (grid->cell_width < 1) grid->cell_width = 1;
    /* Reserve a tiny page-indicator strip. It changes only on a page change. */
    available_height = height - grid->top - grid->margin - 16;
    if (available_height < 1) available_height = 1;
    grid->rows = requested_rows > 0
        ? bounded(requested_rows, 1, 6)
        : bounded(
            (available_height + grid->gap) /
                (grid->icon_size + 46 + grid->gap),
            1,
            6
        );
    grid->cell_height = (
        available_height - grid->gap * (grid->rows - 1)
    ) / grid->rows;
    if (grid->cell_height < 1) grid->cell_height = 1;
    if (grid->icon_size > grid->cell_height - 34) {
        grid->icon_size = grid->cell_height - 34;
    }
    if (grid->icon_size > grid->cell_width - 14) {
        grid->icon_size = grid->cell_width - 14;
    }
    if (grid->icon_size < 24) grid->icon_size = 24;
    grid->viewport_height = available_height;
    grid->content_height = available_height;
    grid->page_capacity = grid->columns * grid->rows;
    if (grid->page_capacity < 1) grid->page_capacity = 1;
    grid->page_count = ((int)item_count + grid->page_capacity - 1) /
        grid->page_capacity;
    if (grid->page_count < 1) grid->page_count = 1;
    grid->indicator_y = grid->top + grid->viewport_height + 7;
}

void msys_native_recents_compute(
    msys_native_recents_layout *layout,
    enum msys_native_shell_profile profile,
    int width,
    int height,
    int top_inset,
    int right_inset,
    int bottom_inset,
    size_t item_count
)
{
    int available_width;
    int rows;
    if (layout == NULL) return;
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    top_inset = bounded(top_inset, 0, height - 1);
    right_inset = bounded(right_inset, 0, width - 1);
    bottom_inset = bounded(bottom_inset, 0, height - top_inset - 1);
    layout->margin = profile == MSYS_NATIVE_PROFILE_DESKTOP ? 28 : 14;
    layout->top = top_inset + 58;
    layout->gap = profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 10 : 14;
    available_width = width - right_inset - layout->margin * 2;
    if (available_width < 1) available_width = 1;
    if (profile == MSYS_NATIVE_PROFILE_DESKTOP) {
        layout->columns = bounded(
            (available_width + layout->gap) / (292 + layout->gap), 1, 4
        );
    } else if (profile == MSYS_NATIVE_PROFILE_MOBILE) {
        /* Phone Overview is a compact screenshot grid.  Two 139px cards on
         * the reference 320px portrait surface retain the application's
         * work-area aspect ratio while keeping two tasks visible per row. */
        layout->columns = available_width >= 260 ? 2 : 1;
    } else if (width - right_inset > height - top_inset - bottom_inset) {
        layout->columns = available_width >= 420 ? 2 : 1;
    } else {
        layout->columns = 1;
    }
    layout->card_width = (
        available_width - layout->gap * (layout->columns - 1)
    ) / layout->columns;
    if (layout->card_width < 1) layout->card_width = 1;
    layout->card_height = profile == MSYS_NATIVE_PROFILE_EMBEDDED ? 176 : 214;
    layout->viewport_height = height - bottom_inset - layout->top - layout->margin;
    if (layout->viewport_height < 1) layout->viewport_height = 1;
    if (layout->card_height > layout->viewport_height) {
        layout->card_height = layout->viewport_height;
    }
    layout->preview_height = layout->card_height - 62;
    if (layout->preview_height < 36) layout->preview_height = 36;
    rows = item_count == 0u
        ? 0 : ((int)item_count + layout->columns - 1) / layout->columns;
    layout->content_height = rows > 0
        ? rows * layout->card_height + (rows - 1) * layout->gap : 0;
}

void msys_native_process_compute(
    msys_native_process_layout *layout,
    int width,
    int height,
    int content_top,
    int right_inset,
    int bottom_inset,
    size_t item_count
)
{
    if (layout == NULL) return;
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;
    content_top = bounded(content_top, 0, height - 1);
    right_inset = bounded(right_inset, 0, width - 1);
    bottom_inset = bounded(bottom_inset, 0, height - content_top - 1);
    layout->margin = 14;
    layout->checkbox_y = content_top;
    layout->checkbox_height = 44;
    layout->rows_top = content_top + layout->checkbox_height + 8;
    if (layout->rows_top >= height - bottom_inset) {
        layout->rows_top = height - bottom_inset - 1;
    }
    layout->row_height = 46;
    layout->gap = 4;
    layout->viewport_height =
        height - bottom_inset - layout->rows_top - layout->margin;
    if (layout->viewport_height < 1) layout->viewport_height = 1;
    layout->content_height = item_count == 0u ? 0 :
        (int)item_count * layout->row_height +
        ((int)item_count - 1) * layout->gap;
    (void)right_inset;
}

int msys_native_scroll_clamp(int requested, int content_height, int viewport_height)
{
    int maximum = content_height - viewport_height;
    if (maximum < 0) maximum = 0;
    return bounded(requested, 0, maximum);
}

void msys_native_launcher_detail_compute(
    msys_native_launcher_detail_layout *layout,
    int height
)
{
    if (layout == NULL) return;
    height = height > 0 ? height : 1;
    layout->top = height > 128 ? height - 128 : 0;
    layout->primary_top = layout->top + 48;
    layout->primary_height = 38;
    layout->quick_top = layout->primary_top + layout->primary_height;
    layout->quick_height = height - layout->quick_top;
    if (layout->quick_height < 1) layout->quick_height = 1;
}

int msys_native_launcher_detail_hit(
    int x,
    int y,
    int width,
    int height,
    size_t quick_action_count
)
{
    msys_native_launcher_detail_layout layout;
    int slot;
    if (width <= 0 || height <= 0 || x < 0 || x >= width) return -1;
    msys_native_launcher_detail_compute(&layout, height);
    if (y >= layout.primary_top && y < layout.quick_top) {
        return x < width / 2 ? 0 : 1;
    }
    if (y < layout.quick_top || y >= height || quick_action_count == 0u) return -1;
    slot = x * 3 / width;
    return (size_t)slot < quick_action_count ? 2 + slot : -1;
}

int msys_native_drag_frame_due(
    int scroll,
    int presented_scroll,
    uint64_t now_ms,
    uint64_t deadline_ms,
    int force
)
{
    if (scroll == presented_scroll) return 0;
    if (force != 0 || deadline_ms == 0u) return 1;
    return now_ms >= deadline_ms;
}

static int layout_hit(
    int x,
    int y,
    int scroll,
    int margin,
    int top,
    int gap,
    int columns,
    int cell_width,
    int cell_height,
    size_t item_count,
    size_t *index
)
{
    int relative_x = x - margin;
    int relative_y = y + scroll - top;
    int column;
    int row;
    size_t position;
    if (
        index == NULL || columns <= 0 || cell_width <= 0 || cell_height <= 0 ||
        relative_x < 0 || relative_y < 0
    ) return 0;
    column = relative_x / (cell_width + gap);
    row = relative_y / (cell_height + gap);
    if (
        column >= columns || relative_x % (cell_width + gap) >= cell_width ||
        relative_y % (cell_height + gap) >= cell_height
    ) return 0;
    position = (size_t)row * (size_t)columns + (size_t)column;
    if (position >= item_count) return 0;
    *index = position;
    return 1;
}

int msys_native_grid_hit(
    int x,
    int y,
    int scroll,
    const msys_native_grid_layout *grid,
    size_t item_count,
    size_t *index
)
{
    if (grid == NULL) return 0;
    return layout_hit(
        x, y, scroll, grid->margin, grid->top, grid->gap, grid->columns,
        grid->cell_width, grid->cell_height, item_count, index
    );
}

int msys_native_recents_hit(
    int x,
    int y,
    int scroll,
    const msys_native_recents_layout *layout,
    size_t item_count,
    size_t *index
)
{
    if (layout == NULL) return 0;
    return layout_hit(
        x, y, scroll, layout->margin, layout->top, layout->gap, layout->columns,
        layout->card_width, layout->card_height, item_count, index
    );
}

int msys_native_recents_exit_hit(
    int x,
    int y,
    int width,
    int top_inset,
    int right_inset,
    int header_bottom
)
{
    if (width <= 0) return 0;
    top_inset = bounded(top_inset, 0, header_bottom);
    right_inset = bounded(right_inset, 0, width - 1);
    return y >= top_inset && y < header_bottom &&
        x >= width - right_inset - 88 && x < width - right_inset;
}

int msys_native_recents_process_hit(
    int x,
    int y,
    int width,
    int top_inset,
    int right_inset,
    int header_bottom
)
{
    if (width <= 0) return 0;
    top_inset = bounded(top_inset, 0, header_bottom);
    right_inset = bounded(right_inset, 0, width - 1);
    return y >= top_inset && y < header_bottom &&
        x >= width - right_inset - 196 && x < width - right_inset - 92;
}

int msys_native_process_checkbox_hit(
    int x,
    int y,
    int width,
    int right_inset,
    const msys_native_process_layout *layout
)
{
    if (layout == NULL || width <= 0) return 0;
    right_inset = bounded(right_inset, 0, width - 1);
    return x >= layout->margin && x < width - right_inset - layout->margin &&
        y >= layout->checkbox_y &&
        y < layout->checkbox_y + layout->checkbox_height;
}

int msys_native_process_row_hit(
    int x,
    int y,
    int width,
    int right_inset,
    int scroll,
    const msys_native_process_layout *layout,
    size_t item_count,
    size_t *index
)
{
    int relative_y;
    int row;
    if (layout == NULL || index == NULL || width <= 0) return 0;
    right_inset = bounded(right_inset, 0, width - 1);
    relative_y = y + scroll - layout->rows_top;
    if (
        x < layout->margin || x >= width - right_inset - layout->margin ||
        relative_y < 0
    ) return 0;
    row = relative_y / (layout->row_height + layout->gap);
    if (
        relative_y % (layout->row_height + layout->gap) >= layout->row_height ||
        row < 0 || (size_t)row >= item_count
    ) return 0;
    *index = (size_t)row;
    return 1;
}

int msys_native_recents_close_hit(
    int x,
    int y,
    int card_x,
    int card_y,
    const msys_native_recents_layout *layout
)
{
    if (layout == NULL || layout->card_width < 1 || layout->card_height < 1) {
        return 0;
    }
    return x >= card_x + layout->card_width - 54 &&
        x < card_x + layout->card_width &&
        y >= card_y + layout->preview_height &&
        y < card_y + layout->card_height;
}

enum msys_native_navigation_action msys_native_gesture_motion(
    msys_native_gesture *gesture,
    int y,
    uint64_t now_ms
)
{
    uint64_t elapsed;
    if (gesture == NULL || gesture->active == 0 || gesture->latched != 0) {
        return MSYS_NATIVE_NAV_NONE;
    }
    gesture->current_y = y;
    elapsed = now_ms >= gesture->started_ms ? now_ms - gesture->started_ms : 0u;
    if (
        inward_distance(gesture->start_y, y) >= RECENTS_DISTANCE_PX &&
        elapsed >= RECENTS_HOLD_MS
    ) {
        gesture->latched = 1;
        return MSYS_NATIVE_NAV_APPS;
    }
    return MSYS_NATIVE_NAV_NONE;
}

enum msys_native_navigation_action msys_native_gesture_release(
    msys_native_gesture *gesture,
    int y,
    uint64_t now_ms
)
{
    enum msys_native_navigation_action action;
    int distance;
    if (gesture == NULL || gesture->active == 0) {
        return MSYS_NATIVE_NAV_NONE;
    }
    action = msys_native_gesture_motion(gesture, y, now_ms);
    distance = inward_distance(gesture->start_y, y);
    gesture->active = 0;
    if (action == MSYS_NATIVE_NAV_APPS || gesture->latched != 0) {
        return action;
    }
    return distance >= BACK_DISTANCE_PX ? MSYS_NATIVE_NAV_BACK : MSYS_NATIVE_NAV_HOME;
}

enum msys_native_method_action msys_native_route_method(
    const char *method,
    int payload_has_message
)
{
    if (method == NULL) {
        return MSYS_NATIVE_METHOD_UNKNOWN;
    }
    if (strcmp(method, "status") == 0) {
        return MSYS_NATIVE_METHOD_STATUS;
    }
    if (strcmp(method, "list") == 0 || strcmp(method, "list_apps") == 0) {
        return MSYS_NATIVE_METHOD_LIST_APPS;
    }
    if (strcmp(method, "get_preferences") == 0) {
        return MSYS_NATIVE_METHOD_GET_PREFERENCES;
    }
    if (strcmp(method, "set_preferences") == 0) {
        return MSYS_NATIVE_METHOD_SET_PREFERENCES;
    }
    if (strcmp(method, "reset_preferences") == 0) {
        return MSYS_NATIVE_METHOD_RESET_PREFERENCES;
    }
    if (strcmp(method, "show") == 0 || strcmp(method, "toggle") == 0) {
        return payload_has_message != 0
            ? MSYS_NATIVE_METHOD_SHOW_NOTIFICATION
            : MSYS_NATIVE_METHOD_SHOW_RECENTS;
    }
    if (strcmp(method, "hide") == 0) {
        return MSYS_NATIVE_METHOD_HIDE_OVERLAYS;
    }
    if (
        strcmp(method, "choose_intent") == 0
    ) {
        return MSYS_NATIVE_METHOD_NOT_IMPLEMENTED;
    }
    return MSYS_NATIVE_METHOD_UNKNOWN;
}

void msys_native_layout_compute(
    msys_native_layout *layout,
    int width,
    int height
)
{
    int bar_height;
    if (layout == NULL) {
        return;
    }
    layout->width = width > 0 ? width : 1;
    layout->height = height > 0 ? height : 1;
    bar_height = 42;
    if (bar_height * 2 >= layout->height) {
        bar_height = layout->height / 4;
    }
    if (bar_height < 1) {
        bar_height = 1;
    }
    layout->bar_height = bar_height;
    layout->content_y = bar_height;
    layout->content_height = layout->height - bar_height * 2;
    if (layout->content_height < 1) {
        layout->content_height = 1;
    }
}

int msys_native_launch_transition_matches(
    const char *active_id,
    const char *active_component,
    const char *event_id,
    const char *event_component,
    const char *event_action
)
{
    return active_id != NULL && active_id[0] != '\0' &&
        active_component != NULL && active_component[0] != '\0' &&
        event_id != NULL && event_component != NULL && event_action != NULL &&
        strcmp(active_id, event_id) == 0 &&
        strcmp(active_component, event_component) == 0 &&
        strcmp(event_action, "launch") == 0;
}

int msys_native_transition_expired(uint64_t deadline_ms, uint64_t now_ms)
{
    return deadline_ms != 0u && now_ms >= deadline_ms;
}
