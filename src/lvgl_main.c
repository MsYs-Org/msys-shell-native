#define _POSIX_C_SOURCE 200809L

#include "msys_shell_native/catalog.h"
#include "msys_shell_native/image.h"
#include "msys_shell_native/model.h"
#include "msys_shell_native/notification.h"
#include "msys_shell_native/preferences.h"
#include "msys_shell_native/launcher_layout.h"
#include "msys_shell_native/system_metrics.h"

#include "msys/mipc.h"
#include "msys_ui/acrylic.h"
#include "msys_ui/document.h"
#include "msys_ui/fonts.h"
#include "msys_ui/runtime.h"
#include "msys_ui/theme.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define APP_VERSION "0.6.23"
#define SURFACE_COUNT 8u
#define BAR_HEIGHT 42
#define ROOT_WIDTH 320
#define ROOT_HEIGHT 480
#define WORK_HEIGHT (ROOT_HEIGHT - BAR_HEIGHT * 2)
#define DRAW_ROWS 48
#define MAX_PENDING 16u
#define LAUNCHER_BINDING_CAPACITY 96u
#define MAX_BINDINGS (LAUNCHER_BINDING_CAPACITY + MSYS_NATIVE_MAX_TASKS * 2u)
#define XML_WATCH_INTERVAL_MS 250u
#define METRICS_INTERVAL_MS 1000u
#define WIFI_REFRESH_INTERVAL_MS 10000u
#define TOAST_VISIBLE_MS 1800u
#define LAUNCH_TRANSITION_TIMEOUT_MS 8000u
#define LAUNCHER_LONG_PRESS_MS 520u
#define LAUNCHER_EDGE_PX 24
#define LAUNCHER_EDGE_DWELL_MS 420u
#define LAUNCHER_SWIPE_SLOP_PX 12

enum shell_surface_id {
    SURFACE_LAUNCHER = 0,
    SURFACE_CHROME,
    SURFACE_NAVIGATION,
    SURFACE_OVERVIEW,
    SURFACE_NOTIFICATION,
    SURFACE_CONTROLS,
    SURFACE_TOAST,
    SURFACE_TRANSITION
};

enum pending_kind {
    PENDING_NONE = 0,
    PENDING_APPS,
    PENDING_TASKS,
    PENDING_TASK_RESOURCES,
    PENDING_START,
    PENDING_BEGIN_TRANSITION,
    PENDING_CANCEL_TRANSITION,
    PENDING_FOCUS,
    PENDING_CLOSE,
    PENDING_NAVIGATION,
    PENDING_WIFI_INVENTORY,
    PENDING_WIFI_STATE,
    PENDING_SETTINGS,
    PENDING_DETAILS,
    PENDING_UNINSTALL,
    PENDING_QUICK_ACTION
};

enum binding_action {
    BIND_START_APP = 1,
    BIND_FOCUS_TASK,
    BIND_CLOSE_TASK,
    BIND_LAUNCHER_ITEM,
    BIND_FOLDER_MEMBER,
    BIND_LAUNCHER_PREVIOUS,
    BIND_LAUNCHER_NEXT,
    BIND_LAUNCHER_EDIT_DONE,
    BIND_LAUNCHER_DETAILS,
    BIND_LAUNCHER_UNINSTALL,
    BIND_LAUNCHER_TOGGLE_FOLDER_SIZE,
    BIND_LAUNCHER_QUICK_ACTION,
    BIND_FOLDER_CLOSE
};

typedef struct shell_state shell_state;

typedef struct {
    shell_state *shell;
    enum shell_surface_id id;
    msys_ui_surface_t *surface;
    msys_ui_theme_t *theme;
    msys_ui_document_t *document;
    char path[PATH_MAX];
} shell_view;

typedef struct {
    uint64_t id;
    enum pending_kind kind;
    size_t index;
} pending_call;

typedef struct {
    shell_state *shell;
    enum binding_action action;
    size_t index;
    size_t secondary;
} ui_binding;

typedef struct {
    lv_image_dsc_t descriptor;
    uint8_t *pixels;
    char path[MSYS_NATIVE_PATH_CAPACITY];
} ui_bitmap;

struct shell_state {
    msys_ui_runtime_t *runtime;
    const msys_ui_anim_policy_t *policy;
    shell_view views[SURFACE_COUNT];
    msys_mipc_client ipc;
    int supervised;
    uint64_t next_request_id;
    pending_call pending[MAX_PENDING];
    char *packet;
    msys_native_app apps[MSYS_NATIVE_MAX_APPS];
    size_t app_count;
    int apps_loaded;
    msys_native_task tasks[MSYS_NATIVE_MAX_TASKS];
    size_t task_count;
    ui_bitmap app_icons[MSYS_NATIVE_MAX_APPS];
    ui_bitmap task_previews[MSYS_NATIVE_MAX_TASKS];
    ui_bitmap transition_icon;
    ui_bitmap wallpaper;
    msys_ui_acrylic_cache_t *launcher_acrylic;
    msys_ui_acrylic_panel_t *launcher_acrylic_panels[
        MSYS_NATIVE_LAUNCHER_MAX_ITEMS];
    size_t launcher_acrylic_panel_count;
    uint64_t launcher_acrylic_revision;
    ui_binding bindings[MAX_BINDINGS];
    size_t binding_count;
    msys_native_system_metrics metrics;
    msys_native_notification_history notifications;
    msys_native_gesture pill_gesture;
    lv_timer_t *toast_timer;
    lv_timer_t *transition_finish_timer;
    uint64_t next_metrics_at;
    uint64_t next_xml_watch_at;
    uint64_t next_wifi_refresh_at;
    uint64_t run_until;
    int overview_visible;
    int overview_pending;
    int notification_visible;
    int controls_visible;
    int toast_visible;
    int transition_visible;
    int transition_finishing;
    uint64_t transition_sequence;
    uint64_t transition_deadline;
    uint64_t transition_opening_until;
    uint64_t transition_begin_call_id;
    uint64_t transition_start_call_id;
    size_t transition_app_index;
    char transition_id[97];
    char transition_component[MSYS_NATIVE_COMPONENT_CAPACITY];
    int transition_origin_x;
    int transition_origin_y;
    int transition_origin_width;
    int transition_origin_height;
    int buttons_mode;
    int legacy_navigation_override;
    int wifi_known;
    int wifi_available;
    int wifi_connected;
    int wifi_signal_level;
    int watch_ui;
    int chinese;
    char wifi_device[MSYS_NATIVE_COMPONENT_CAPACITY];
    char ui_dir[PATH_MAX];
    char clock_text[16];
    msys_native_preferences preferences;
    char preferences_path[MSYS_NATIVE_PREFERENCES_PATH_CAPACITY];
    msys_native_launcher_layout launcher_layout;
    unsigned int launcher_page;
    size_t launcher_page_capacity;
    int launcher_columns;
    int launcher_rows;
    int launcher_editing;
    int launcher_folder;
    int launcher_selected;
    int launcher_drag_source;
    int launcher_drag_target;
    size_t launcher_drop_position;
    enum msys_native_launcher_drop_mode launcher_drop_mode;
    int launcher_dragging;
    int launcher_drag_dx;
    int launcher_drag_dy;
    int launcher_drag_folder;
    int launcher_drag_member;
    int launcher_pointer_consumed;
    int launcher_edge_direction;
    int launcher_edge_armed;
    uint64_t launcher_edge_since;
    lv_point_t launcher_drag_origin;
    lv_point_t launcher_drag_point;
    int launcher_drag_point_valid;
    lv_obj_t *launcher_drag_object;
    lv_obj_t *launcher_drop_preview;
    int launcher_swipe_active;
    int launcher_swipe_dx;
    lv_point_t launcher_swipe_origin;
    lv_obj_t *launcher_tiles[MSYS_NATIVE_LAUNCHER_MAX_ITEMS];
};

static volatile sig_atomic_t stopping;
static shell_state *active_shell;

static uint32_t launcher_color(const char *text, uint32_t fallback);

