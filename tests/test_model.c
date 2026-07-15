#include "msys_shell_native/model.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static int test_gesture(void)
{
    msys_native_gesture gesture = {0};
    msys_native_gesture_begin(&gesture, 34, 100u);
    CHECK(msys_native_gesture_motion(&gesture, 2, 300u) == MSYS_NATIVE_NAV_NONE);
    CHECK(msys_native_gesture_motion(&gesture, 2, 520u) == MSYS_NATIVE_NAV_APPS);
    CHECK(msys_native_gesture_release(&gesture, 2, 600u) == MSYS_NATIVE_NAV_NONE);

    msys_native_gesture_begin(&gesture, 34, 100u);
    CHECK(msys_native_gesture_release(&gesture, 15, 250u) == MSYS_NATIVE_NAV_BACK);
    msys_native_gesture_begin(&gesture, 34, 100u);
    CHECK(msys_native_gesture_release(&gesture, 33, 180u) == MSYS_NATIVE_NAV_HOME);
    CHECK(msys_native_button_action(0, 320) == MSYS_NATIVE_NAV_BACK);
    CHECK(msys_native_button_action(160, 320) == MSYS_NATIVE_NAV_HOME);
    CHECK(msys_native_button_action(319, 320) == MSYS_NATIVE_NAV_APPS);
    CHECK(msys_native_button_action_at(10, 200, 42, 480) == MSYS_NATIVE_NAV_HOME);
    CHECK(msys_native_button_action_at(200, 10, 480, 42) == MSYS_NATIVE_NAV_HOME);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, 53, 42, 320, 42, 8
    ) == MSYS_NATIVE_NAV_BACK);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, 53, 43, 320, 42, 8
    ) == MSYS_NATIVE_NAV_BACK);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, -4, 21, 320, 42, 8
    ) == MSYS_NATIVE_NAV_BACK);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, 53, 50, 320, 42, 8
    ) == MSYS_NATIVE_NAV_NONE);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, 160, 21, 320, 42, 8
    ) == MSYS_NATIVE_NAV_NONE);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, 43, 79, 42, 480, 8
    ) == MSYS_NATIVE_NAV_BACK);
    CHECK(msys_native_button_release_action_at(
        MSYS_NATIVE_NAV_BACK, 43, 240, 42, 480, 8
    ) == MSYS_NATIVE_NAV_NONE);
    CHECK(msys_native_button_action(0, 0) == MSYS_NATIVE_NAV_NONE);
    CHECK(msys_native_navigation_slot_center(320, 0) == 53);
    CHECK(msys_native_navigation_slot_center(320, 1) == 160);
    CHECK(msys_native_navigation_slot_center(320, 2) == 266);
    CHECK(msys_native_navigation_slot_center(0, 1) == 0);
    CHECK(msys_native_navigation_slot_center(320, 3) == 0);
    CHECK(msys_native_center_baseline(24, -14, 18) == 17);
    CHECK(msys_native_center_baseline(42, -14, 18) == 26);
    CHECK(msys_native_center_baseline(0, 0, 0) == 0);
    CHECK(msys_native_wifi_signal_level(0, 1, -40) == 0);
    CHECK(msys_native_wifi_signal_level(1, 0, 0) == 1);
    CHECK(msys_native_wifi_signal_level(1, 1, -71) == 1);
    CHECK(msys_native_wifi_signal_level(1, 1, -70) == 2);
    CHECK(msys_native_wifi_signal_level(1, 1, -56) == 2);
    CHECK(msys_native_wifi_signal_level(1, 1, -55) == 3);
    return 0;
}