static uint64_t monotonic_ms(void)
{
    struct timespec value;
    (void)clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static uint64_t realtime_ms(void)
{
    struct timespec value;
    (void)clock_gettime(CLOCK_REALTIME, &value);
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static void signal_handler(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static const char *localized(const shell_state *shell, const char *zh,
                             const char *en)
{
    return shell->chinese != 0 ? zh : en;
}

static int locale_is_chinese(void)
{
    const char *locale = getenv("MSYS_LOCALE");
    if(locale == NULL || locale[0] == '\0') locale = getenv("LANG");
    return locale != NULL && strncmp(locale, "zh", 2u) == 0;
}

static lv_obj_t *view_object(shell_view *view, const char *name)
{
    if(view == NULL || view->document == NULL) return NULL;
    return msys_ui_document_find(view->document, name);
}

static void set_label(shell_view *view, const char *name, const char *text)
{
    lv_obj_t *object = view_object(view, name);
    if(object != NULL) lv_label_set_text(object, text != NULL ? text : "");
}

static void set_label_if_changed(shell_view *view, const char *name,
                                 const char *text)
{
    lv_obj_t *object = view_object(view, name);
    const char *current;
    if(object == NULL) return;
    if(text == NULL) text = "";
    current = lv_label_get_text(object);
    if(current == NULL || strcmp(current, text) != 0) lv_label_set_text(object, text);
}

static void bitmap_dispose(ui_bitmap *bitmap)
{
    if(bitmap == NULL) return;
    free(bitmap->pixels);
    memset(bitmap, 0, sizeof(*bitmap));
}

static void bitmaps_dispose(ui_bitmap *items, size_t count)
{
    size_t index;
    for(index = 0u; index < count; index++) bitmap_dispose(&items[index]);
}

static int bitmap_load(ui_bitmap *bitmap, const char *path, int width, int height)
{
    msys_native_ppm source = {0};
    size_t bytes;
    int x;
    int y;
    if(bitmap == NULL || path == NULL || path[0] == '\0' || width <= 0 ||
       height <= 0)
        return 0;
    if(bitmap->pixels != NULL && bitmap->descriptor.header.w == (uint16_t)width &&
       bitmap->descriptor.header.h == (uint16_t)height &&
       strcmp(bitmap->path, path) == 0)
        return 1;
    bitmap_dispose(bitmap);
    if(msys_native_ppm_load(path, 8u * 1024u * 1024u, &source) == 0) return 0;
    bytes = (size_t)width * (size_t)height * 2u;
    bitmap->pixels = malloc(bytes);
    if(bitmap->pixels == NULL) {
        msys_native_ppm_free(&source);
        return 0;
    }
    for(y = 0; y < height; y++) {
        for(x = 0; x < width; x++) {
            unsigned char rgb[3];
            uint16_t pixel;
            size_t offset = ((size_t)y * (size_t)width + (size_t)x) * 2u;
            if(msys_native_ppm_sample_resized(&source, width, height, x, y,
                                               rgb) == 0) {
                rgb[0] = rgb[1] = rgb[2] = 0u;
            }
            pixel = (uint16_t)(((uint16_t)(rgb[0] & 0xf8u) << 8u) |
                               ((uint16_t)(rgb[1] & 0xfcu) << 3u) |
                               ((uint16_t)rgb[2] >> 3u));
            bitmap->pixels[offset] = (uint8_t)(pixel & 0xffu);
            bitmap->pixels[offset + 1u] = (uint8_t)(pixel >> 8u);
        }
    }
    msys_native_ppm_free(&source);
    bitmap->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    bitmap->descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    bitmap->descriptor.header.w = (uint16_t)width;
    bitmap->descriptor.header.h = (uint16_t)height;
    bitmap->descriptor.header.stride = (uint16_t)(width * 2);
    bitmap->descriptor.data_size = (uint32_t)bytes;
    bitmap->descriptor.data = bitmap->pixels;
    (void)snprintf(bitmap->path, sizeof(bitmap->path), "%s", path);
    return 1;
}

typedef struct {
    uint16_t color;
} acrylic_fill_source;

typedef struct {
    const uint8_t *pixels;
    size_t stride;
} acrylic_bitmap_source;

static uint16_t acrylic_fill_sample(uint16_t x, uint16_t y, void *user_data)
{
    const acrylic_fill_source *source = user_data;
    (void)x;
    (void)y;
    return source != NULL ? source->color : 0xffffu;
}

static uint16_t acrylic_bitmap_sample(uint16_t x, uint16_t y, void *user_data)
{
    const acrylic_bitmap_source *source = user_data;
    const uint8_t *pixel;
    if(source == NULL || source->pixels == NULL) return 0xffffu;
    pixel = source->pixels + (size_t)y * source->stride + (size_t)x * 2u;
    return (uint16_t)((uint16_t)pixel[0] | ((uint16_t)pixel[1] << 8u));
}

static uint16_t rgb888_to_rgb565(unsigned int color)
{
    unsigned int red = (color >> 16u) & 0xffu;
    unsigned int green = (color >> 8u) & 0xffu;
    unsigned int blue = color & 0xffu;
    return (uint16_t)(((red & 0xf8u) << 8u) |
                      ((green & 0xfcu) << 3u) | (blue >> 3u));
}

static void launcher_acrylic_dispose(shell_state *shell)
{
    if(shell == NULL) return;
    msys_ui_acrylic_cache_destroy(shell->launcher_acrylic);
    shell->launcher_acrylic = NULL;
    shell->launcher_acrylic_revision = 0u;
}

static int launcher_acrylic_prepare(shell_state *shell)
{
    acrylic_fill_source fill;
    acrylic_bitmap_source bitmap;
    msys_ui_acrylic_cache_config_t config =
        msys_ui_acrylic_cache_config_default();
    /* The cache is stretched 4x on this panel, so keep grain subtle enough
     * that one cached sample never reads as a visible square artifact. */
    config.grain_strength = 5u;
    if(shell == NULL || shell->preferences.acrylic == 0) {
        launcher_acrylic_dispose(shell);
        return 0;
    }
    if(shell->launcher_acrylic != NULL &&
       shell->launcher_acrylic_revision == shell->preferences.revision)
        return 1;
    launcher_acrylic_dispose(shell);
    if(shell->wallpaper.pixels != NULL &&
       shell->wallpaper.descriptor.header.w == ROOT_WIDTH &&
       shell->wallpaper.descriptor.header.h == WORK_HEIGHT) {
        bitmap.pixels = shell->wallpaper.pixels;
        bitmap.stride = (size_t)ROOT_WIDTH * 2u;
        shell->launcher_acrylic = msys_ui_acrylic_cache_create_sampled(
            ROOT_WIDTH, WORK_HEIGHT, &config, acrylic_bitmap_sample, &bitmap);
    }
    else {
        fill.color = rgb888_to_rgb565(launcher_color(
            shell->preferences.wallpaper_color, 0xf4f6fa));
        shell->launcher_acrylic = msys_ui_acrylic_cache_create_sampled(
            ROOT_WIDTH, WORK_HEIGHT, &config, acrylic_fill_sample, &fill);
    }
    if(shell->launcher_acrylic == NULL) return 0;
    shell->launcher_acrylic_revision = shell->preferences.revision;
    return 1;
}

static lv_obj_t *launcher_tile_create(shell_state *shell, lv_obj_t *grid,
                                      int width, int height,
                                      lv_obj_t **content)
{
    lv_obj_t *tile;
    lv_obj_t *body;
    if(shell != NULL && shell->launcher_acrylic != NULL &&
       shell->launcher_acrylic_panel_count <
           MSYS_NATIVE_LAUNCHER_MAX_ITEMS) {
        msys_ui_acrylic_panel_t *panel = msys_ui_acrylic_panel_create(
            grid, shell->launcher_acrylic, NULL);
        if(panel != NULL) {
            tile = msys_ui_acrylic_panel_object(panel);
            body = msys_ui_acrylic_panel_content(panel);
            shell->launcher_acrylic_panels[
                shell->launcher_acrylic_panel_count++] = panel;
            lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE |
                                  LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_set_size(tile, width, height);
            lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(body, 6, LV_PART_MAIN);
            if(content != NULL) *content = body;
            return tile;
        }
    }
    tile = lv_button_create(grid);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(tile, width, height);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(tile, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, lv_color_hex(0xdde4ef), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 6, LV_PART_MAIN);
    if(content != NULL) *content = tile;
    return tile;
}

static void launcher_acrylic_sync(shell_state *shell, lv_obj_t *root)
{
    size_t index;
    if(shell == NULL || shell->launcher_acrylic_panel_count == 0u) return;
    if(root != NULL) lv_obj_update_layout(root);
    for(index = 0u; index < shell->launcher_acrylic_panel_count; index++)
        msys_ui_acrylic_panel_sync(shell->launcher_acrylic_panels[index]);
}

static pending_call *pending_find(shell_state *shell, uint64_t id)
{
    size_t index;
    for(index = 0u; index < MAX_PENDING; index++) {
        if(shell->pending[index].id == id) return &shell->pending[index];
    }
    return NULL;
}

static pending_call *pending_allocate(shell_state *shell)
{
    size_t index;
    for(index = 0u; index < MAX_PENDING; index++) {
        if(shell->pending[index].id == 0u) return &shell->pending[index];
    }
    return NULL;
}

static uint64_t send_call(shell_state *shell, enum pending_kind kind,
                          size_t index, const char *target, const char *method,
                          const char *payload, int idempotent)
{
    pending_call *pending;
    uint64_t deadline;
    uint64_t id;
    if(shell->supervised == 0) return 0u;
    pending = pending_allocate(shell);
    if(pending == NULL) return 0u;
    id = ++shell->next_request_id;
    if(id == 0u) id = ++shell->next_request_id;
    deadline = monotonic_ms() + 8000u;
    if(msys_mipc_send_call_json(&shell->ipc, id, target, method, payload,
                                deadline, idempotent) != MSYS_MIPC_OK)
        return 0u;
    pending->id = id;
    pending->kind = kind;
    pending->index = index;
    return id;
}

static int json_payload_copy(const char *packet, char **output)
{
    const char *raw = NULL;
    size_t length = 0u;
    char *copy;
    if(msys_mipc_json_get_raw(packet, "payload", &raw, &length) != MSYS_MIPC_OK)
        return 0;
    copy = malloc(length + 1u);
    if(copy == NULL) return 0;
    memcpy(copy, raw, length);
    copy[length] = '\0';
    *output = copy;
    return 1;
}

static int component_payload(const char *component, char *output,
                             size_t capacity)
{
    char escaped[MSYS_NATIVE_COMPONENT_CAPACITY * 2u];
    int written;
    if(component == NULL || strchr(component, ':') == NULL ||
       msys_native_json_escape(component, escaped, sizeof(escaped)) == 0)
        return 0;
    written = snprintf(output, capacity, "{\"component\":\"%s\"}", escaped);
    return written >= 0 && (size_t)written < capacity;
}

static int window_payload(const char *window_id, char *output, size_t capacity)
{
    char escaped[MSYS_NATIVE_WINDOW_ID_CAPACITY * 2u];
    int written;
    if(window_id == NULL || window_id[0] == '\0' ||
       msys_native_json_escape(window_id, escaped, sizeof(escaped)) == 0)
        return 0;
    written = snprintf(output, capacity, "{\"id\":\"%s\"}", escaped);
    return written >= 0 && (size_t)written < capacity;
}

static void hide_toast(shell_state *shell)
{
    if(shell == NULL) return;
    if(shell->toast_timer != NULL) {
        lv_timer_delete(shell->toast_timer);
        shell->toast_timer = NULL;
    }
    if(shell->toast_visible == 0) return;
    shell->toast_visible = 0;
    msys_ui_surface_hide(shell->views[SURFACE_TOAST].surface);
}

static void toast_hide_cb(lv_timer_t *timer)
{
    shell_state *shell = lv_timer_get_user_data(timer);
    if(shell != NULL) {
        shell->toast_timer = NULL;
        hide_toast(shell);
    }
    lv_timer_delete(timer);
}

static void show_toast(shell_state *shell, const char *message)
{
    lv_obj_t *root;
    set_label_if_changed(&shell->views[SURFACE_TOAST], "toast_message", message);
    if(shell->toast_visible != 0) return;
    shell->toast_visible = 1;
    msys_ui_surface_show(shell->views[SURFACE_TOAST].surface);
    root = msys_ui_document_root(shell->views[SURFACE_TOAST].document);
    if(root != NULL) msys_ui_animate_toast(root, shell->policy, true);
    shell->toast_timer = lv_timer_create(toast_hide_cb, TOAST_VISIBLE_MS, shell);
}

static lv_obj_t *make_image(lv_obj_t *parent, ui_bitmap *bitmap, int width,
                            int height);
static lv_obj_t *make_fallback_icon(shell_view *view, lv_obj_t *parent,
                                    const char *name, int size);

static void transition_set_translate_x(void *object, int32_t value)
{
    lv_obj_set_style_translate_x(object, value, LV_PART_MAIN);
}

static void transition_set_translate_y(void *object, int32_t value)
{
    lv_obj_set_style_translate_y(object, value, LV_PART_MAIN);
}

static void transition_set_scale(void *object, int32_t value)
{
    lv_obj_set_style_transform_scale(object, value, LV_PART_MAIN);
}

static void transition_animate(lv_obj_t *object, lv_anim_exec_xcb_t callback,
                               int32_t from, int32_t to, uint16_t duration)
{
    lv_anim_t animation;
    lv_anim_delete(object, callback);
    if(duration == 0u) {
        callback(object, to);
        return;
    }
    callback(object, from);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    (void)lv_anim_start(&animation);
}

static uint16_t transition_motion_duration(const shell_state *shell)
{
    if(shell->preferences.animations_enabled == 0 ||
       shell->preferences.reduce_motion != 0)
        return 0u;
    return msys_ui_motion_duration(shell->policy, MSYS_UI_MOTION_OPENING);
}

static void hide_launch_transition(shell_state *shell)
{
    if(shell == NULL) return;
    if(shell->transition_finish_timer != NULL) {
        lv_timer_delete(shell->transition_finish_timer);
        shell->transition_finish_timer = NULL;
    }
    if(shell->transition_visible != 0)
        msys_ui_surface_hide(shell->views[SURFACE_TRANSITION].surface);
    shell->transition_visible = 0;
    shell->transition_finishing = 0;
    shell->transition_deadline = 0u;
    shell->transition_opening_until = 0u;
    shell->transition_begin_call_id = 0u;
    shell->transition_start_call_id = 0u;
    shell->transition_id[0] = '\0';
    shell->transition_component[0] = '\0';
}

static void transition_finish_cb(lv_timer_t *timer)
{
    shell_state *shell = lv_timer_get_user_data(timer);
    if(shell != NULL) shell->transition_finish_timer = NULL;
    lv_timer_delete(timer);
    hide_launch_transition(shell);
}

static void begin_transition_finish_feedback(shell_state *shell)
{
    lv_obj_t *host;
    int32_t scale;
    const uint16_t duration = 100u;
    if(shell == NULL || shell->transition_visible == 0) return;
    host = view_object(&shell->views[SURFACE_TRANSITION],
                       "transition_icon_host");
    if(host == NULL) {
        hide_launch_transition(shell);
        return;
    }
    scale = lv_obj_get_style_transform_scale_x(host, LV_PART_MAIN);
    transition_animate(host, transition_set_scale, scale, 288, duration);
    shell->transition_finish_timer = lv_timer_create(
        transition_finish_cb, duration, shell);
    if(shell->transition_finish_timer == NULL)
        hide_launch_transition(shell);
}

static void transition_opening_complete_cb(lv_timer_t *timer)
{
    shell_state *shell = lv_timer_get_user_data(timer);
    if(shell != NULL) shell->transition_finish_timer = NULL;
    lv_timer_delete(timer);
    begin_transition_finish_feedback(shell);
}

static void cancel_launch_observation(shell_state *shell, const char *reason)
{
    char payload[256];
    if(shell == NULL || shell->supervised == 0 || shell->transition_id[0] == '\0')
        return;
    (void)snprintf(payload, sizeof(payload),
        "{\"transition_id\":\"%s\",\"reason\":\"%s\"}",
        shell->transition_id, reason != NULL ? reason : "shell-cancelled");
    (void)send_call(shell, PENDING_CANCEL_TRANSITION,
                    shell->transition_app_index, "role:window-manager",
                    "cancel_transition", payload, 1);
}

static void fail_launch_transition(shell_state *shell, const char *reason,
                                   int cancel_observation)
{
    char message[192];
    if(shell == NULL || shell->transition_visible == 0) return;
    if(cancel_observation != 0)
        cancel_launch_observation(shell,
            reason != NULL ? reason : "launch-failed");
    hide_launch_transition(shell);
    (void)snprintf(message, sizeof(message), "%s%s%s",
        localized(shell, "应用启动失败", "Application launch failed"),
        reason != NULL && reason[0] != '\0' ? ": " : "",
        reason != NULL ? reason : "");
    show_toast(shell, message);
}

static int show_launch_transition(shell_state *shell, size_t app_index,
                                  lv_obj_t *origin_object)
{
    shell_view *view = &shell->views[SURFACE_TRANSITION];
    lv_obj_t *root = view_object(view, "transition_root");
    lv_obj_t *host = view_object(view, "transition_icon_host");
    lv_obj_t *icon;
    lv_area_t area;
    lv_area_t host_area;
    uint16_t duration;
    int origin_center_x;
    int origin_center_y;
    int host_center_x;
    int host_center_y;
    int scale;
    if(shell == NULL || app_index >= shell->app_count || root == NULL || host == NULL)
        return 0;
    if(shell->transition_visible != 0) {
        cancel_launch_observation(shell, "superseded");
        hide_launch_transition(shell);
    }
    lv_obj_update_layout(root);
    lv_obj_get_coords(host, &host_area);
    if(origin_object != NULL) {
        int size;
        int center_x;
        int center_y;
        lv_obj_get_coords(origin_object, &area);
        size = (int)lv_area_get_width(&area);
        if(size > (int)lv_area_get_height(&area))
            size = (int)lv_area_get_height(&area);
        if(size > 64) size = 64;
        if(size < 1) size = 1;
        center_x = area.x1 + (int)lv_area_get_width(&area) / 2;
        center_y = area.y1 + (int)lv_area_get_height(&area) / 2;
        area.x1 = center_x - size / 2;
        area.y1 = center_y - size / 2;
        area.x2 = area.x1 + size - 1;
        area.y2 = area.y1 + size - 1;
    }
    else area = host_area;
    shell->transition_origin_x = area.x1;
    shell->transition_origin_y = area.y1;
    shell->transition_origin_width = (int)lv_area_get_width(&area);
    shell->transition_origin_height = (int)lv_area_get_height(&area);
    shell->transition_app_index = app_index;
    shell->transition_sequence++;
    (void)snprintf(shell->transition_id, sizeof(shell->transition_id),
                   "lvgl-%ld-%llu", (long)getpid(),
                   (unsigned long long)shell->transition_sequence);
    (void)snprintf(shell->transition_component,
                   sizeof(shell->transition_component), "%s",
                   shell->apps[app_index].component);
    shell->transition_deadline = monotonic_ms() + LAUNCH_TRANSITION_TIMEOUT_MS;
    shell->transition_visible = 1;
    shell->transition_finishing = 0;
    set_label_if_changed(view, "transition_name", shell->apps[app_index].name);
    lv_obj_clean(host);
    if(bitmap_load(&shell->transition_icon,
                   shell->apps[app_index].icon_path, 84, 84) != 0)
        icon = make_image(host, &shell->transition_icon, 84, 84);
    else icon = make_fallback_icon(view, host, shell->apps[app_index].name, 84);
    if(icon != NULL) lv_obj_center(icon);
    lv_obj_update_layout(root);
    lv_obj_get_coords(host, &host_area);
    origin_center_x = area.x1 + shell->transition_origin_width / 2;
    origin_center_y = area.y1 + shell->transition_origin_height / 2;
    host_center_x = host_area.x1 + (int)lv_area_get_width(&host_area) / 2;
    host_center_y = host_area.y1 + (int)lv_area_get_height(&host_area) / 2;
    scale = shell->transition_origin_width * 256 / 84;
    if(scale < 64) scale = 64;
    if(scale > 384) scale = 384;
    duration = transition_motion_duration(shell);
    shell->transition_opening_until = monotonic_ms() + duration;
    lv_obj_set_style_transform_pivot_x(host, 42, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(host, 42, LV_PART_MAIN);
    transition_animate(host, transition_set_translate_x,
        origin_center_x - host_center_x, 0, duration);
    transition_animate(host, transition_set_translate_y,
        origin_center_y - host_center_y, 0, duration);
    transition_animate(host, transition_set_scale, scale, 256, duration);
    msys_ui_surface_show(view->surface);
    return 1;
}

static void complete_launch_transition(shell_state *shell)
{
    uint16_t duration;
    uint64_t now;
    uint64_t remaining;
    if(shell == NULL || shell->transition_visible == 0 ||
       shell->transition_finishing != 0)
        return;
    shell->transition_finishing = 1;
    shell->transition_deadline = 0u;
    duration = transition_motion_duration(shell);
    if(duration == 0u) {
        hide_launch_transition(shell);
        return;
    }
    now = monotonic_ms();
    if(now < shell->transition_opening_until) {
        remaining = shell->transition_opening_until - now;
        if(remaining > UINT32_MAX) remaining = UINT32_MAX;
        shell->transition_finish_timer = lv_timer_create(
            transition_opening_complete_cb, (uint32_t)remaining, shell);
        if(shell->transition_finish_timer == NULL)
            begin_transition_finish_feedback(shell);
        return;
    }
    begin_transition_finish_feedback(shell);
}

static ui_binding *new_binding(shell_state *shell, enum binding_action action,
                               size_t index)
{
    ui_binding *binding;
    if(shell->binding_count >= MAX_BINDINGS) return NULL;
    binding = &shell->bindings[shell->binding_count++];
    binding->shell = shell;
    binding->action = action;
    binding->index = index;
    binding->secondary = 0u;
    return binding;
}

static void request_apps(shell_state *shell);
static void request_tasks(shell_state *shell);
static void show_overview(shell_state *shell);
static void hide_overview(shell_state *shell);
static void show_notification_center(shell_state *shell);
static void hide_notification_center(shell_state *shell);
static void show_controls(shell_state *shell);
static void hide_controls(shell_state *shell);
static void render_launcher(shell_state *shell);
static void render_overview(shell_state *shell);
static void render_notifications(shell_state *shell);
static void request_wifi_inventory(shell_state *shell);
static void send_navigation(shell_state *shell, const char *action);
static int event_point(lv_event_t *event, lv_point_t *point);

static void start_app_at(shell_state *shell, size_t index, lv_obj_t *origin)
{
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 32u];
    char observe[MSYS_NATIVE_PATH_CAPACITY * 2u + 1024u];
    char escaped_component[MSYS_NATIVE_COMPONENT_CAPACITY * 2u];
    char escaped_icon[MSYS_NATIVE_PATH_CAPACITY * 2u];
    uint64_t begin_id = 0u;
    uint64_t start_id;
    if(index >= shell->app_count ||
       component_payload(shell->apps[index].component, payload,
                         sizeof(payload)) == 0 ||
       msys_native_json_escape(shell->apps[index].component,
            escaped_component, sizeof(escaped_component)) == 0 ||
       msys_native_json_escape(shell->apps[index].icon_path,
            escaped_icon, sizeof(escaped_icon)) == 0)
        return;
    if(show_launch_transition(shell, index, origin) != 0) {
        (void)snprintf(observe, sizeof(observe),
            "{\"transition_id\":\"%s\",\"component\":\"%s\","
            "\"icon\":\"%s\",\"color\":\"#f4f7fb\","
            "\"origin\":{\"x\":%d,\"y\":%d,\"width\":%d,"
            "\"height\":%d},\"timeout_ms\":%u}",
            shell->transition_id, escaped_component, escaped_icon,
            shell->transition_origin_x,
            shell->transition_origin_y + BAR_HEIGHT,
            shell->transition_origin_width, shell->transition_origin_height,
            LAUNCH_TRANSITION_TIMEOUT_MS);
        begin_id = send_call(shell, PENDING_BEGIN_TRANSITION, index,
            "role:window-manager", "begin_launch_transition", observe, 1);
        shell->transition_begin_call_id = begin_id;
        if(begin_id == 0u)
            fail_launch_transition(shell, "transition-observer-unavailable", 0);
    }
    start_id = send_call(shell, PENDING_START, index, "msys.core", "start",
                         payload, 0);
    if(shell->transition_visible != 0)
        shell->transition_start_call_id = start_id;
    if(start_id == 0u) {
        if(shell->transition_visible != 0)
            fail_launch_transition(shell, "core-start-unavailable",
                                   begin_id != 0u);
        else
            show_toast(shell, localized(shell, "无法启动应用",
                                        "Unable to start app"));
    }
}

static void start_app(shell_state *shell, size_t index)
{
    start_app_at(shell, index, NULL);
}

static void focus_task(shell_state *shell, size_t index)
{
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 32u];
    const char *target;
    const char *method;
    if(index >= shell->task_count) return;
    if(shell->tasks[index].component[0] != '\0') {
        if(component_payload(shell->tasks[index].component, payload,
                             sizeof(payload)) == 0)
            return;
        target = "msys.core";
        method = "start";
    }
    else {
        if(window_payload(shell->tasks[index].window_id, payload,
                          sizeof(payload)) == 0)
            return;
        target = "role:window-manager";
        method = "focus_window";
    }
    if(send_call(shell, PENDING_FOCUS, index, target, method, payload, 0) != 0u)
        hide_overview(shell);
}

static void close_task(shell_state *shell, size_t index)
{
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 32u];
    const char *target;
    const char *method;
    if(index >= shell->task_count) return;
    if(shell->tasks[index].component[0] != '\0') {
        if(component_payload(shell->tasks[index].component, payload,
                             sizeof(payload)) == 0)
            return;
        target = "msys.core";
        method = "stop";
    }
    else {
        if(window_payload(shell->tasks[index].window_id, payload,
                          sizeof(payload)) == 0)
            return;
        target = "role:window-manager";
        method = "close_window";
    }
    (void)send_call(shell, PENDING_CLOSE, index, target, method, payload, 0);
}

static int launcher_app_index(const shell_state *shell, const char *component,
                              size_t *result)
{
    size_t index;
    for(index = 0u; index < shell->app_count; index++) {
        if(strcmp(shell->apps[index].component, component) == 0) {
            if(result != NULL) *result = index;
            return 1;
        }
    }
    return 0;
}

static void launcher_open_software_center(shell_state *shell, size_t app_index,
                                          const char *action)
{
    char component[MSYS_NATIVE_COMPONENT_CAPACITY * 2u];
    char payload[768];
    enum pending_kind kind = strcmp(action, "uninstall") == 0
        ? PENDING_UNINSTALL : PENDING_DETAILS;
    if(app_index >= shell->app_count ||
       msys_native_json_escape(shell->apps[app_index].component, component,
                               sizeof(component)) == 0)
        return;
    (void)snprintf(payload, sizeof(payload),
        "{\"component\":\"org.msys.settings:software-center\","
        "\"activation\":{\"action\":\"software-center\",\"name\":\"%s\","
        "\"component\":\"%s\"}}", action, component);
    if(send_call(shell, kind, app_index, "msys.core", "start", payload, 1) == 0u)
        show_toast(shell, localized(shell, "软件中心不可用", "Software Center unavailable"));
}

static void launcher_quick_action(shell_state *shell, size_t app_index,
                                  size_t action_index)
{
    char payload[768];
    if(app_index >= shell->app_count ||
       action_index >= shell->apps[app_index].quick_action_count ||
       msys_native_quick_action_payload(
           &shell->apps[app_index].quick_actions[action_index],
           shell->apps[app_index].component, payload, sizeof(payload)) == 0)
        return;
    (void)send_call(shell, PENDING_QUICK_ACTION, app_index, "msys.core",
                    "activate", payload, 1);
}

static void launcher_commit(shell_state *shell)
{
    if(msys_native_launcher_layout_commit(&shell->launcher_layout) == 0)
        show_toast(shell, localized(shell, "无法保存桌面布局",
                                    "Unable to save Home layout"));
}

static void launcher_set_translate_x(void *object, int32_t value)
{
    lv_obj_set_style_translate_x(object, value, LV_PART_MAIN);
}

static void launcher_animate_translate(shell_state *shell, lv_obj_t *object,
                                       int from, int to)
{
    lv_anim_t animation;
    uint32_t duration = shell->preferences.animations_enabled != 0 &&
        shell->preferences.reduce_motion == 0 ? 150u : 0u;
    lv_anim_delete(object, launcher_set_translate_x);
    if(duration == 0u) {
        launcher_set_translate_x(object, to);
        return;
    }
    launcher_set_translate_x(object, from);
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_exec_cb(&animation, launcher_set_translate_x);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    (void)lv_anim_start(&animation);
}

static void launcher_clear_drop_preview(shell_state *shell)
{
    if(shell->launcher_drop_preview != NULL &&
       lv_obj_is_valid(shell->launcher_drop_preview))
        lv_obj_delete(shell->launcher_drop_preview);
    shell->launcher_drop_preview = NULL;
}

static lv_obj_t *launcher_preview_part(lv_obj_t *parent, int x, int y,
                                       int width, int height)
{
    lv_obj_t *part = lv_obj_create(parent);
    lv_obj_add_flag(part, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(part, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_size(part, width, height);
    lv_obj_set_style_radius(part, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(part, lv_color_hex(0x3f66e8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(part, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(part, 0, LV_PART_MAIN);
    return part;
}

static void launcher_show_drop_preview(shell_state *shell, lv_obj_t *target,
                                       enum msys_native_launcher_drop_mode mode,
                                       int edge_direction)
{
    lv_obj_t *grid = view_object(&shell->views[SURFACE_LAUNCHER], "app_grid");
    lv_obj_t *preview;
    lv_area_t grid_area;
    lv_area_t area;
    int width;
    int height;
    int index;
    launcher_clear_drop_preview(shell);
    if(grid == NULL || (target == NULL && edge_direction == 0)) return;
    lv_obj_get_coords(grid, &grid_area);
    if(target != NULL) lv_obj_get_coords(target, &area);
    else area = grid_area;
    width = (int)lv_area_get_width(&area);
    height = (int)lv_area_get_height(&area);
    preview = lv_obj_create(grid);
    shell->launcher_drop_preview = preview;
    lv_obj_add_flag(preview, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(preview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(preview, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(preview, 0, LV_PART_MAIN);
    if(edge_direction != 0) {
        lv_obj_set_pos(preview, edge_direction < 0 ? 0 :
            (int)lv_area_get_width(&grid_area) - 4, 0);
        lv_obj_set_size(preview, 4, (int)lv_area_get_height(&grid_area));
        (void)launcher_preview_part(preview, 0, 0, 4,
                                    (int)lv_area_get_height(&grid_area));
        return;
    }
    if(mode == MSYS_NATIVE_LAUNCHER_DROP_GROUP) {
        int dash_width = width > 36 ? 12 : 7;
        int dash_height = height > 48 ? 12 : 7;
        lv_obj_set_pos(preview, area.x1 - grid_area.x1,
                       area.y1 - grid_area.y1);
        lv_obj_set_size(preview, width, height);
        for(index = 3; index + dash_width < width; index += dash_width + 5) {
            (void)launcher_preview_part(preview, index, 1, dash_width, 2);
            (void)launcher_preview_part(preview, index, height - 3,
                                        dash_width, 2);
        }
        for(index = 4; index + dash_height < height; index += dash_height + 5) {
            (void)launcher_preview_part(preview, 1, index, 2, dash_height);
            (void)launcher_preview_part(preview, width - 3, index, 2,
                                        dash_height);
        }
    }
    else {
        int x = mode == MSYS_NATIVE_LAUNCHER_DROP_INSERT_BEFORE
            ? area.x1 - grid_area.x1 - 2 : area.x2 - grid_area.x1 + 1;
        lv_obj_set_pos(preview, x, area.y1 - grid_area.y1 + 4);
        lv_obj_set_size(preview, 3, height > 8 ? height - 8 : height);
        (void)launcher_preview_part(preview, 0, 0, 3,
                                    height > 8 ? height - 8 : height);
    }
}

static void launcher_reset_drag(shell_state *shell)
{
    shell->launcher_drag_source = -1;
    shell->launcher_drag_target = -1;
    shell->launcher_drag_folder = -1;
    shell->launcher_drag_member = -1;
    shell->launcher_drop_position = 0u;
    shell->launcher_drop_mode = MSYS_NATIVE_LAUNCHER_DROP_NONE;
    shell->launcher_dragging = 0;
    shell->launcher_drag_dx = 0;
    shell->launcher_drag_dy = 0;
    shell->launcher_drag_object = NULL;
    shell->launcher_edge_direction = 0;
    shell->launcher_edge_armed = 0;
    shell->launcher_edge_since = 0u;
    shell->launcher_drag_point_valid = 0;
    launcher_clear_drop_preview(shell);
}

static void launcher_page_enter(shell_state *shell, int direction, int carry)
{
    lv_obj_t *grid;
    int width;
    render_launcher(shell);
    grid = view_object(&shell->views[SURFACE_LAUNCHER], "app_grid");
    if(grid == NULL || direction == 0) return;
    width = lv_obj_get_width(grid);
    if(width <= 0) width = ROOT_WIDTH;
    launcher_animate_translate(shell, grid,
        (direction > 0 ? width : -width) + carry, 0);
}

static void launcher_select_page(shell_state *shell, int direction)
{
    unsigned int pages = msys_native_launcher_page_count(&shell->launcher_layout);
    unsigned int previous = shell->launcher_page;
    if(shell->launcher_folder >= 0) {
        shell->launcher_folder = -1;
        shell->launcher_editing = 0;
        render_launcher(shell);
        return;
    }
    if(direction < 0 && shell->launcher_page > 0u) shell->launcher_page--;
    else if(direction > 0 && shell->launcher_page + 1u < pages)
        shell->launcher_page++;
    if(shell->launcher_page != previous)
        launcher_page_enter(shell, direction, 0);
}

static void launcher_finish_edit(shell_state *shell)
{
    shell->launcher_editing = 0;
    shell->launcher_selected = -1;
    launcher_reset_drag(shell);
    render_launcher(shell);
}

static void launcher_update_drag_intent(shell_state *shell, size_t source,
                                        lv_point_t point)
{
    size_t indices[MSYS_NATIVE_LAUNCHER_MAX_ITEMS];
    size_t count;
    size_t slot;
    size_t nearest_slot = 0u;
    long nearest_distance = LONG_MAX;
    int edge = 0;
    uint64_t now = monotonic_ms();
    unsigned int pages = msys_native_launcher_page_count(&shell->launcher_layout);
    if(point.x <= LAUNCHER_EDGE_PX && shell->launcher_page > 0u) edge = -1;
    else if(point.x >= ROOT_WIDTH - LAUNCHER_EDGE_PX &&
            (shell->launcher_page + 1u < pages ||
             msys_native_launcher_page_items(&shell->launcher_layout,
                 shell->launcher_page, NULL, 0u) > 1u)) edge = 1;
    if(edge != 0) {
        if(shell->launcher_edge_direction != edge) {
            shell->launcher_edge_direction = edge;
            shell->launcher_edge_since = now;
            shell->launcher_edge_armed = 0;
            launcher_show_drop_preview(shell, NULL,
                MSYS_NATIVE_LAUNCHER_DROP_NONE, edge);
        }
        else if(shell->launcher_edge_armed == 0 &&
                now - shell->launcher_edge_since >= LAUNCHER_EDGE_DWELL_MS) {
            shell->launcher_edge_armed = edge;
            set_label_if_changed(&shell->views[SURFACE_LAUNCHER],
                "launcher_status", localized(shell,
                    edge < 0 ? "松开移到上一页" : "松开移到下一页",
                    edge < 0 ? "Release for previous page" :
                               "Release for next page"));
        }
        shell->launcher_drag_target = -1;
        shell->launcher_drop_mode = MSYS_NATIVE_LAUNCHER_DROP_NONE;
        return;
    }
    if(shell->launcher_edge_direction != 0) launcher_clear_drop_preview(shell);
    shell->launcher_edge_direction = 0;
    shell->launcher_edge_armed = 0;
    shell->launcher_edge_since = 0u;
    count = msys_native_launcher_page_items(&shell->launcher_layout,
        shell->launcher_page, indices, MSYS_NATIVE_LAUNCHER_MAX_ITEMS);
    shell->launcher_drag_target = -1;
    shell->launcher_drop_mode = MSYS_NATIVE_LAUNCHER_DROP_NONE;
    for(slot = 0u; slot < count; slot++) {
        size_t target_index = indices[slot];
        lv_obj_t *target = shell->launcher_tiles[target_index];
        lv_area_t area;
        long dx;
        long dy;
        int allow_group;
        enum msys_native_launcher_drop_mode mode;
        if(target_index == source || target == NULL) continue;
        lv_obj_get_coords(target, &area);
        dx = (long)point.x - ((long)area.x1 + (long)area.x2) / 2;
        dy = (long)point.y - ((long)area.y1 + (long)area.y2) / 2;
        if(dx * dx + dy * dy < nearest_distance) {
            nearest_distance = dx * dx + dy * dy;
            nearest_slot = slot;
        }
        allow_group = shell->preferences.folders_enabled != 0 &&
            shell->launcher_layout.items[source].kind == MSYS_NATIVE_LAUNCHER_APP &&
            (shell->launcher_layout.items[target_index].kind ==
                 MSYS_NATIVE_LAUNCHER_APP ||
             shell->launcher_layout.items[target_index].member_count <
                 MSYS_NATIVE_LAUNCHER_MAX_FOLDER_MEMBERS);
        mode = msys_native_launcher_drop_mode_at(point.x, point.y,
            area.x1, area.y1, area.x2, area.y2, allow_group);
        if(mode != MSYS_NATIVE_LAUNCHER_DROP_NONE) {
            if(shell->launcher_drag_target != (int)target_index ||
               shell->launcher_drop_mode != mode)
                launcher_show_drop_preview(shell, target, mode, 0);
            shell->launcher_drag_target = (int)target_index;
            shell->launcher_drop_mode = mode;
            shell->launcher_drop_position = slot +
                (mode == MSYS_NATIVE_LAUNCHER_DROP_INSERT_AFTER ? 1u : 0u);
            return;
        }
    }
    if(count > 1u && nearest_distance != LONG_MAX) {
        size_t target_index = indices[nearest_slot];
        lv_obj_t *target = shell->launcher_tiles[target_index];
        lv_area_t area;
        enum msys_native_launcher_drop_mode mode;
        lv_obj_get_coords(target, &area);
        mode = point.x < (area.x1 + area.x2) / 2
            ? MSYS_NATIVE_LAUNCHER_DROP_INSERT_BEFORE
            : MSYS_NATIVE_LAUNCHER_DROP_INSERT_AFTER;
        if(shell->launcher_drag_target != (int)target_index ||
           shell->launcher_drop_mode != mode)
            launcher_show_drop_preview(shell, target, mode, 0);
        shell->launcher_drag_target = (int)target_index;
        shell->launcher_drop_mode = mode;
        shell->launcher_drop_position = nearest_slot +
            (mode == MSYS_NATIVE_LAUNCHER_DROP_INSERT_AFTER ? 1u : 0u);
    }
    else {
        launcher_clear_drop_preview(shell);
    }
}

static void launcher_item_event_cb(lv_event_t *event)
{
    ui_binding *binding = lv_event_get_user_data(event);
    shell_state *shell;
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_point_t point;
    if(binding == NULL || binding->shell == NULL || object == NULL) return;
    shell = binding->shell;
    if(binding->index >= shell->launcher_layout.count) return;
    if(code == LV_EVENT_PRESSED && event_point(event, &point) != 0) {
        shell->launcher_pointer_consumed = 0;
        shell->launcher_drag_source = (int)binding->index;
        shell->launcher_drag_target = -1;
        shell->launcher_drag_origin = point;
        shell->launcher_drag_point = point;
        shell->launcher_drag_point_valid = 1;
        shell->launcher_drag_object = object;
        shell->launcher_dragging = 0;
        shell->launcher_drag_dx = 0;
        shell->launcher_drag_dy = 0;
        if(shell->preferences.animations_enabled != 0 &&
           shell->preferences.reduce_motion == 0)
            msys_ui_animate_press(object, shell->policy, true);
    }
    else if(code == LV_EVENT_LONG_PRESSED) {
        if(event_point(event, &point) != 0 &&
           (abs(point.x - shell->launcher_drag_origin.x) >
                LAUNCHER_SWIPE_SLOP_PX ||
            abs(point.y - shell->launcher_drag_origin.y) >
                LAUNCHER_SWIPE_SLOP_PX))
            return;
        shell->launcher_editing = 1;
        shell->launcher_selected = (int)binding->index;
        lv_obj_set_style_border_width(object, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(object, lv_color_hex(0x3f66e8), LV_PART_MAIN);
        set_label_if_changed(&shell->views[SURFACE_LAUNCHER], "launcher_status",
            localized(shell, "编辑模式：拖动排序或归入文件夹",
                      "Edit mode: drag to reorder or group"));
    }
    else if(code == LV_EVENT_PRESSING && shell->launcher_editing != 0 &&
            shell->launcher_drag_source == (int)binding->index &&
            event_point(event, &point) != 0) {
        int dx = point.x - shell->launcher_drag_origin.x;
        int dy = point.y - shell->launcher_drag_origin.y;
        shell->launcher_drag_point = point;
        shell->launcher_drag_point_valid = 1;
        if(abs(dx) > 5 || abs(dy) > 5) shell->launcher_dragging = 1;
        if(shell->launcher_dragging != 0) {
            if(dx != shell->launcher_drag_dx) {
                lv_obj_set_style_translate_x(object, (int16_t)dx, LV_PART_MAIN);
                shell->launcher_drag_dx = dx;
            }
            if(dy != shell->launcher_drag_dy) {
                lv_obj_set_style_translate_y(object, (int16_t)dy, LV_PART_MAIN);
                shell->launcher_drag_dy = dy;
            }
            launcher_update_drag_intent(shell, binding->index, point);
        }
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int source = shell->launcher_drag_source;
        int target = shell->launcher_drag_target;
        int changed = 0;
        if(shell->preferences.animations_enabled != 0 &&
           shell->preferences.reduce_motion == 0)
            msys_ui_animate_press(object, shell->policy, false);
        lv_obj_set_style_translate_x(object, 0, LV_PART_MAIN);
        lv_obj_set_style_translate_y(object, 0, LV_PART_MAIN);
        if(shell->launcher_dragging != 0) shell->launcher_pointer_consumed = 1;
        if(shell->launcher_editing != 0 &&
           source >= 0 && shell->launcher_dragging != 0) {
            if(target >= 0 &&
               shell->launcher_drop_mode == MSYS_NATIVE_LAUNCHER_DROP_GROUP) {
                msys_native_launcher_item *source_item =
                    &shell->launcher_layout.items[source];
                msys_native_launcher_item *target_item =
                    &shell->launcher_layout.items[target];
                size_t result = 0u;
                if(shell->preferences.folders_enabled != 0 &&
                   source_item->kind == MSYS_NATIVE_LAUNCHER_APP &&
                   target_item->kind == MSYS_NATIVE_LAUNCHER_FOLDER)
                    changed = msys_native_launcher_add_to_folder(
                        &shell->launcher_layout, (size_t)source, (size_t)target,
                        shell->launcher_page_capacity, &result);
                else if(shell->preferences.folders_enabled != 0 &&
                        source_item->kind == MSYS_NATIVE_LAUNCHER_APP &&
                        target_item->kind == MSYS_NATIVE_LAUNCHER_APP)
                    changed = msys_native_launcher_make_folder(
                        &shell->launcher_layout, (size_t)source, (size_t)target,
                        localized(shell, "新建文件夹", "New folder"),
                        shell->launcher_page_capacity, &result);
            }
            else if(target >= 0 &&
                    (shell->launcher_drop_mode ==
                         MSYS_NATIVE_LAUNCHER_DROP_INSERT_BEFORE ||
                     shell->launcher_drop_mode ==
                         MSYS_NATIVE_LAUNCHER_DROP_INSERT_AFTER)) {
                size_t result = 0u;
                changed = msys_native_launcher_move(&shell->launcher_layout,
                    (size_t)source, shell->launcher_page,
                    shell->launcher_drop_position,
                    shell->launcher_page_capacity, &result);
            }
            else if(shell->launcher_edge_armed != 0) {
                unsigned int target_page = shell->launcher_page;
                size_t result = 0u;
                if(shell->launcher_edge_armed < 0 && target_page > 0u)
                    target_page--;
                else if(shell->launcher_edge_armed > 0)
                    target_page++;
                if(target_page != shell->launcher_page) {
                    changed = msys_native_launcher_move(
                        &shell->launcher_layout, (size_t)source, target_page,
                        msys_native_launcher_page_items(&shell->launcher_layout,
                            target_page, NULL, 0u), shell->launcher_page_capacity,
                        &result);
                    if(changed != 0)
                        shell->launcher_page =
                            shell->launcher_layout.items[result].page;
                }
            }
            if(changed != 0) launcher_commit(shell);
            launcher_reset_drag(shell);
            render_launcher(shell);
            return;
        }
        launcher_reset_drag(shell);
    }
    else if(code == LV_EVENT_CLICKED && shell->launcher_pointer_consumed == 0) {
        const msys_native_launcher_item *item =
            &shell->launcher_layout.items[binding->index];
        if(item->kind == MSYS_NATIVE_LAUNCHER_FOLDER) {
            shell->launcher_editing = 0;
            shell->launcher_folder = (int)binding->index;
            render_launcher(shell);
        }
        else if(shell->launcher_editing == 0) {
            size_t app_index;
            if(launcher_app_index(shell, item->id, &app_index))
                start_app_at(shell, app_index, object);
        }
    }
}

static int launcher_folder_drop_is_outside(shell_state *shell,
                                           lv_obj_t *object,
                                           lv_point_t point)
{
    lv_obj_t *grid = view_object(&shell->views[SURFACE_LAUNCHER], "app_grid");
    lv_area_t area;
    uint32_t index;
    uint32_t count;
    if(grid == NULL) return 1;
    lv_obj_get_coords(grid, &area);
    if(point.x <= LAUNCHER_EDGE_PX ||
       point.x >= ROOT_WIDTH - LAUNCHER_EDGE_PX || point.y < area.y1 ||
       point.y > area.y2)
        return 1;

    /* A small long-press wobble should not pull an item out.  LVGL includes
     * style translation in the live coordinates, so recover the tile's
     * original slot before testing it. */
    lv_obj_get_coords(object, &area);
    area.x1 -= shell->launcher_drag_dx;
    area.x2 -= shell->launcher_drag_dx;
    area.y1 -= shell->launcher_drag_dy;
    area.y2 -= shell->launcher_drag_dy;
    if(point.x >= area.x1 && point.x <= area.x2 &&
       point.y >= area.y1 && point.y <= area.y2)
        return 0;

    /* Empty folder space is an intentional extract target.  Landing on a
     * sibling keeps the member in the folder until in-folder reorder exists. */
    count = lv_obj_get_child_count(grid);
    for(index = 0u; index < count; index++) {
        lv_obj_t *child = lv_obj_get_child(grid, (int32_t)index);
        if(child == NULL || child == object) continue;
        lv_obj_get_coords(child, &area);
        if(point.x >= area.x1 && point.x <= area.x2 &&
           point.y >= area.y1 && point.y <= area.y2)
            return 0;
    }
    return 1;
}

static void launcher_folder_member_event_cb(lv_event_t *event)
{
    ui_binding *binding = lv_event_get_user_data(event);
    shell_state *shell;
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    lv_point_t point;
    if(binding == NULL || binding->shell == NULL || object == NULL) return;
    shell = binding->shell;
    if(binding->index >= shell->launcher_layout.count ||
       shell->launcher_layout.items[binding->index].kind !=
           MSYS_NATIVE_LAUNCHER_FOLDER ||
       binding->secondary >=
           shell->launcher_layout.items[binding->index].member_count)
        return;
    if(code == LV_EVENT_PRESSED && event_point(event, &point) != 0) {
        shell->launcher_pointer_consumed = 0;
        shell->launcher_drag_folder = (int)binding->index;
        shell->launcher_drag_member = (int)binding->secondary;
        shell->launcher_drag_origin = point;
        shell->launcher_drag_point = point;
        shell->launcher_drag_point_valid = 1;
        shell->launcher_drag_object = object;
        shell->launcher_dragging = 0;
        shell->launcher_drag_dx = 0;
        shell->launcher_drag_dy = 0;
        if(shell->preferences.animations_enabled != 0 &&
           shell->preferences.reduce_motion == 0)
            msys_ui_animate_press(object, shell->policy, true);
    }
    else if(code == LV_EVENT_LONG_PRESSED) {
        if(event_point(event, &point) != 0 &&
           (abs(point.x - shell->launcher_drag_origin.x) >
                LAUNCHER_SWIPE_SLOP_PX ||
            abs(point.y - shell->launcher_drag_origin.y) >
                LAUNCHER_SWIPE_SLOP_PX))
            return;
        shell->launcher_editing = 1;
        lv_obj_set_style_border_width(object, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(object, lv_color_hex(0x3f66e8),
                                      LV_PART_MAIN);
        set_label_if_changed(&shell->views[SURFACE_LAUNCHER],
            "launcher_status", localized(shell,
                "拖到文件夹外移回桌面", "Drag outside the folder to Home"));
    }
    else if(code == LV_EVENT_PRESSING && shell->launcher_editing != 0 &&
            shell->launcher_drag_folder == (int)binding->index &&
            shell->launcher_drag_member == (int)binding->secondary &&
            event_point(event, &point) != 0) {
        int dx = point.x - shell->launcher_drag_origin.x;
        int dy = point.y - shell->launcher_drag_origin.y;
        shell->launcher_drag_point = point;
        shell->launcher_drag_point_valid = 1;
        if(abs(dx) > 5 || abs(dy) > 5) shell->launcher_dragging = 1;
        if(shell->launcher_dragging != 0) {
            if(dx != shell->launcher_drag_dx) {
                lv_obj_set_style_translate_x(object, (int16_t)dx, LV_PART_MAIN);
                shell->launcher_drag_dx = dx;
            }
            if(dy != shell->launcher_drag_dy) {
                lv_obj_set_style_translate_y(object, (int16_t)dy, LV_PART_MAIN);
                shell->launcher_drag_dy = dy;
            }
        }
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        int extracted = 0;
        int point_valid = event_point(event, &point) != 0;
        if(point_valid == 0 && shell->launcher_drag_point_valid != 0) {
            point = shell->launcher_drag_point;
            point_valid = 1;
        }
        if(shell->preferences.animations_enabled != 0 &&
           shell->preferences.reduce_motion == 0)
            msys_ui_animate_press(object, shell->policy, false);
        lv_obj_set_style_translate_x(object, 0, LV_PART_MAIN);
        lv_obj_set_style_translate_y(object, 0, LV_PART_MAIN);
        if(shell->launcher_dragging != 0) shell->launcher_pointer_consumed = 1;
        if(shell->launcher_dragging != 0 && point_valid != 0) {
            if(launcher_folder_drop_is_outside(shell, object, point) != 0) {
                const msys_native_launcher_item *folder =
                    &shell->launcher_layout.items[binding->index];
                size_t position = msys_native_launcher_page_items(
                    &shell->launcher_layout, folder->page, NULL, 0u);
                size_t result = 0u;
                extracted = msys_native_launcher_extract_folder_member(
                    &shell->launcher_layout, binding->index,
                    binding->secondary, folder->page, position,
                    shell->launcher_page_capacity, &result);
                if(extracted != 0) {
                    shell->launcher_page =
                        shell->launcher_layout.items[result].page;
                    shell->launcher_folder = -1;
                    shell->launcher_editing = 0;
                    launcher_commit(shell);
                }
            }
        }
        launcher_reset_drag(shell);
        if(extracted != 0) render_launcher(shell);
    }
    else if(code == LV_EVENT_CLICKED && shell->launcher_pointer_consumed == 0) {
        const msys_native_launcher_item *folder =
            &shell->launcher_layout.items[binding->index];
        size_t app_index;
        if(launcher_app_index(shell, folder->members[binding->secondary],
                              &app_index))
            start_app_at(shell, app_index, object);
    }
}

static void launcher_page_event_cb(lv_event_t *event)
{
    shell_state *shell = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *grid = lv_event_get_current_target(event);
    lv_point_t point;
    if(shell == NULL || grid == NULL) return;
    if(shell->launcher_editing != 0 || shell->launcher_folder >= 0) {
        if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
            shell->launcher_swipe_active = 0;
        return;
    }
    if(code == LV_EVENT_PRESSED && event_point(event, &point) != 0) {
        shell->launcher_swipe_active = 1;
        shell->launcher_swipe_origin = point;
        shell->launcher_swipe_dx = 0;
        shell->launcher_pointer_consumed = 0;
    }
    else if(code == LV_EVENT_PRESSING && shell->launcher_swipe_active != 0 &&
            event_point(event, &point) != 0) {
        int dx = point.x - shell->launcher_swipe_origin.x;
        int dy = point.y - shell->launcher_swipe_origin.y;
        unsigned int pages = msys_native_launcher_page_count(
            &shell->launcher_layout);
        if(abs(dy) > abs(dx) && abs(dy) > LAUNCHER_SWIPE_SLOP_PX) {
            shell->launcher_swipe_active = 0;
            launcher_animate_translate(shell, grid, shell->launcher_swipe_dx, 0);
            shell->launcher_swipe_dx = 0;
            return;
        }
        if(abs(dx) <= LAUNCHER_SWIPE_SLOP_PX) return;
        if((dx > 0 && shell->launcher_page == 0u) ||
           (dx < 0 && shell->launcher_page + 1u >= pages))
            dx /= 3;
        if(dx != shell->launcher_swipe_dx) {
            shell->launcher_swipe_dx = dx;
            shell->launcher_pointer_consumed = 1;
            launcher_set_translate_x(grid, dx);
        }
    }
    else if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
            shell->launcher_swipe_active != 0) {
        int dx = shell->launcher_swipe_dx;
        int page = (int)shell->launcher_page;
        int direction = 0;
        shell->launcher_swipe_active = 0;
        page = msys_native_launcher_swipe_page(shell->launcher_page,
            msys_native_launcher_page_count(&shell->launcher_layout), dx,
            lv_obj_get_width(grid));
        if(page != (int)shell->launcher_page) {
            direction = page > (int)shell->launcher_page ? 1 : -1;
            shell->launcher_page = (unsigned int)page;
            launcher_page_enter(shell, direction, dx);
        }
        else launcher_animate_translate(shell, grid, dx, 0);
        shell->launcher_swipe_dx = 0;
    }
}

static void item_event_cb(lv_event_t *event)
{
    ui_binding *binding = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    if(binding == NULL || binding->shell == NULL) return;
    if(code == LV_EVENT_PRESSED)
        msys_ui_animate_press(object, binding->shell->policy, true);
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        msys_ui_animate_press(object, binding->shell->policy, false);
    else if(code == LV_EVENT_CLICKED) {
        if(binding->action == BIND_START_APP)
            start_app(binding->shell, binding->index);
        else if(binding->action == BIND_FOCUS_TASK)
            focus_task(binding->shell, binding->index);
        else if(binding->action == BIND_CLOSE_TASK)
            close_task(binding->shell, binding->index);
        else if(binding->action == BIND_FOLDER_MEMBER)
            start_app(binding->shell, binding->index);
        else if(binding->action == BIND_LAUNCHER_PREVIOUS)
            launcher_select_page(binding->shell, -1);
        else if(binding->action == BIND_LAUNCHER_NEXT)
            launcher_select_page(binding->shell, 1);
        else if(binding->action == BIND_LAUNCHER_EDIT_DONE)
            launcher_finish_edit(binding->shell);
        else if(binding->action == BIND_LAUNCHER_DETAILS)
            launcher_open_software_center(binding->shell, binding->index, "details");
        else if(binding->action == BIND_LAUNCHER_UNINSTALL)
            launcher_open_software_center(binding->shell, binding->index, "uninstall");
        else if(binding->action == BIND_LAUNCHER_QUICK_ACTION)
            launcher_quick_action(binding->shell, binding->index,
                                  binding->secondary);
        else if(binding->action == BIND_LAUNCHER_TOGGLE_FOLDER_SIZE &&
                binding->index < binding->shell->launcher_layout.count) {
            msys_native_launcher_item *folder =
                &binding->shell->launcher_layout.items[binding->index];
            folder->large = folder->large == 0;
            launcher_commit(binding->shell);
            render_launcher(binding->shell);
        }
    }
}

static void feedback_event_cb(lv_event_t *event)
{
    shell_state *shell = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *object = lv_event_get_current_target(event);
    if(shell == NULL || object == NULL) return;
    if(code == LV_EVENT_PRESSED)
        msys_ui_animate_press(object, shell->policy, true);
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        msys_ui_animate_press(object, shell->policy, false);
}

static int event_point(lv_event_t *event, lv_point_t *point)
{
    lv_indev_t *indev = lv_event_get_indev(event);
    if(indev == NULL || point == NULL) return 0;
    lv_indev_get_point(indev, point);
    return 1;
}

static void pill_event_cb(lv_event_t *event)
{
    shell_state *shell = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *pill;
    lv_point_t point;
    if(shell == NULL) return;
    pill = view_object(&shell->views[SURFACE_NAVIGATION], "navigation_pill");
    if(pill == NULL) return;
    if(code == LV_EVENT_PRESSED && event_point(event, &point) != 0) {
        msys_native_gesture_begin(&shell->pill_gesture, point.y, monotonic_ms());
        msys_ui_animate_press(pill, shell->policy, true);
    }
    else if(code == LV_EVENT_PRESSING && event_point(event, &point) != 0) {
        enum msys_native_navigation_action action = msys_native_gesture_motion(
            &shell->pill_gesture, point.y, monotonic_ms());
        int inward = msys_native_gesture_inward(&shell->pill_gesture);
        if(inward > 12) inward = 12;
        lv_obj_set_style_translate_y(pill, (int16_t)-inward, LV_PART_MAIN);
        if(action == MSYS_NATIVE_NAV_APPS) show_overview(shell);
    }
    else if(code == LV_EVENT_RELEASED && event_point(event, &point) != 0) {
        int was_latched = shell->pill_gesture.latched;
        (void)msys_native_gesture_release(&shell->pill_gesture, point.y,
                                          monotonic_ms());
        lv_obj_set_style_translate_y(pill, 0, LV_PART_MAIN);
        msys_ui_animate_press(pill, shell->policy, false);
        if(was_latched == 0) send_navigation(shell, "home");
    }
    else if(code == LV_EVENT_PRESS_LOST) {
        shell->pill_gesture.active = 0;
        lv_obj_set_style_translate_y(pill, 0, LV_PART_MAIN);
        msys_ui_animate_press(pill, shell->policy, false);
    }
}

static lv_obj_t *make_image(lv_obj_t *parent, ui_bitmap *bitmap, int width,
                            int height)
{
    lv_obj_t *image;
    if(bitmap == NULL || bitmap->pixels == NULL) return NULL;
    image = lv_image_create(parent);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(image, &bitmap->descriptor);
    lv_obj_set_size(image, width, height);
    lv_obj_set_style_radius(image, 12, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(image, true, LV_PART_MAIN);
    return image;
}

static uint32_t launcher_color(const char *text, uint32_t fallback)
{
    char *end = NULL;
    unsigned long value;
    if(text == NULL || text[0] != '#' || strlen(text) != 7u) return fallback;
    value = strtoul(text + 1, &end, 16);
    return end != NULL && *end == '\0' ? (uint32_t)value : fallback;
}

static lv_obj_t *make_fallback_icon(shell_view *view, lv_obj_t *parent,
                                    const char *name, int size)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_t *symbol = lv_label_create(icon);
    char initial[5] = "M";
    size_t bytes = name != NULL ? strlen(name) : 0u;
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(icon, size, size);
    lv_obj_set_style_radius(icon, (int16_t)(size / 4), LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0x3f66e8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    if(bytes > 0u) {
        size_t take = ((unsigned char)name[0] & 0x80u) != 0u
            ? (bytes >= 3u ? 3u : bytes) : 1u;
        memcpy(initial, name, take);
        initial[take] = '\0';
    }
    lv_label_set_text(symbol, initial);
    lv_obj_set_style_text_color(symbol, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(symbol, msys_ui_theme_font(view->theme, 20),
                               LV_PART_MAIN);
    lv_obj_center(symbol);
    return icon;
}

static lv_obj_t *launcher_xml_root(shell_view *view)
{
    lv_obj_t *root = view_object(view, "launcher_root");
    if(root == NULL && view != NULL)
        root = msys_ui_document_root(view->document);
    return root;
}

static void launcher_resolve_grid_geometry(shell_view *view, lv_obj_t *root,
                                           lv_obj_t *grid)
{
    lv_obj_t *header = view_object(view, "launcher_header");
    lv_obj_t *status = view_object(view, "launcher_status");
    int32_t available;
    int32_t row_gap;
    if(root == NULL || grid == NULL || header == NULL || status == NULL) return;
    /* lv_xml_create can return a component instance wrapper. The named view
     * owns header/grid/status, so enforce its required vertical structure at
     * the Launcher boundary after resolving that actual XML object. */
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    /* The XML loader may expose empty self-closing siblings through its
     * component wrapper.  Launcher owns these three objects, so normalize
     * their parent and order once before applying the page layout. */
    if(lv_obj_get_parent(header) != root) lv_obj_set_parent(header, root);
    if(lv_obj_get_parent(grid) != root) lv_obj_set_parent(grid, root);
    if(lv_obj_get_parent(status) != root) lv_obj_set_parent(status, root);
    lv_obj_move_to_index(header, 0);
    lv_obj_move_to_index(grid, 1);
    lv_obj_move_to_index(status, 2);
    lv_obj_update_layout(root);
    row_gap = lv_obj_get_style_pad_row(root, LV_PART_MAIN);
    available = lv_obj_get_content_height(root) - lv_obj_get_height(header) -
        lv_obj_get_height(status) - row_gap * 2;
    if(available < 64) available = 64;
    lv_obj_set_flex_grow(grid, 0);
    lv_obj_set_height(grid, available);
    lv_obj_update_layout(root);
}

static void launcher_report_geometry(shell_state *shell, lv_obj_t *root,
                                     lv_obj_t *grid, size_t visible_count)
{
    lv_area_t root_area;
    lv_area_t grid_area;
    lv_area_t tile_area = {0, 0, 0, 0};
    lv_obj_t *tile = lv_obj_get_child_count(grid) > 0u
        ? lv_obj_get_child(grid, 0) : NULL;
    if(shell->apps_loaded == 0 || root == NULL || grid == NULL) return;
    lv_obj_update_layout(root);
    lv_obj_get_coords(root, &root_area);
    lv_obj_get_coords(grid, &grid_area);
    if(tile != NULL) lv_obj_get_coords(tile, &tile_area);
    fprintf(stderr,
        "msys-shell-lvgl: launcher geometry apps=%zu layout=%zu page=%u/%u "
        "visible=%zu children=%u root=%d,%d,%dx%d grid=%d,%d,%dx%d "
        "content=%dx%d tile=%s%d,%d,%dx%d\n",
        shell->app_count, shell->launcher_layout.count,
        shell->launcher_page + 1u,
        msys_native_launcher_page_count(&shell->launcher_layout),
        visible_count, (unsigned int)lv_obj_get_child_count(grid),
        (int)root_area.x1, (int)root_area.y1,
        (int)lv_area_get_width(&root_area), (int)lv_area_get_height(&root_area),
        (int)grid_area.x1, (int)grid_area.y1,
        (int)lv_area_get_width(&grid_area), (int)lv_area_get_height(&grid_area),
        (int)lv_obj_get_content_width(grid), (int)lv_obj_get_content_height(grid),
        tile == NULL ? "none:" : "",
        (int)tile_area.x1, (int)tile_area.y1,
        tile == NULL ? 0 : (int)lv_area_get_width(&tile_area),
        tile == NULL ? 0 : (int)lv_area_get_height(&tile_area));
    {
        lv_obj_t *hint = view_object(&shell->views[SURFACE_LAUNCHER],
                                     "launcher_hint");
        lv_obj_t *pager = view_object(&shell->views[SURFACE_LAUNCHER],
                                      "page_navigation");
        lv_area_t hint_area = {0, 0, 0, 0};
        lv_area_t pager_area = {0, 0, 0, 0};
        int overlap = 0;
        if(hint != NULL) lv_obj_get_coords(hint, &hint_area);
        if(pager != NULL) lv_obj_get_coords(pager, &pager_area);
        if(hint != NULL && pager != NULL && hint_area.x1 <= pager_area.x2 &&
           hint_area.x2 >= pager_area.x1 && hint_area.y1 <= pager_area.y2 &&
           hint_area.y2 >= pager_area.y1)
            overlap = 1;
        fprintf(stderr,
            "msys-shell-lvgl: launcher header hint=%d,%d,%dx%d "
            "pager=%d,%d,%dx%d overlap=%d\n",
            (int)hint_area.x1, (int)hint_area.y1,
            hint == NULL ? 0 : (int)lv_area_get_width(&hint_area),
            hint == NULL ? 0 : (int)lv_area_get_height(&hint_area),
            (int)pager_area.x1, (int)pager_area.y1,
            pager == NULL ? 0 : (int)lv_area_get_width(&pager_area),
            pager == NULL ? 0 : (int)lv_area_get_height(&pager_area), overlap);
    }
}

static void render_launcher(shell_state *shell)
{
    shell_view *view = &shell->views[SURFACE_LAUNCHER];
    lv_obj_t *grid = view_object(view, "app_grid");
    lv_obj_t *root = launcher_xml_root(view);
    size_t indices[MSYS_NATIVE_LAUNCHER_MAX_ITEMS];
    size_t count;
    size_t slot;
    unsigned int pages;
    int width;
    int height;
    int columns;
    int rows;
    int spacing;
    int icon_size;
    int tile_width;
    int tile_height;
    char page_text[24];
    shell->binding_count = 0u;
    if(grid == NULL) return;
    launcher_resolve_grid_geometry(view, root, grid);
    width = lv_obj_get_content_width(grid);
    height = lv_obj_get_content_height(grid);
    if(width <= 0) width = ROOT_WIDTH - 24;
    if(height <= 0) height = WORK_HEIGHT - 70;
    spacing = shell->preferences.icon_spacing;
    columns = shell->preferences.grid_columns > 0
        ? shell->preferences.grid_columns : (width >= 600 ? 6 : 3);
    icon_size = shell->preferences.icon_size;
    tile_height = icon_size + (shell->preferences.show_labels != 0 ? 34 : 16);
    rows = shell->preferences.grid_rows > 0 ? shell->preferences.grid_rows
        : (height + spacing) / (tile_height + spacing);
    if(rows < 1) rows = 1;
    if(rows > 6) rows = 6;
    shell->launcher_columns = columns;
    shell->launcher_rows = rows;
    shell->launcher_page_capacity = (size_t)columns * (size_t)rows;
    lv_obj_set_style_pad_row(grid, (int16_t)spacing, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, (int16_t)spacing, LV_PART_MAIN);
    memset(shell->launcher_tiles, 0, sizeof(shell->launcher_tiles));
    memset(shell->launcher_acrylic_panels, 0,
           sizeof(shell->launcher_acrylic_panels));
    shell->launcher_acrylic_panel_count = 0u;
    shell->launcher_drop_preview = NULL;
    launcher_set_translate_x(grid, 0);
    lv_obj_clean(grid);
    if(root != NULL) {
        lv_obj_set_style_bg_image_src(root, NULL, LV_PART_MAIN);
        lv_obj_set_style_bg_color(root, lv_color_hex(launcher_color(
            shell->preferences.wallpaper_color, 0xf4f6fa)), LV_PART_MAIN);
        if(shell->preferences.wallpaper_path[0] == '/' &&
           bitmap_load(&shell->wallpaper, shell->preferences.wallpaper_path,
                        ROOT_WIDTH, WORK_HEIGHT) != 0)
            lv_obj_set_style_bg_image_src(root, &shell->wallpaper.descriptor,
                                          LV_PART_MAIN);
        else
            bitmap_dispose(&shell->wallpaper);
    }
    (void)launcher_acrylic_prepare(shell);
    if(shell->apps_loaded != 0 &&
       msys_native_launcher_layout_reconcile(&shell->launcher_layout,
            shell->apps, shell->app_count, shell->launcher_page_capacity) != 0)
        launcher_commit(shell);
    pages = msys_native_launcher_page_count(&shell->launcher_layout);
    if(shell->launcher_page >= pages) shell->launcher_page = pages - 1u;
    count = msys_native_launcher_page_items(&shell->launcher_layout,
        shell->launcher_page, indices, MSYS_NATIVE_LAUNCHER_MAX_ITEMS);
    tile_width = (width - (columns - 1) * spacing) / columns;
    if(tile_width < 60) tile_width = 60;
    if(icon_size > tile_width - 10) icon_size = tile_width - 10;
    tile_height = icon_size + (shell->preferences.show_labels != 0 ? 32 : 12);
    if(shell->launcher_folder >= 0 &&
       (size_t)shell->launcher_folder < shell->launcher_layout.count &&
       shell->launcher_layout.items[shell->launcher_folder].kind ==
           MSYS_NATIVE_LAUNCHER_FOLDER) {
        const msys_native_launcher_item *folder =
            &shell->launcher_layout.items[shell->launcher_folder];
        set_label_if_changed(view, "launcher_title", folder->name);
        for(slot = 0u; slot < folder->member_count; slot++) {
            size_t app_index;
            ui_binding *binding;
            lv_obj_t *tile;
            lv_obj_t *content;
            lv_obj_t *icon;
            lv_obj_t *name;
            if(!launcher_app_index(shell, folder->members[slot], &app_index)) continue;
            binding = new_binding(shell, BIND_FOLDER_MEMBER,
                                  (size_t)shell->launcher_folder);
            if(binding != NULL) binding->secondary = slot;
            tile = launcher_tile_create(shell, grid, tile_width, tile_height,
                                        &content);
            if(bitmap_load(&shell->app_icons[app_index],
                           shell->apps[app_index].icon_path,
                           icon_size, icon_size) != 0)
                icon = make_image(content, &shell->app_icons[app_index],
                                  icon_size, icon_size);
            else
                icon = make_fallback_icon(view, content,
                                          shell->apps[app_index].name, icon_size);
            (void)icon;
            if(shell->preferences.show_labels != 0) {
                name = lv_label_create(content);
                lv_obj_set_width(name, tile_width - 8);
                lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
                lv_label_set_text(name, shell->apps[app_index].name);
                lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
                lv_obj_set_style_text_color(name, lv_color_hex(0x253047),
                                             LV_PART_MAIN);
                lv_obj_set_style_text_font(
                    name, msys_ui_theme_font(view->theme, 14), LV_PART_MAIN);
            }
            if(binding != NULL)
                lv_obj_add_event_cb(tile, launcher_folder_member_event_cb,
                                    LV_EVENT_ALL, binding);
        }
        (void)snprintf(page_text, sizeof(page_text), "%zu", folder->member_count);
        set_label_if_changed(view, "page_label", page_text);
        set_label_if_changed(view, "launcher_status",
            localized(shell, "点击左箭头关闭文件夹", "Use the left arrow to close"));
        launcher_acrylic_sync(shell, root);
        launcher_report_geometry(shell, root, grid, folder->member_count);
        return;
    }
    set_label_if_changed(view, "launcher_title", localized(shell, "应用", "Apps"));
    for(slot = 0u; slot < count; slot++) {
        size_t item_index = indices[slot];
        const msys_native_launcher_item *item =
            &shell->launcher_layout.items[item_index];
        size_t app_index = shell->app_count;
        ui_binding *binding = new_binding(shell, BIND_LAUNCHER_ITEM, item_index);
        lv_obj_t *tile;
        lv_obj_t *content;
        lv_obj_t *icon;
        lv_obj_t *name;
        int rendered_width;
        rendered_width = item->kind == MSYS_NATIVE_LAUNCHER_FOLDER &&
            item->large != 0 && shell->preferences.large_folders_enabled != 0
                ? tile_width * 2 + spacing : tile_width;
        tile = launcher_tile_create(shell, grid, rendered_width, tile_height,
                                    &content);
        shell->launcher_tiles[item_index] = tile;
        if(item->kind == MSYS_NATIVE_LAUNCHER_APP)
            (void)launcher_app_index(shell, item->id, &app_index);
        if(item->kind == MSYS_NATIVE_LAUNCHER_APP && app_index < shell->app_count &&
           bitmap_load(&shell->app_icons[app_index],
                        shell->apps[app_index].icon_path, icon_size, icon_size) != 0)
            icon = make_image(content, &shell->app_icons[app_index], icon_size,
                              icon_size);
        else
            icon = make_fallback_icon(view, content,
                item->kind == MSYS_NATIVE_LAUNCHER_FOLDER ? item->name :
                (app_index < shell->app_count ? shell->apps[app_index].name : "M"),
                icon_size);
        (void)icon;
        if(shell->preferences.show_labels != 0) {
            const char *label = item->kind == MSYS_NATIVE_LAUNCHER_FOLDER
                ? item->name : (app_index < shell->app_count
                    ? shell->apps[app_index].name : item->id);
            name = lv_label_create(content);
            lv_obj_set_width(name, tile_width - 8);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_label_set_text(name, label);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_color(name, lv_color_hex(0x253047), LV_PART_MAIN);
            lv_obj_set_style_text_font(name, msys_ui_theme_font(view->theme, 14),
                                       LV_PART_MAIN);
        }
        if(binding != NULL)
            lv_obj_add_event_cb(tile, launcher_item_event_cb, LV_EVENT_ALL, binding);
    }
    (void)snprintf(page_text, sizeof(page_text), "%u / %u",
                   shell->launcher_page + 1u, pages);
    set_label_if_changed(view, "page_label", page_text);
    set_label_if_changed(view, "launcher_status",
        shell->app_count == 0u
            ? localized(shell, "没有可启动的应用", "No launchable apps")
            : (shell->launcher_editing != 0
                ? localized(shell, "编辑模式：拖动排序或归入文件夹",
                            "Edit mode: drag to reorder or group") : ""));
    launcher_acrylic_sync(shell, root);
    launcher_report_geometry(shell, root, grid, count);
}

static void render_metrics(shell_state *shell)
{
    char text[80];
    if(msys_native_system_metrics_sample(&shell->metrics) != 0 &&
       msys_native_system_metrics_format(
           &shell->metrics, localized(shell, "CPU", "CPU"),
           localized(shell, "内存", "Memory"), text, sizeof(text)) != 0)
        set_label(&shell->views[SURFACE_OVERVIEW], "metrics", text);
}

static void render_overview(shell_state *shell)
{
    shell_view *view = &shell->views[SURFACE_OVERVIEW];
    lv_obj_t *grid = view_object(view, "task_grid");
    size_t index;
    bitmaps_dispose(shell->task_previews, MSYS_NATIVE_MAX_TASKS);
    if(grid == NULL) return;
    lv_obj_clean(grid);
    shell->binding_count = LAUNCHER_BINDING_CAPACITY;
    for(index = 0u; index < shell->task_count; index++) {
        ui_binding *focus = new_binding(shell, BIND_FOCUS_TASK, index);
        ui_binding *close = new_binding(shell, BIND_CLOSE_TASK, index);
        lv_obj_t *card = lv_button_create(grid);
        lv_obj_t *preview;
        lv_obj_t *row;
        lv_obj_t *title;
        lv_obj_t *memory;
        lv_obj_t *close_button;
        char memory_text[48];
        const char *display_name = msys_native_task_display_name(
            &shell->tasks[index], shell->apps, shell->app_count);
        lv_obj_set_size(card, 141, 154);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(0xd7deea), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 5, LV_PART_MAIN);
        lv_obj_set_style_pad_row(card, 3, LV_PART_MAIN);
        if(bitmap_load(&shell->task_previews[index], shell->tasks[index].thumbnail,
                       129, 91) != 0)
            preview = make_image(card, &shell->task_previews[index], 129, 91);
        else {
            preview = lv_obj_create(card);
            lv_obj_set_size(preview, 129, 91);
            lv_obj_set_style_radius(preview, 12, LV_PART_MAIN);
            lv_obj_set_style_bg_color(preview, lv_color_hex(0xe8edf5), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(preview, 0, LV_PART_MAIN);
            {
                lv_obj_t *placeholder = lv_label_create(preview);
                lv_label_set_text(placeholder, localized(shell, "等待截图", "No preview"));
                lv_obj_set_style_text_font(placeholder,
                    msys_ui_theme_font(view->theme, 14), LV_PART_MAIN);
                lv_obj_set_style_text_color(placeholder, lv_color_hex(0x69758a),
                                             LV_PART_MAIN);
                lv_obj_center(placeholder);
            }
        }
        (void)preview;
        row = lv_obj_create(card);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 129, 43);
        title = lv_label_create(row);
        lv_obj_set_width(title, 95);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_label_set_text(title, display_name);
        lv_obj_set_style_text_font(title, msys_ui_theme_font(view->theme, 14),
                                   LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0x253047), LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
        if(shell->tasks[index].pss_available != 0)
            (void)snprintf(memory_text, sizeof(memory_text), "PSS %.1f MB",
                           (double)shell->tasks[index].pss_kib / 1024.0);
        else if(shell->tasks[index].rss_available != 0)
            (void)snprintf(memory_text, sizeof(memory_text), "RSS %.1f MB",
                           (double)shell->tasks[index].rss_kib / 1024.0);
        else
            (void)snprintf(memory_text, sizeof(memory_text), "%s",
                           localized(shell, "内存 --", "Memory --"));
        memory = lv_label_create(row);
        lv_label_set_text(memory, memory_text);
        lv_obj_set_style_text_font(memory, msys_ui_theme_font(view->theme, 12),
                                   LV_PART_MAIN);
        lv_obj_set_style_text_color(memory, lv_color_hex(0x69758a), LV_PART_MAIN);
        lv_obj_align(memory, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        close_button = lv_button_create(row);
        lv_obj_set_size(close_button, 28, 28);
        lv_obj_align(close_button, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_radius(close_button, 14, LV_PART_MAIN);
        lv_obj_set_style_bg_color(close_button, lv_color_hex(0xf0f3f8), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(close_button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(close_button, 0, LV_PART_MAIN);
        {
            lv_obj_t *label = lv_label_create(close_button);
            lv_label_set_text(label, "×");
            lv_obj_set_style_text_color(label, lv_color_hex(0x5d6678), LV_PART_MAIN);
            lv_obj_center(label);
        }
        if(focus != NULL) lv_obj_add_event_cb(card, item_event_cb, LV_EVENT_ALL, focus);
        if(close != NULL)
            lv_obj_add_event_cb(close_button, item_event_cb, LV_EVENT_ALL, close);
    }
    set_label(view, "overview_status",
              shell->task_count == 0u
                  ? localized(shell, "没有最近任务", "No recent tasks")
                  : "");
    render_metrics(shell);
}

static void notification_time(const msys_native_notification *item,
                              char *output, size_t capacity)
{
    time_t seconds;
    struct tm local;
    if(item == NULL || output == NULL || capacity == 0u) return;
    seconds = (time_t)(item->timestamp_ms / 1000u);
    if(localtime_r(&seconds, &local) == NULL ||
       strftime(output, capacity, "%H:%M", &local) == 0u)
        (void)snprintf(output, capacity, "--:--");
}

static void render_notifications(shell_state *shell)
{
    shell_view *view = &shell->views[SURFACE_NOTIFICATION];
    lv_obj_t *list = view_object(view, "notification_list");
    char count_text[32];
    size_t index;
    if(list == NULL) return;
    lv_obj_clean(list);
    for(index = 0u; index < shell->notifications.count; index++) {
        const msys_native_notification *item =
            msys_native_notification_newest(&shell->notifications, index);
        lv_obj_t *card;
        lv_obj_t *header;
        lv_obj_t *title;
        lv_obj_t *time_label;
        lv_obj_t *message;
        char time_text[16];
        if(item == NULL) continue;
        card = lv_obj_create(list);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(0xd7deea), LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);
        header = lv_obj_create(card);
        lv_obj_remove_style_all(header);
        lv_obj_set_size(header, LV_PCT(100), 22);
        title = lv_label_create(header);
        lv_obj_set_width(title, 220);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_label_set_text(title, item->title[0] != '\0' ? item->title :
                          (item->source[0] != '\0' ? item->source :
                           localized(shell, "系统消息", "System message")));
        lv_obj_set_style_text_font(title, msys_ui_theme_font(view->theme, 16),
                                   LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(0x182033), LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);
        notification_time(item, time_text, sizeof(time_text));
        time_label = lv_label_create(header);
        lv_label_set_text(time_label, time_text);
        lv_obj_set_style_text_font(time_label,
                                   msys_ui_theme_font(view->theme, 12), LV_PART_MAIN);
        lv_obj_set_style_text_color(time_label, lv_color_hex(0x69758a), LV_PART_MAIN);
        lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, 0, 0);
        message = lv_label_create(card);
        lv_obj_set_width(message, LV_PCT(100));
        lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
        lv_label_set_text(message, item->message[0] != '\0' ? item->message :
                          localized(shell, "无正文", "No body"));
        lv_obj_set_style_text_font(message, msys_ui_theme_font(view->theme, 14),
                                   LV_PART_MAIN);
        lv_obj_set_style_text_color(message, lv_color_hex(0x4f5b70), LV_PART_MAIN);
    }
    (void)snprintf(count_text, sizeof(count_text),
                   shell->chinese != 0 ? "%zu 条" : "%zu", shell->notifications.count);
    set_label_if_changed(view, "notification_count", count_text);
    set_label_if_changed(view, "notification_empty",
                         shell->notifications.count == 0u
                             ? localized(shell, "暂无消息", "No notifications")
                             : "");
}

static void wifi_update_labels(shell_state *shell)
{
    char chrome[32];
    const char *status;
    if(shell->wifi_known == 0) {
        (void)snprintf(chrome, sizeof(chrome), "Wi-Fi ·");
        status = localized(shell, "正在读取实际状态…", "Reading actual state…");
    }
    else if(shell->wifi_available == 0) {
        (void)snprintf(chrome, sizeof(chrome), "Wi-Fi ×");
        status = localized(shell, "Wi-Fi 硬件不可用", "Wi-Fi hardware unavailable");
    }
    else if(shell->wifi_connected == 0) {
        (void)snprintf(chrome, sizeof(chrome), "Wi-Fi 0");
        status = localized(shell, "未连接", "Disconnected");
    }
    else {
        (void)snprintf(chrome, sizeof(chrome), "Wi-Fi %d", shell->wifi_signal_level);
        status = shell->wifi_signal_level >= 3
                     ? localized(shell, "已连接 · 信号强", "Connected · strong")
                     : (shell->wifi_signal_level == 2
                            ? localized(shell, "已连接 · 信号中", "Connected · medium")
                            : localized(shell, "已连接 · 信号弱", "Connected · weak"));
    }
    set_label_if_changed(&shell->views[SURFACE_CHROME], "wifi", chrome);
    set_label_if_changed(&shell->views[SURFACE_CONTROLS],
                         "control_wifi_status", status);
}

static void request_apps(shell_state *shell)
{
    set_label(&shell->views[SURFACE_LAUNCHER], "launcher_status",
              localized(shell, "正在读取应用…", "Loading apps…"));
    if(send_call(shell, PENDING_APPS, 0u, "msys.core", "list_apps", "{}", 1) ==
       0u) {
        set_label(&shell->views[SURFACE_LAUNCHER], "launcher_status",
                  localized(shell, "Core 暂不可用", "Core is unavailable"));
    }
}

static int pending_kind_active(const shell_state *shell, enum pending_kind kind)
{
    size_t index;
    for(index = 0u; index < MAX_PENDING; index++) {
        if(shell->pending[index].kind == kind && shell->pending[index].id != 0u)
            return 1;
    }
    return 0;
}

static void request_wifi_state(shell_state *shell)
{
    char escaped[MSYS_NATIVE_COMPONENT_CAPACITY * 2u];
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 48u];
    if(shell->wifi_device[0] == '\0' ||
       msys_native_json_escape(shell->wifi_device, escaped, sizeof(escaped)) == 0) {
        shell->wifi_known = 1;
        shell->wifi_available = 0;
        shell->wifi_connected = 0;
        shell->wifi_signal_level = 0;
        wifi_update_labels(shell);
        return;
    }
    (void)snprintf(payload, sizeof(payload),
                   "{\"id\":\"%s\",\"refresh\":false}", escaped);
    if(send_call(shell, PENDING_WIFI_STATE, 0u, "role:hal-manager", "get_state",
                 payload, 1) == 0u) {
        shell->wifi_known = 1;
        shell->wifi_available = 0;
        shell->wifi_connected = 0;
        shell->wifi_signal_level = 0;
        wifi_update_labels(shell);
    }
}

static void request_wifi_inventory(shell_state *shell)
{
    if(shell->supervised == 0) return;
    if(pending_kind_active(shell, PENDING_WIFI_INVENTORY) != 0 ||
       pending_kind_active(shell, PENDING_WIFI_STATE) != 0) {
        shell->next_wifi_refresh_at = monotonic_ms() + 1000u;
        return;
    }
    shell->next_wifi_refresh_at = monotonic_ms() + WIFI_REFRESH_INTERVAL_MS;
    if(send_call(shell, PENDING_WIFI_INVENTORY, 0u, "role:hal-manager",
                 "inventory", "{\"domains\":[\"network\"],\"refresh\":false}",
                 1) == 0u) {
        shell->wifi_known = 1;
        shell->wifi_available = 0;
        shell->wifi_connected = 0;
        shell->wifi_signal_level = 0;
        wifi_update_labels(shell);
    }
}

static void request_tasks(shell_state *shell)
{
    if(send_call(shell, PENDING_TASKS, 0u, "role:window-manager", "list_windows",
                 "{}", 1) == 0u) {
        shell->overview_pending = 0;
        show_toast(shell,
                   localized(shell, "窗口管理器暂不可用", "Window manager unavailable"));
    }
}

static void show_overview(shell_state *shell)
{
    if(shell->overview_visible != 0) return;
    hide_notification_center(shell);
    hide_controls(shell);
    hide_toast(shell);
    shell->overview_pending = 1;
    set_label(&shell->views[SURFACE_OVERVIEW], "overview_status",
              localized(shell, "正在读取最近任务…", "Loading recent tasks…"));
    request_tasks(shell);
}

static void present_overview(shell_state *shell)
{
    lv_obj_t *root;
    shell->overview_pending = 0;
    shell->overview_visible = 1;
    msys_ui_surface_show(shell->views[SURFACE_OVERVIEW].surface);
    root = msys_ui_document_root(shell->views[SURFACE_OVERVIEW].document);
    if(root != NULL) msys_ui_animate_opening(root, shell->policy);
}

static void hide_overview(shell_state *shell)
{
    shell->overview_pending = 0;
    if(shell->overview_visible == 0) return;
    shell->overview_visible = 0;
    msys_ui_surface_hide(shell->views[SURFACE_OVERVIEW].surface);
}

static void hide_notification_center(shell_state *shell)
{
    if(shell->notification_visible == 0) return;
    shell->notification_visible = 0;
    msys_ui_surface_hide(shell->views[SURFACE_NOTIFICATION].surface);
}

static void hide_controls(shell_state *shell)
{
    if(shell->controls_visible == 0) return;
    shell->controls_visible = 0;
    msys_ui_surface_hide(shell->views[SURFACE_CONTROLS].surface);
}

static void show_notification_center(shell_state *shell)
{
    if(shell->notification_visible != 0) {
        hide_notification_center(shell);
        return;
    }
    hide_overview(shell);
    hide_controls(shell);
    hide_toast(shell);
    render_notifications(shell);
    shell->notification_visible = 1;
    msys_ui_surface_show(shell->views[SURFACE_NOTIFICATION].surface);
}

static void show_controls(shell_state *shell)
{
    lv_obj_t *root;
    if(shell->controls_visible != 0) {
        hide_controls(shell);
        return;
    }
    hide_overview(shell);
    hide_notification_center(shell);
    hide_toast(shell);
    wifi_update_labels(shell);
    request_wifi_inventory(shell);
    shell->controls_visible = 1;
    msys_ui_surface_show(shell->views[SURFACE_CONTROLS].surface);
    root = msys_ui_document_root(shell->views[SURFACE_CONTROLS].document);
    if(root != NULL) msys_ui_animate_opening(root, shell->policy);
}

static void start_settings_panel(shell_state *shell, const char *panel)
{
    char payload[320];
    if(panel == NULL || pending_kind_active(shell, PENDING_SETTINGS) != 0) return;
    (void)snprintf(
        payload, sizeof(payload),
        "{\"component\":\"org.msys.settings:main\",\"activation\":{"
        "\"action\":\"settings-panel\",\"name\":\"%s\"}}",
        panel);
    if(send_call(shell, PENDING_SETTINGS, 0u, "msys.core", "start", payload, 0) ==
       0u)
        show_toast(shell, localized(shell, "设置不可用", "Settings unavailable"));
    else
        hide_controls(shell);
}

static void send_navigation(shell_state *shell, const char *action)
{
    char payload[96];
    if(strcmp(action, "apps") == 0) {
        if(shell->overview_visible != 0 || shell->overview_pending != 0)
            hide_overview(shell);
        else
            show_overview(shell);
        return;
    }
    if(strcmp(action, "back") == 0 &&
       (shell->overview_visible != 0 || shell->overview_pending != 0)) {
        hide_overview(shell);
        return;
    }
    if(strcmp(action, "back") == 0 && shell->notification_visible != 0) {
        hide_notification_center(shell);
        return;
    }
    if(strcmp(action, "back") == 0 && shell->controls_visible != 0) {
        hide_controls(shell);
        return;
    }
    if(strcmp(action, "home") == 0) {
        hide_overview(shell);
        hide_notification_center(shell);
        hide_controls(shell);
    }
    (void)snprintf(payload, sizeof(payload),
                   "{\"action\":\"%s\",\"input\":\"button\"}", action);
    (void)send_call(shell, PENDING_NAVIGATION, 0u, "role:window-manager",
                    "navigation_action", payload, 0);
}

static void xml_action_cb(lv_event_t *event)
{
    const char *action = lv_event_get_user_data(event);
    if(active_shell == NULL || action == NULL) return;
    if(strcmp(action, "back") == 0 || strcmp(action, "home") == 0 ||
       strcmp(action, "apps") == 0)
        send_navigation(active_shell, action);
    else if(strcmp(action, "close-overview") == 0)
        hide_overview(active_shell);
    else if(strcmp(action, "notifications") == 0)
        show_notification_center(active_shell);
    else if(strcmp(action, "controls") == 0)
        show_controls(active_shell);
    else if(strcmp(action, "notification-close") == 0)
        hide_notification_center(active_shell);
    else if(strcmp(action, "notification-clear") == 0) {
        (void)msys_native_notification_clear(&active_shell->notifications);
        render_notifications(active_shell);
    }
    else if(strcmp(action, "controls-close") == 0)
        hide_controls(active_shell);
    else if(strcmp(action, "control-wifi") == 0)
        start_settings_panel(active_shell, "wifi");
    else if(strcmp(action, "control-bluetooth") == 0)
        start_settings_panel(active_shell, "bluetooth");
    else if(strcmp(action, "control-settings") == 0)
        start_settings_panel(active_shell, "system");
    else if(strcmp(action, "launcher-previous") == 0)
        launcher_select_page(active_shell, -1);
    else if(strcmp(action, "launcher-next") == 0)
        launcher_select_page(active_shell, 1);
}

static int bind_document(lv_xml_component_scope_t *scope, void *user_data)
{
    shell_view *view = user_data;
    if(scope == NULL || view == NULL || view->theme == NULL) return -1;
    if(lv_xml_register_font(scope, "msys_12", msys_ui_theme_font(view->theme, 12)) !=
           LV_RESULT_OK ||
       lv_xml_register_font(scope, "msys_14", msys_ui_theme_font(view->theme, 14)) !=
           LV_RESULT_OK ||
       lv_xml_register_font(scope, "msys_16", msys_ui_theme_font(view->theme, 16)) !=
           LV_RESULT_OK ||
       lv_xml_register_font(scope, "msys_20", msys_ui_theme_font(view->theme, 20)) !=
           LV_RESULT_OK ||
       lv_xml_register_event_cb(scope, "shell_action", xml_action_cb) != LV_RESULT_OK)
        return -1;
    return 0;
}

static void add_feedback(shell_state *shell, enum shell_surface_id surface,
                         const char *name)
{
    lv_obj_t *object = view_object(&shell->views[surface], name);
    if(object != NULL)
        lv_obj_add_event_cb(object, feedback_event_cb, LV_EVENT_ALL, shell);
}

static void configure_interactions(shell_state *shell)
{
    static const struct {
        enum shell_surface_id surface;
        const char *name;
    } feedback[] = {
        {SURFACE_CHROME, "notifications"},
        {SURFACE_CHROME, "controls"},
        {SURFACE_NAVIGATION, "back_button"},
        {SURFACE_NAVIGATION, "home_button"},
        {SURFACE_NAVIGATION, "apps_button"},
        {SURFACE_OVERVIEW, "overview_close"},
        {SURFACE_NOTIFICATION, "notification_clear"},
        {SURFACE_NOTIFICATION, "notification_close"},
        {SURFACE_CONTROLS, "controls_close"},
        {SURFACE_CONTROLS, "control_wifi"},
        {SURFACE_CONTROLS, "control_bluetooth"},
        {SURFACE_CONTROLS, "control_settings"},
    };
    lv_obj_t *buttons;
    lv_obj_t *pill_area;
    lv_obj_t *pill;
    lv_obj_t *navigation_root;
    lv_obj_t *page_previous;
    lv_obj_t *page_next;
    lv_obj_t *launcher_grid;
    size_t index;
    for(index = 0u; index < sizeof(feedback) / sizeof(feedback[0]); index++)
        add_feedback(shell, feedback[index].surface, feedback[index].name);
    page_previous = view_object(&shell->views[SURFACE_LAUNCHER], "page_previous");
    page_next = view_object(&shell->views[SURFACE_LAUNCHER], "page_next");
    if(page_previous != NULL)
        lv_obj_add_event_cb(page_previous, xml_action_cb, LV_EVENT_CLICKED,
                            "launcher-previous");
    if(page_next != NULL)
        lv_obj_add_event_cb(page_next, xml_action_cb, LV_EVENT_CLICKED,
                            "launcher-next");
    launcher_grid = view_object(&shell->views[SURFACE_LAUNCHER], "app_grid");
    if(launcher_grid != NULL) {
        lv_obj_add_flag(launcher_grid, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(launcher_grid, launcher_page_event_cb,
                            LV_EVENT_ALL, shell);
    }
    buttons = view_object(&shell->views[SURFACE_NAVIGATION], "button_navigation");
    pill_area = view_object(&shell->views[SURFACE_NAVIGATION], "pill_navigation");
    pill = view_object(&shell->views[SURFACE_NAVIGATION], "navigation_pill");
    navigation_root = view_object(&shell->views[SURFACE_NAVIGATION],
                                  "navigation_root");
    if(navigation_root != NULL && buttons != NULL && pill_area != NULL) {
        if(lv_obj_get_parent(buttons) != navigation_root)
            lv_obj_set_parent(buttons, navigation_root);
        if(lv_obj_get_parent(pill_area) != navigation_root)
            lv_obj_set_parent(pill_area, navigation_root);
        lv_obj_move_to_index(buttons, 0);
        lv_obj_move_to_index(pill_area, 1);
        lv_obj_update_layout(navigation_root);
    }
    if(navigation_root != NULL && pill != NULL) {
        if(lv_obj_get_parent(pill) != navigation_root)
            lv_obj_set_parent(pill, navigation_root);
        lv_obj_add_flag(pill, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(pill, 94, 7);
        lv_obj_set_style_radius(pill, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pill, lv_color_hex(0x273247), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);
        lv_obj_center(pill);
        lv_obj_set_flag(pill, LV_OBJ_FLAG_HIDDEN, shell->buttons_mode != 0);
        lv_obj_update_layout(navigation_root);
    }
    if(buttons != NULL)
        lv_obj_set_flag(buttons, LV_OBJ_FLAG_HIDDEN, shell->buttons_mode == 0);
    if(pill_area != NULL)
        lv_obj_set_flag(pill_area, LV_OBJ_FLAG_HIDDEN, shell->buttons_mode != 0);
    if(pill_area != NULL)
        lv_obj_add_event_cb(pill_area, pill_event_cb, LV_EVENT_ALL, shell);
    if(pill_area != NULL) lv_obj_add_flag(pill_area, LV_OBJ_FLAG_CLICKABLE);
}

static void localize_documents(shell_state *shell)
{
    set_label(&shell->views[SURFACE_LAUNCHER], "launcher_title",
              localized(shell, "应用", "Apps"));
    set_label(&shell->views[SURFACE_LAUNCHER], "launcher_hint",
              localized(shell, "轻触启动，长按查看详情", "Tap to open"));
    set_label(&shell->views[SURFACE_OVERVIEW], "overview_title",
              localized(shell, "最近任务", "Recent tasks"));
    set_label(&shell->views[SURFACE_NOTIFICATION], "notification_title",
              localized(shell, "消息", "Notifications"));
    set_label(&shell->views[SURFACE_NOTIFICATION], "notification_clear_label",
              localized(shell, "清空", "Clear"));
    set_label(&shell->views[SURFACE_NOTIFICATION], "notification_close_label",
              localized(shell, "关闭", "Close"));
    set_label(&shell->views[SURFACE_CONTROLS], "controls_title",
              localized(shell, "控制中心", "Quick controls"));
    set_label(&shell->views[SURFACE_CONTROLS], "controls_close_label",
              localized(shell, "关闭", "Close"));
    set_label(&shell->views[SURFACE_CONTROLS], "control_bluetooth_title",
              localized(shell, "蓝牙", "Bluetooth"));
    set_label(&shell->views[SURFACE_CONTROLS], "control_bluetooth_status",
              localized(shell, "扫描、配对与设备管理", "Scan, pair and manage devices"));
    set_label(&shell->views[SURFACE_CONTROLS], "control_settings_title",
              localized(shell, "全部设置", "All settings"));
    set_label(&shell->views[SURFACE_CONTROLS], "control_settings_status",
              localized(shell, "系统、显示、语言与 HAL",
                        "System, display, language and HAL"));
}

static void update_clock(shell_state *shell)
{
    time_t current = time(NULL);
    struct tm local;
    char text[16];
    if(localtime_r(&current, &local) == NULL ||
       strftime(text, sizeof(text), "%H:%M:%S", &local) == 0u)
        return;
    if(strcmp(text, shell->clock_text) != 0) {
        (void)snprintf(shell->clock_text, sizeof(shell->clock_text), "%s", text);
        set_label(&shell->views[SURFACE_CHROME], "clock", text);
    }
}

static int resolve_ui_dir(char *output, size_t capacity)
{
    const char *environment = getenv("MSYS_SHELL_UI_DIR");
    char executable[PATH_MAX];
    ssize_t length;
    char *files;
    if(environment != NULL && environment[0] != '\0') {
        int written = snprintf(output, capacity, "%s", environment);
        return written >= 0 && (size_t)written < capacity;
    }
    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1u);
    if(length > 0) {
        executable[length] = '\0';
        files = strstr(executable, "/files/bin/");
        if(files != NULL) {
            int written;
            *files = '\0';
            written = snprintf(output, capacity, "%s/files/share/ui/shell", executable);
            if(written >= 0 && (size_t)written < capacity) return 1;
        }
    }
    return snprintf(output, capacity, "files/share/ui/shell") > 0;
}

static int load_document(shell_state *shell, enum shell_surface_id id,
                         const char *filename)
{
    shell_view *view = &shell->views[id];
    msys_ui_document_config_t config = {
        .max_bytes = 64u * 1024u,
        .bind = bind_document,
        .user_data = view,
    };
    int written = snprintf(view->path, sizeof(view->path), "%s/%s", shell->ui_dir,
                           filename);
    if(written < 0 || (size_t)written >= sizeof(view->path)) return 0;
    view->document = msys_ui_document_create(msys_ui_surface_screen(view->surface),
                                              &config);
    return view->document != NULL &&
           msys_ui_document_load_file(view->document, view->path) ==
               MSYS_UI_DOCUMENT_OK;
}

static int create_view(shell_state *shell, enum shell_surface_id id,
                       const msys_ui_surface_config_t *config)
{
    shell_view *view = &shell->views[id];
    view->shell = shell;
    view->id = id;
    view->surface = msys_ui_surface_create(shell->runtime, config);
    if(view->surface == NULL) return 0;
    view->theme = msys_ui_theme_create(msys_ui_surface_display(view->surface),
                                       shell->policy);
    if(view->theme == NULL) return 0;
    msys_ui_theme_set_font_provider(view->theme, msys_ui_font_provider, NULL,
                                    shell->chinese != 0 ? "zh-CN" : "en-US");
    return 1;
}

static int initialize_ui(shell_state *shell, const char *display_name,
                         msys_ui_output_t output, bool reduced_motion)
{
    msys_ui_runtime_config_t runtime_config = {
        .display_name = display_name,
        .output = output,
        .reduced_motion = reduced_motion,
    };
    const msys_ui_surface_config_t configs[SURFACE_COUNT] = {
        {.x=0, .y=BAR_HEIGHT, .width=ROOT_WIDTH, .height=WORK_HEIGHT,
         .draw_rows=DRAW_ROWS, .title="MSYS Launcher",
         .app_id="org.msys.shell.native.launcher",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="launcher", .wm_instance="msys-shell-lvgl",
         .override_redirect=false},
        {.x=0, .y=0, .width=ROOT_WIDTH, .height=BAR_HEIGHT,
         .draw_rows=BAR_HEIGHT, .title="MSYS Chrome",
         .app_id="org.msys.shell.native.chrome",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="system-chrome", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
        {.x=0, .y=ROOT_HEIGHT-BAR_HEIGHT, .width=ROOT_WIDTH, .height=BAR_HEIGHT,
         .draw_rows=BAR_HEIGHT, .title="MSYS Navigation",
         .app_id="org.msys.shell.native.navigation-pill",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="navigation-bar", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
        {.x=0, .y=BAR_HEIGHT, .width=ROOT_WIDTH, .height=WORK_HEIGHT,
         .draw_rows=DRAW_ROWS, .title="MSYS Recents",
         .app_id="org.msys.shell.task-switcher",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="task-switcher", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
        {.x=0, .y=BAR_HEIGHT, .width=ROOT_WIDTH, .height=WORK_HEIGHT,
         .draw_rows=DRAW_ROWS, .title="MSYS Notification Center",
         .app_id="org.msys.shell.native.notification-center",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="notification-center", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
        {.x=0, .y=BAR_HEIGHT, .width=ROOT_WIDTH, .height=WORK_HEIGHT,
         .draw_rows=DRAW_ROWS, .title="MSYS Quick Controls",
         .app_id="org.msys.shell.native.quick-controls",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="quick-controls", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
        {.x=10, .y=BAR_HEIGHT+10, .width=ROOT_WIDTH-20, .height=76,
         .draw_rows=48, .title="MSYS Notifications",
         .app_id="org.msys.shell.native.notifications",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="notification-presenter", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
        {.x=0, .y=BAR_HEIGHT, .width=ROOT_WIDTH, .height=WORK_HEIGHT,
         .draw_rows=DRAW_ROWS, .title="MSYS Launch Transition",
         .app_id="org.msys.shell.transitions",
         .component_id="org.msys.shell.native:desktop-shell-lvgl",
         .role="transition-presenter", .wm_instance="msys-shell-lvgl",
         .override_redirect=true},
    };
    const char *documents[SURFACE_COUNT] = {
        "launcher.xml", "chrome.xml", "navigation.xml", "overview.xml",
        "notification.xml", "controls.xml", "toast.xml", "transition.xml"
    };
    size_t index;
    shell->runtime = msys_ui_runtime_create(&runtime_config);
    if(shell->runtime == NULL) return 0;
    shell->policy = msys_ui_runtime_policy(shell->runtime);
    (void)msys_ui_dynamic_fonts_init(NULL);
    for(index = 0u; index < SURFACE_COUNT; index++) {
        if(create_view(shell, (enum shell_surface_id)index, &configs[index]) == 0 ||
           load_document(shell, (enum shell_surface_id)index, documents[index]) == 0) {
            fprintf(stderr, "msys-shell-lvgl: cannot load %s\n", documents[index]);
            return 0;
        }
    }
    configure_interactions(shell);
    localize_documents(shell);
    render_launcher(shell);
    render_notifications(shell);
    wifi_update_labels(shell);
    set_label(&shell->views[SURFACE_LAUNCHER], "launcher_status",
              localized(shell, "正在读取应用…", "Loading apps…"));
    update_clock(shell);
    msys_ui_surface_show(shell->views[SURFACE_LAUNCHER].surface);
    msys_ui_surface_show(shell->views[SURFACE_CHROME].surface);
    msys_ui_surface_show(shell->views[SURFACE_NAVIGATION].surface);
    return 1;
}

static int initialize_ipc(shell_state *shell)
{
    char type[32];
    if(getenv("MSYS_CONTROL_FD") == NULL) {
        shell->supervised = 0;
        set_label(&shell->views[SURFACE_LAUNCHER], "launcher_status",
                  localized(shell, "未连接 Core（预览模式）",
                            "Core is disconnected (preview mode)"));
        return 1;
    }
    shell->supervised = 1;
    if(msys_mipc_client_from_env(&shell->ipc) != MSYS_MIPC_OK ||
       msys_mipc_send_hello_from_env(&shell->ipc) != MSYS_MIPC_OK ||
       msys_mipc_recv_json(&shell->ipc, shell->packet, MSYS_MIPC_RECV_CAPACITY,
                           3000, NULL) != MSYS_MIPC_OK ||
       msys_mipc_json_get_string(shell->packet, "type", type, sizeof(type), NULL) !=
           MSYS_MIPC_OK ||
       strcmp(type, "welcome") != 0) {
        fprintf(stderr, "msys-shell-lvgl: component handshake failed\n");
        return 0;
    }
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.install.package_changed");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.lifecycle.transition");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.timezone.changed");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.session.preferences.changed");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.hal.changed");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.role.notification-presenter");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.notification.post");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.window.transition");
    if(msys_mipc_send_ready(&shell->ipc) != MSYS_MIPC_OK) return 0;
    (void)msys_mipc_send_event_json(
        &shell->ipc, "msys.role.ready",
        "{\"role\":\"lvgl-shell\",\"roles\":[\"launcher\",\"system-chrome\",\"navigation-bar\",\"task-switcher\",\"notification-presenter\",\"notification-center\",\"transition-presenter\"]}");
    request_apps(shell);
    request_wifi_inventory(shell);
    return 1;
}

static int json_boolean_true(const char *object, const char *key)
{
    const char *raw = NULL;
    size_t length = 0u;
    return object != NULL && key != NULL &&
        msys_mipc_json_get_raw(object, key, &raw, &length) == MSYS_MIPC_OK &&
        length == 4u && memcmp(raw, "true", 4u) == 0;
}

static int begin_transition_reply_matches(shell_state *shell,
                                          const char *payload)
{
    char schema[64];
    char transition_id[97];
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    return shell->transition_visible != 0 && payload != NULL &&
        json_boolean_true(payload, "ok") &&
        msys_mipc_json_get_string(payload, "schema", schema,
                                  sizeof(schema), NULL) == MSYS_MIPC_OK &&
        strcmp(schema, "msys.window-transition.v1") == 0 &&
        msys_mipc_json_get_string(payload, "transition_id", transition_id,
                                  sizeof(transition_id), NULL) == MSYS_MIPC_OK &&
        msys_mipc_json_get_string(payload, "component", component,
                                  sizeof(component), NULL) == MSYS_MIPC_OK &&
        strcmp(transition_id, shell->transition_id) == 0 &&
        strcmp(component, shell->transition_component) == 0;
}

static void handle_reply(shell_state *shell, const char *packet,
                         const char *type)
{
    uint64_t id;
    pending_call *owned;
    pending_call call;
    char *payload = NULL;
    int success = strcmp(type, "return") == 0;
    if(msys_mipc_json_get_u64(packet, "id", &id) != MSYS_MIPC_OK) return;
    owned = pending_find(shell, id);
    if(owned == NULL) return;
    call = *owned;
    memset(owned, 0, sizeof(*owned));
    if(success == 0 || json_payload_copy(packet, &payload) == 0) {
        if(call.kind == PENDING_BEGIN_TRANSITION &&
           call.id == shell->transition_begin_call_id)
            fail_launch_transition(shell, "transition-observer-error", 0);
        else if(call.kind == PENDING_START) {
            if(call.id == shell->transition_start_call_id &&
               shell->transition_visible != 0)
                fail_launch_transition(shell, "core-start-error", 1);
            else
                show_toast(shell, localized(shell, "无法启动应用",
                                            "Unable to start app"));
        }
        else if(call.kind == PENDING_TASKS) {
            shell->overview_pending = 0;
            show_toast(shell, localized(shell, "无法读取最近任务",
                                       "Unable to load recent tasks"));
        }
        else if(call.kind == PENDING_APPS)
            set_label(&shell->views[SURFACE_LAUNCHER], "launcher_status",
                      localized(shell, "无法读取应用列表", "Unable to load apps"));
        else if(call.kind == PENDING_WIFI_INVENTORY ||
                call.kind == PENDING_WIFI_STATE) {
            shell->wifi_known = 1;
            shell->wifi_available = 0;
            shell->wifi_connected = 0;
            shell->wifi_signal_level = 0;
            shell->wifi_device[0] = '\0';
            wifi_update_labels(shell);
        }
        else if(call.kind == PENDING_SETTINGS)
            show_toast(shell, localized(shell, "设置不可用", "Settings unavailable"));
        return;
    }
    if(call.kind == PENDING_APPS) {
        if(msys_native_parse_apps(payload, shell->apps, MSYS_NATIVE_MAX_APPS,
                                  &shell->app_count) != 0) {
            shell->apps_loaded = 1;
            render_launcher(shell);
            if(shell->overview_visible != 0) render_overview(shell);
        }
        else {
            shell->app_count = 0u;
            render_launcher(shell);
            set_label(&shell->views[SURFACE_LAUNCHER], "launcher_status",
                      localized(shell, "应用列表格式无效",
                                "The app list is invalid"));
        }
    }
    else if(call.kind == PENDING_BEGIN_TRANSITION) {
        if(call.id == shell->transition_begin_call_id &&
           begin_transition_reply_matches(shell, payload) == 0)
            fail_launch_transition(shell, "invalid-transition-response", 1);
    }
    else if(call.kind == PENDING_START) {
        /* A successful Core start is not visual completion. The exact
         * surface-ready event owns the transition lifetime. */
    }
    else if(call.kind == PENDING_TASKS) {
        if(msys_native_parse_tasks(payload, shell->tasks, MSYS_NATIVE_MAX_TASKS,
                                   &shell->task_count) != 0) {
            render_overview(shell);
            if(shell->overview_pending != 0) present_overview(shell);
            (void)send_call(shell, PENDING_TASK_RESOURCES, 0u, "msys.core",
                            "foreground_stack", "{\"include_resources\":true}", 1);
        }
        else {
            shell->overview_pending = 0;
            show_toast(shell, localized(shell, "最近任务数据无效",
                                       "Recent task data is invalid"));
        }
    }
    else if(call.kind == PENDING_TASK_RESOURCES) {
        (void)msys_native_apply_task_resources(payload, shell->tasks,
                                                shell->task_count);
        if(shell->overview_visible != 0) render_overview(shell);
    }
    else if(call.kind == PENDING_CLOSE) {
        if(shell->overview_visible != 0) request_tasks(shell);
    }
    else if(call.kind == PENDING_WIFI_INVENTORY) {
        if(msys_native_parse_wifi_device(payload, shell->wifi_device,
                                         sizeof(shell->wifi_device)) != 0 &&
           shell->wifi_device[0] != '\0') {
            shell->wifi_available = 1;
            request_wifi_state(shell);
        }
        else {
            shell->wifi_known = 1;
            shell->wifi_available = 0;
            shell->wifi_connected = 0;
            shell->wifi_signal_level = 0;
            shell->wifi_device[0] = '\0';
            wifi_update_labels(shell);
        }
    }
    else if(call.kind == PENDING_WIFI_STATE) {
        int connected = 0;
        int signal_known = 0;
        int signal_dbm = 0;
        if(msys_native_parse_wifi_state(payload, &connected, &signal_known,
                                        &signal_dbm) != 0) {
            shell->wifi_known = 1;
            shell->wifi_available = 1;
            shell->wifi_connected = connected;
            shell->wifi_signal_level = msys_native_wifi_signal_level(
                connected, signal_known, signal_dbm);
        }
        else {
            shell->wifi_known = 1;
            shell->wifi_available = 0;
            shell->wifi_connected = 0;
            shell->wifi_signal_level = 0;
        }
        wifi_update_labels(shell);
    }
    free(payload);
}

static void handle_notification_call(shell_state *shell, uint64_t id,
                                     const char *method, const char *payload)
{
    char small[128];
    if(strcmp(method, "show") == 0) {
        if(shell->notification_visible == 0) show_notification_center(shell);
    }
    else if(strcmp(method, "hide") == 0)
        hide_notification_center(shell);
    else if(strcmp(method, "toggle") == 0)
        show_notification_center(shell);
    else if(strcmp(method, "clear") == 0) {
        size_t removed = msys_native_notification_clear(&shell->notifications);
        render_notifications(shell);
        (void)snprintf(small, sizeof(small),
                       "{\"removed\":%zu,\"count\":0,\"visible\":%s}",
                       removed, shell->notification_visible != 0 ? "true" : "false");
        (void)msys_mipc_send_return_json(&shell->ipc, id, small);
        return;
    }
    else if(strcmp(method, "list") == 0) {
        uint64_t requested = shell->notifications.limit;
        char *response = malloc(64u * 1024u);
        if(payload != NULL)
            (void)msys_mipc_json_get_u64(payload, "limit", &requested);
        if(requested > shell->notifications.limit)
            requested = shell->notifications.limit;
        if(response == NULL ||
           msys_native_notification_list_json(
               &shell->notifications, (size_t)requested,
               shell->notification_visible, response, 64u * 1024u) == 0)
            (void)msys_mipc_send_error(&shell->ipc, id, "RESPONSE_TOO_LARGE",
                                       "notification list");
        else
            (void)msys_mipc_send_return_json(&shell->ipc, id, response);
        free(response);
        return;
    }
    else {
        (void)msys_mipc_send_error(&shell->ipc, id, "NO_METHOD", method);
        return;
    }
    (void)snprintf(small, sizeof(small), "{\"visible\":%s,\"count\":%zu}",
                   shell->notification_visible != 0 ? "true" : "false",
                   shell->notifications.count);
    (void)msys_mipc_send_return_json(&shell->ipc, id, small);
}

static void apply_launcher_preferences(shell_state *shell)
{
    lv_obj_t *buttons;
    lv_obj_t *pill_area;
    lv_obj_t *pill;
    shell->legacy_navigation_override = 0;
    shell->buttons_mode = strcmp(shell->preferences.navigation_mode, "buttons") == 0;
    buttons = view_object(&shell->views[SURFACE_NAVIGATION], "button_navigation");
    pill_area = view_object(&shell->views[SURFACE_NAVIGATION], "pill_navigation");
    pill = view_object(&shell->views[SURFACE_NAVIGATION], "navigation_pill");
    if(buttons != NULL)
        lv_obj_set_flag(buttons, LV_OBJ_FLAG_HIDDEN, shell->buttons_mode == 0);
    if(pill_area != NULL)
        lv_obj_set_flag(pill_area, LV_OBJ_FLAG_HIDDEN, shell->buttons_mode != 0);
    if(pill != NULL)
        lv_obj_set_flag(pill, LV_OBJ_FLAG_HIDDEN, shell->buttons_mode != 0);
    render_launcher(shell);
}

static void handle_call(shell_state *shell, const char *packet)
{
    uint64_t id;
    char method[96];
    char logical_target[MSYS_NATIVE_COMPONENT_CAPACITY] = "";
    char *payload = NULL;
    if(msys_mipc_json_get_u64(packet, "id", &id) != MSYS_MIPC_OK ||
       msys_mipc_json_get_string(packet, "method", method, sizeof(method), NULL) !=
           MSYS_MIPC_OK)
        return;
    (void)msys_mipc_json_get_string(packet, "logical_target", logical_target,
                                    sizeof(logical_target), NULL);
    (void)json_payload_copy(packet, &payload);
    if(strcmp(logical_target, "role:notification-center") == 0) {
        handle_notification_call(shell, id, method, payload);
        free(payload);
        return;
    }
    if(strcmp(method, "get_preferences") == 0) {
        char response[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
        if(payload == NULL || msys_native_preferences_empty_request(payload) == 0 ||
           msys_native_preferences_state_json(&shell->preferences, response,
                                               sizeof(response)) == 0)
            (void)msys_mipc_send_error(&shell->ipc, id, "BAD_PREFERENCES",
                                       "invalid preference request");
        else
            (void)msys_mipc_send_return_json(&shell->ipc, id, response);
        free(payload);
        return;
    }
    if(strcmp(method, "set_preferences") == 0 ||
       strcmp(method, "reset_preferences") == 0) {
        msys_native_preferences candidate;
        char response[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
        char changed[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
        int reset = strcmp(method, "reset_preferences") == 0;
        enum msys_native_preferences_result result;
        if(reset != 0) {
            if(payload == NULL || msys_native_preferences_empty_request(payload) == 0)
                result = MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
            else {
                msys_native_preferences_defaults(&candidate);
                result = MSYS_NATIVE_PREFERENCES_OK;
            }
        }
        else result = payload != NULL
            ? msys_native_preferences_merge(payload, &shell->preferences, &candidate)
            : MSYS_NATIVE_PREFERENCES_BAD_REQUEST;
        if(result == MSYS_NATIVE_PREFERENCES_OK) {
            candidate.revision = shell->preferences.revision + 1u;
            result = msys_native_preferences_commit(shell->preferences_path,
                                                     &candidate);
        }
        if(result != MSYS_NATIVE_PREFERENCES_OK ||
           msys_native_preferences_state_json(&candidate, response,
                                               sizeof(response)) == 0 ||
           msys_native_preferences_event_json(&candidate, reset, changed,
                                               sizeof(changed)) == 0) {
            (void)msys_mipc_send_error(&shell->ipc, id, "BAD_PREFERENCES",
                                       "preference update failed");
        }
        else {
            shell->preferences = candidate;
            apply_launcher_preferences(shell);
            (void)msys_mipc_send_return_json(&shell->ipc, id, response);
            (void)msys_mipc_send_event_json(&shell->ipc,
                "msys.shell.preferences.changed", changed);
        }
        free(payload);
        return;
    }
    if(strcmp(method, "show_recents") == 0 || strcmp(method, "show") == 0) {
        show_overview(shell);
        (void)msys_mipc_send_return_json(&shell->ipc, id, "{\"ok\":true}");
    }
    else if(strcmp(method, "hide_overlays") == 0 || strcmp(method, "hide") == 0) {
        hide_overview(shell);
        hide_notification_center(shell);
        hide_controls(shell);
        hide_toast(shell);
        (void)msys_mipc_send_return_json(&shell->ipc, id, "{\"ok\":true}");
    }
    else if(strcmp(method, "status") == 0) {
        (void)msys_mipc_send_return_json(
            &shell->ipc, id,
            shell->overview_visible != 0
                ? "{\"version\":\"0.6.23\",\"renderer\":\"lvgl-xml\",\"overview\":true}"
                : "{\"version\":\"0.6.23\",\"renderer\":\"lvgl-xml\",\"overview\":false}");
    }
    else
        (void)msys_mipc_send_error(&shell->ipc, id, "NO_METHOD", method);
    free(payload);
}

static void handle_event(shell_state *shell, const char *packet)
{
    char topic[160];
    char source[MSYS_NATIVE_NOTIFICATION_SOURCE_CAPACITY] = "";
    char *payload = NULL;
    if(msys_mipc_json_get_string(packet, "topic", topic, sizeof(topic), NULL) !=
       MSYS_MIPC_OK)
        return;
    if(strcmp(topic, "msys.window.transition") == 0) {
        char transition_id[97];
        char component[MSYS_NATIVE_COMPONENT_CAPACITY];
        char action[24];
        char phase[32];
        char reason[96] = "window-transition-failed";
        if(json_payload_copy(packet, &payload) == 0) return;
        if(msys_mipc_json_get_string(payload, "transition_id", transition_id,
               sizeof(transition_id), NULL) == MSYS_MIPC_OK &&
           msys_mipc_json_get_string(payload, "component", component,
               sizeof(component), NULL) == MSYS_MIPC_OK &&
           msys_mipc_json_get_string(payload, "action", action,
               sizeof(action), NULL) == MSYS_MIPC_OK &&
           msys_mipc_json_get_string(payload, "phase", phase,
               sizeof(phase), NULL) == MSYS_MIPC_OK &&
           msys_native_launch_transition_matches(shell->transition_id,
               shell->transition_component, transition_id, component, action)) {
            if(strcmp(phase, "surface-ready") == 0)
                complete_launch_transition(shell);
            else if(strcmp(phase, "failed") == 0) {
                (void)msys_mipc_json_get_string(payload, "reason", reason,
                                                 sizeof(reason), NULL);
                fail_launch_transition(shell, reason, 0);
            }
        }
        free(payload);
    }
    else if(strcmp(topic, "msys.role.notification-presenter") == 0 ||
       strcmp(topic, "msys.notification.post") == 0) {
        const msys_native_notification *newest;
        if(json_payload_copy(packet, &payload) == 0) return;
        (void)msys_mipc_json_get_string(packet, "source", source,
                                        sizeof(source), NULL);
        if(msys_native_notification_append(&shell->notifications, topic, payload,
                                           source, realtime_ms()) != 0) {
            if(shell->notification_visible != 0) render_notifications(shell);
            newest = msys_native_notification_newest(&shell->notifications, 0u);
            if(strcmp(topic, "msys.role.notification-presenter") == 0 &&
               shell->notification_visible == 0 && newest != NULL)
                show_toast(shell, newest->message[0] != '\0' ? newest->message :
                           (newest->title[0] != '\0' ? newest->title :
                            localized(shell, "新消息", "New notification")));
        }
        free(payload);
    }
    else if(strcmp(topic, "msys.install.package_changed") == 0 ||
       strcmp(topic, "msys.lifecycle.transition") == 0)
        request_apps(shell);
    else if(strcmp(topic, "msys.hal.changed") == 0)
        request_wifi_inventory(shell);
    else if(strcmp(topic, "msys.session.preferences.changed") == 0) {
        msys_native_preferences changed;
        char path[MSYS_NATIVE_PREFERENCES_PATH_CAPACITY];
        if(msys_native_preferences_load(&changed, path, sizeof(path)) ==
               MSYS_NATIVE_PREFERENCES_OK &&
           changed.revision >= shell->preferences.revision) {
            shell->preferences = changed;
            apply_launcher_preferences(shell);
        }
    }
    else if(strcmp(topic, "msys.timezone.changed") == 0) {
        tzset();
        shell->clock_text[0] = '\0';
        update_clock(shell);
    }
}

static void drain_ipc(shell_state *shell)
{
    unsigned int count;
    for(count = 0u; count < 16u; count++) {
        char type[32];
        int result = msys_mipc_recv_json(&shell->ipc, shell->packet,
                                         MSYS_MIPC_RECV_CAPACITY, 0, NULL);
        if(result == MSYS_MIPC_TIMEOUT) break;
        if(result != MSYS_MIPC_OK) {
            stopping = 1;
            break;
        }
        if(msys_mipc_json_get_string(shell->packet, "type", type, sizeof(type),
                                     NULL) != MSYS_MIPC_OK)
            continue;
        if(strcmp(type, "return") == 0 || strcmp(type, "error") == 0)
            handle_reply(shell, shell->packet, type);
        else if(strcmp(type, "call") == 0)
            handle_call(shell, shell->packet);
        else if(strcmp(type, "event") == 0)
            handle_event(shell, shell->packet);
    }
}

static void watch_documents(shell_state *shell)
{
    size_t index;
    int changed = 0;
    for(index = 0u; index < SURFACE_COUNT; index++) {
        int result = msys_ui_document_reload_if_changed(shell->views[index].document);
        if(result == MSYS_UI_DOCUMENT_OK) changed = 1;
    }
    if(changed != 0) {
        configure_interactions(shell);
        localize_documents(shell);
        render_launcher(shell);
        if(shell->overview_visible != 0) render_overview(shell);
        render_notifications(shell);
        wifi_update_labels(shell);
        update_clock(shell);
    }
}

static int event_loop(shell_state *shell)
{
    while(stopping == 0 &&
          (shell->run_until == 0u || monotonic_ms() < shell->run_until)) {
        struct pollfd descriptors[2];
        nfds_t count = 1u;
        uint32_t timeout;
        uint64_t current;
        int result;
        size_t index;
        if(msys_ui_runtime_pump(shell->runtime) <= 0) return -1;
        for(index = 0u; index < SURFACE_COUNT; index++) {
            if(msys_ui_surface_closed(shell->views[index].surface)) return 0;
        }
        current = monotonic_ms();
        if(shell->transition_visible != 0 &&
           msys_native_transition_expired(shell->transition_deadline, current))
            fail_launch_transition(shell, "shell-timeout", 1);
        update_clock(shell);
        if(shell->overview_visible != 0 && current >= shell->next_metrics_at) {
            render_metrics(shell);
            shell->next_metrics_at = current + METRICS_INTERVAL_MS;
        }
        if(shell->supervised != 0 && shell->next_wifi_refresh_at != 0u &&
           current >= shell->next_wifi_refresh_at)
            request_wifi_inventory(shell);
        if(shell->watch_ui != 0 && current >= shell->next_xml_watch_at) {
            watch_documents(shell);
            shell->next_xml_watch_at = current + XML_WATCH_INTERVAL_MS;
        }
        timeout = msys_ui_runtime_next_deadline_ms(shell->runtime);
        if(timeout == LV_NO_TIMER_READY || timeout > 250u) timeout = 250u;
        descriptors[0].fd = msys_ui_runtime_poll_fd(shell->runtime);
        descriptors[0].events = POLLIN;
        descriptors[0].revents = 0;
        if(shell->supervised != 0) {
            descriptors[1].fd = msys_mipc_client_fd(&shell->ipc);
            descriptors[1].events = POLLIN | POLLHUP;
            descriptors[1].revents = 0;
            count = 2u;
        }
        result = poll(descriptors, count, (int)timeout);
        if(result < 0 && errno != EINTR) return -1;
        if(count == 2u && (descriptors[1].revents & (POLLIN | POLLHUP)) != 0)
            drain_ipc(shell);
    }
    return 0;
}

static void shutdown_shell(shell_state *shell)
{
    size_t index;
    bitmaps_dispose(shell->app_icons, MSYS_NATIVE_MAX_APPS);
    bitmaps_dispose(shell->task_previews, MSYS_NATIVE_MAX_TASKS);
    bitmap_dispose(&shell->transition_icon);
    bitmap_dispose(&shell->wallpaper);
    for(index = 0u; index < SURFACE_COUNT; index++) {
        if(shell->views[index].document != NULL)
            msys_ui_document_destroy(shell->views[index].document);
        if(shell->views[index].theme != NULL)
            msys_ui_theme_destroy(shell->views[index].theme);
    }
    launcher_acrylic_dispose(shell);
    if(shell->supervised != 0) msys_mipc_client_close(&shell->ipc);
    msys_ui_dynamic_fonts_shutdown();
    if(shell->runtime != NULL) msys_ui_runtime_destroy(shell->runtime);
    free(shell->packet);
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--display :24] [--output spi|hdmi] [--ui-dir PATH] "
            "[--watch-ui] [--reduced-motion] [--probe-launcher ICON.ppm] "
            "[--run-ms N]\n",
            program);
}

int main(int argc, char **argv)
{
    shell_state shell;
    const char *display_name = NULL;
    msys_ui_output_t output = MSYS_UI_OUTPUT_SPI;
    bool reduced_motion = false;
    int exit_code;
    int index;
    const char *state_directory;
    enum msys_native_preferences_result preference_result;
    memset(&shell, 0, sizeof(shell));
    shell.launcher_folder = -1;
    shell.launcher_selected = -1;
    shell.launcher_drag_source = -1;
    shell.launcher_drag_target = -1;
    shell.launcher_drag_folder = -1;
    shell.launcher_drag_member = -1;
    shell.chinese = locale_is_chinese();
    preference_result = msys_native_preferences_load(
        &shell.preferences, shell.preferences_path,
        sizeof(shell.preferences_path));
    if(preference_result != MSYS_NATIVE_PREFERENCES_OK &&
       preference_result != MSYS_NATIVE_PREFERENCES_NO_STATE_DIR)
        fprintf(stderr, "msys-shell-lvgl: invalid preferences; using defaults\n");
    shell.buttons_mode = strcmp(shell.preferences.navigation_mode, "buttons") == 0;
    if(shell.preferences.revision == 0u && getenv("MSYS_NATIVE_NAV_MODE") != NULL) {
        shell.legacy_navigation_override = 1;
        shell.buttons_mode = strcmp(getenv("MSYS_NATIVE_NAV_MODE"), "buttons") == 0;
    }
    state_directory = getenv("MSYS_COMPONENT_STATE_DIR");
    if(state_directory == NULL || state_directory[0] == '\0')
        state_directory = getenv("MSYS_APP_STATE_DIR");
    if(msys_native_launcher_layout_load(&shell.launcher_layout,
                                        state_directory) == 0) {
        msys_native_launcher_layout_init(&shell.launcher_layout);
        if(state_directory != NULL && state_directory[0] == '/')
            (void)snprintf(shell.launcher_layout.path,
                sizeof(shell.launcher_layout.path), "%s/launcher-layout.v1",
                state_directory);
    }
    shell.packet = malloc(MSYS_MIPC_RECV_CAPACITY);
    shell.next_request_id = 100u;
    msys_native_system_metrics_init(&shell.metrics);
    msys_native_notification_history_init(&shell.notifications,
                                          MSYS_NATIVE_MAX_NOTIFICATIONS);
    if(shell.packet == NULL || resolve_ui_dir(shell.ui_dir, sizeof(shell.ui_dir)) == 0)
        return 1;
    for(index = 1; index < argc; index++) {
        if(strcmp(argv[index], "--describe") == 0) {
            puts("{\"frontend\":\"lvgl-xml\",\"version\":\"0.6.23\","
                 "\"surfaces\":[\"launcher\",\"system-chrome\","
                 "\"navigation-bar\",\"task-switcher\","
                 "\"notification-center\",\"quick-controls\","
                 "\"notification-presenter\",\"transition-presenter\"],"
                 "\"fallback\":\"msys-shell-native\"}");
            free(shell.packet);
            return 0;
        }
        if(strcmp(argv[index], "--display") == 0 && index + 1 < argc)
            display_name = argv[++index];
        else if(strcmp(argv[index], "--output") == 0 && index + 1 < argc)
            output = strcmp(argv[++index], "hdmi") == 0 ? MSYS_UI_OUTPUT_HDMI
                                                          : MSYS_UI_OUTPUT_SPI;
        else if(strcmp(argv[index], "--ui-dir") == 0 && index + 1 < argc)
            (void)snprintf(shell.ui_dir, sizeof(shell.ui_dir), "%s", argv[++index]);
        else if(strcmp(argv[index], "--watch-ui") == 0)
            shell.watch_ui = 1;
        else if(strcmp(argv[index], "--reduced-motion") == 0)
            reduced_motion = true;
        else if(strcmp(argv[index], "--probe-launcher") == 0 && index + 1 < argc) {
            const char *icon = argv[++index];
            size_t app;
            shell.app_count = 8u;
            shell.apps_loaded = 1;
            for(app = 0u; app < shell.app_count; app++) {
                (void)snprintf(shell.apps[app].component,
                    sizeof(shell.apps[app].component), "org.msys.probe:item-%zu",
                    app + 1u);
                (void)snprintf(shell.apps[app].name,
                    sizeof(shell.apps[app].name), "探针 %zu", app + 1u);
                (void)snprintf(shell.apps[app].icon_path,
                    sizeof(shell.apps[app].icon_path), "%s", icon);
            }
        }
        else if(strcmp(argv[index], "--run-ms") == 0 && index + 1 < argc)
            shell.run_until = monotonic_ms() + strtoull(argv[++index], NULL, 10);
        else {
            usage(argv[0]);
            free(shell.packet);
            return 2;
        }
    }
    active_shell = &shell;
    (void)signal(SIGINT, signal_handler);
    (void)signal(SIGTERM, signal_handler);
    if(initialize_ui(&shell, display_name, output, reduced_motion) == 0 ||
       initialize_ipc(&shell) == 0) {
        shutdown_shell(&shell);
        active_shell = NULL;
        return 1;
    }
    exit_code = event_loop(&shell) == 0 ? 0 : 1;
    shutdown_shell(&shell);
    active_shell = NULL;
    return exit_code;
}