static int test_adaptive_ui(void)
{
    msys_native_grid_layout grid;
    msys_native_recents_layout recents;
    msys_native_process_layout processes;
    size_t index = 99u;
    CHECK(msys_native_profile_resolve("profile", "mobile", 320, 480) == MSYS_NATIVE_PROFILE_MOBILE);
    CHECK(msys_native_profile_resolve("embedded", "desktop", 1280, 720) == MSYS_NATIVE_PROFILE_EMBEDDED);
    CHECK(msys_native_profile_resolve("auto", NULL, 1280, 720) == MSYS_NATIVE_PROFILE_DESKTOP);
    CHECK(strcmp(msys_native_profile_name(MSYS_NATIVE_PROFILE_EMBEDDED), "embedded") == 0);
    msys_native_grid_compute(&grid, MSYS_NATIVE_PROFILE_MOBILE, 320, 396, 64, 24u);
    CHECK(grid.columns >= 2 && grid.content_height > grid.viewport_height);
    CHECK(msys_native_grid_hit(grid.margin + 2, grid.top + 2, 0, &grid, 24u, &index));
    CHECK(index == 0u);
    CHECK(msys_native_scroll_clamp(9999, grid.content_height, grid.viewport_height) ==
        grid.content_height - grid.viewport_height);
    CHECK(!msys_native_drag_frame_due(0, 0, 100u, 0u, 0));
    CHECK(!msys_native_drag_frame_due(0, 0, 100u, 0u, 1));
    CHECK(msys_native_drag_frame_due(1, 0, 100u, 0u, 0));
    CHECK(!msys_native_drag_frame_due(1, 0, 100u, 180u, 0));
    CHECK(msys_native_drag_frame_due(1, 0, 180u, 180u, 0));
    CHECK(msys_native_drag_frame_due(1, 0, 100u, 180u, 1));
    msys_native_recents_compute(
        &recents, MSYS_NATIVE_PROFILE_MOBILE, 800, 480, 42, 42, 0, 5u
    );
    CHECK(recents.columns == 2);
    CHECK(msys_native_recents_hit(
        recents.margin + recents.card_width + recents.gap + 2,
        recents.top + 2,
        0,
        &recents,
        5u,
        &index
    ));
    CHECK(index == 1u);

    /* A full-screen 320x480 Overview stays clear of the 42px system bars,
     * while its visible header action remains touchable. */
    msys_native_recents_compute(
        &recents, MSYS_NATIVE_PROFILE_MOBILE, 320, 480, 42, 0, 42, 3u
    );
    CHECK(recents.top == 100);
    CHECK(recents.viewport_height == 324);
    CHECK(recents.columns == 2);
    CHECK(recents.card_width == 139);
    CHECK(msys_native_recents_exit_hit(290, 70, 320, 42, 0, recents.top));
    CHECK(!msys_native_recents_exit_hit(290, 30, 320, 42, 0, recents.top));
    CHECK(!msys_native_recents_exit_hit(100, 70, 320, 42, 0, recents.top));
    CHECK(msys_native_recents_process_hit(180, 70, 320, 42, 0, recents.top));
    CHECK(!msys_native_recents_process_hit(240, 70, 320, 42, 0, recents.top));
    CHECK(!msys_native_recents_process_hit(180, 30, 320, 42, 0, recents.top));
    CHECK(!(
        msys_native_recents_process_hit(232, 70, 320, 42, 0, recents.top) &&
        msys_native_recents_exit_hit(232, 70, 320, 42, 0, recents.top)
    ));
    CHECK(msys_native_recents_close_hit(
        137, recents.top + recents.preview_height + 4,
        recents.margin, recents.top, &recents
    ));
    CHECK(!msys_native_recents_close_hit(
        60, recents.top + recents.preview_height + 4,
        recents.margin, recents.top, &recents
    ));
    msys_native_process_compute(
        &processes, 320, 480, recents.top, 0, 42, 20u
    );
    CHECK(processes.checkbox_y == recents.top);
    CHECK(processes.rows_top > processes.checkbox_y);
    CHECK(processes.viewport_height > 0);
    CHECK(processes.content_height > processes.viewport_height);
    CHECK(msys_native_process_checkbox_hit(
        24, processes.checkbox_y + 10, 320, 0, &processes
    ));
    CHECK(!msys_native_process_checkbox_hit(
        24, processes.rows_top + 10, 320, 0, &processes
    ));
    CHECK(msys_native_process_row_hit(
        24, processes.rows_top + 4, 320, 0, 0,
        &processes, 20u, &index
    ));
    CHECK(index == 0u);
    CHECK(msys_native_process_row_hit(
        24, processes.rows_top + 4, 320, 0,
        processes.row_height + processes.gap,
        &processes, 20u, &index
    ));
    CHECK(index == 1u);
    CHECK(!msys_native_process_row_hit(
        2, processes.rows_top + 4, 320, 0, 0,
        &processes, 20u, &index
    ));
    return 0;
}

static int test_methods(void)
{
    CHECK(msys_native_route_method("list", 0) == MSYS_NATIVE_METHOD_LIST_APPS);
    CHECK(msys_native_route_method("get_preferences", 0) == MSYS_NATIVE_METHOD_GET_PREFERENCES);
    CHECK(msys_native_route_method("show", 0) == MSYS_NATIVE_METHOD_SHOW_RECENTS);
    CHECK(msys_native_route_method("show", 1) == MSYS_NATIVE_METHOD_SHOW_NOTIFICATION);
    CHECK(msys_native_route_method("set_preferences", 0) == MSYS_NATIVE_METHOD_SET_PREFERENCES);
    CHECK(msys_native_route_method("reset_preferences", 0) == MSYS_NATIVE_METHOD_RESET_PREFERENCES);
    CHECK(msys_native_route_method("future", 0) == MSYS_NATIVE_METHOD_UNKNOWN);
    return 0;
}

static int test_layout(void)
{
    msys_native_layout layout;
    msys_native_layout_compute(&layout, 320, 480);
    CHECK(layout.width == 320);
    CHECK(layout.height == 480);
    CHECK(layout.bar_height == 42);
    CHECK(layout.content_y == 42);
    CHECK(layout.content_height == 396);
    msys_native_layout_compute(&layout, 0, 3);
    CHECK(layout.width == 1);
    CHECK(layout.content_height >= 1);
    return 0;
}

int main(void)
{
    CHECK(test_gesture() == 0);
    CHECK(test_methods() == 0);
    CHECK(test_layout() == 0);
    CHECK(test_adaptive_ui() == 0);
    puts("native shell model tests passed");
    return 0;
}
