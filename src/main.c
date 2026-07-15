#define _POSIX_C_SOURCE 200809L

#include "msys/mipc.h"
#include "msys/i18n.h"
#include "msys_shell_native/catalog.h"
#include "msys_shell_native/image.h"
#include "msys_shell_native/model.h"
#include "msys_shell_native/preferences.h"
#include "shell_catalog.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define APP_VERSION "0.3.12"
#define NAV_FEEDBACK_MS 260u
#define NAV_INTERACTION_MAX_MS 4000u
#define TOAST_VISIBLE_MS 2600u
#define EXIT_TOAST_VISIBLE_MS 1100u
#define TOAST_ANIMATION_FRAME_MS 85u
#define TOAST_ANIMATION_FRAMES 3
#define OVERVIEW_ACCENT_MS 240u
#define TRANSITION_PULSE_MS 180u
#define LAUNCH_TRANSITION_FRAME_MS 90u
#define LAUNCH_TRANSITION_FRAMES 4
#define LAUNCH_TRANSITION_MAX_MS 8000u
#define TOAST_MAX_WIDTH 420
#define IMAGE_FILE_LIMIT (4u * 1024u * 1024u)
#define MAX_PENDING_CALLS 16u
#define RESPONSE_CAPACITY (16u * 1024u)
#define CONTROL_ROW_COUNT 4
#define AUDIO_CONTROL_ROW 2
#define AUDIO_STATE_TIMEOUT_MS 12000u
#define AUDIO_WRITE_TIMEOUT_MS 10000u
#define AUDIO_EDGE_ZONE_DIVISOR 5

enum catalog_state {
    CATALOG_LOADING = 0,
    CATALOG_READY,
    CATALOG_EMPTY,
    CATALOG_ERROR
};

enum pending_kind {
    PENDING_NONE = 0,
    PENDING_APPS_LIST,
    PENDING_APP_START,
    PENDING_RECENTS_LIST,
    PENDING_TASK_ACTIVATE,
    PENDING_TASK_CLOSE,
    PENDING_NAVIGATION,
    PENDING_NOTIFICATION_CENTER,
    PENDING_SETTINGS_PANEL,
    PENDING_AUDIO_STATE,
    PENDING_AUDIO_VOLUME,
    PENDING_AUDIO_MUTE
};

/*
 * Keep Xft optional at both link and run time.  The target deliberately ships
 * only the small Xft runtime library (not its development headers), so the
 * handful of ABI-stable types used here are declared locally and loaded with
 * dlsym.  When Xft or RENDER is unavailable, the existing Xlib FontSet path
 * remains fully functional.
 */
typedef struct msys_xft_draw msys_xft_draw;
typedef struct msys_xft_font msys_xft_font;

typedef struct msys_xrender_color {
    unsigned short red;
    unsigned short green;
    unsigned short blue;
    unsigned short alpha;
} msys_xrender_color;

typedef struct msys_xft_color {
    unsigned long pixel;
    msys_xrender_color color;
} msys_xft_color;

typedef struct msys_xft_glyph_info {
    unsigned short width;
    unsigned short height;
    short x;
    short y;
    short x_offset;
    short y_offset;
} msys_xft_glyph_info;

typedef msys_xft_draw *(*msys_xft_draw_create_fn)(
    Display *display,
    Drawable drawable,
    Visual *visual,
    Colormap colormap
);
typedef void (*msys_xft_draw_destroy_fn)(msys_xft_draw *draw);
typedef void (*msys_xft_draw_change_fn)(msys_xft_draw *draw, Drawable drawable);
typedef int (*msys_xft_draw_set_clip_fn)(msys_xft_draw *draw, Region region);
typedef int (*msys_xft_draw_set_clip_rectangles_fn)(
    msys_xft_draw *draw,
    int x_origin,
    int y_origin,
    const XRectangle *rectangles,
    int count
);
typedef msys_xft_font *(*msys_xft_font_open_name_fn)(
    Display *display,
    int screen,
    const char *name
);
typedef void (*msys_xft_font_close_fn)(Display *display, msys_xft_font *font);
typedef void (*msys_xft_draw_string_utf8_fn)(
    msys_xft_draw *draw,
    const msys_xft_color *color,
    const msys_xft_font *font,
    int x,
    int y,
    const unsigned char *text,
    int length
);
typedef void (*msys_xft_text_extents_utf8_fn)(
    Display *display,
    const msys_xft_font *font,
    const unsigned char *text,
    int length,
    msys_xft_glyph_info *extents
);
typedef int (*msys_xft_char_exists_fn)(
    Display *display,
    const msys_xft_font *font,
    unsigned int codepoint
);

typedef struct msys_xft_backend {
    void *library;
    msys_xft_draw *draw;
    msys_xft_font *font;
    msys_xft_draw_create_fn draw_create;
    msys_xft_draw_destroy_fn draw_destroy;
    msys_xft_draw_change_fn draw_change;
    msys_xft_draw_set_clip_fn draw_set_clip;
    msys_xft_draw_set_clip_rectangles_fn draw_set_clip_rectangles;
    msys_xft_font_open_name_fn font_open_name;
    msys_xft_font_close_fn font_close;
    msys_xft_draw_string_utf8_fn draw_string_utf8;
    msys_xft_text_extents_utf8_fn text_extents_utf8;
    msys_xft_char_exists_fn char_exists;
} msys_xft_backend;

typedef struct pending_call {
    uint64_t id;
    uint64_t deadline_ms;
    enum pending_kind kind;
    size_t index;
} pending_call;

typedef struct native_image_cache {
    XImage *image;
    char path[MSYS_NATIVE_PATH_CAPACITY];
    int box_width;
    int box_height;
    int draw_width;
    int draw_height;
} native_image_cache;

typedef struct native_controls_layout {
    int panel_x;
    int panel_width;
    int rows_y;
    int row_pitch;
    int row_height;
} native_controls_layout;

typedef struct native_shell {
    Display *display;
    int screen;
    Window root;
    Window launcher;
    Window chrome;
    Window navigation;
    Window recents;
    Window controls;
    Window launch_transition;
    Window toast;
    Atom wm_delete;
    GC gc;
    msys_xft_backend xft;
    XFontSet font_set;
    XFontStruct *fallback_font;
    unsigned long background;
    unsigned long surface;
    unsigned long surface_variant;
    unsigned long foreground;
    unsigned long muted;
    unsigned long accent;
    unsigned long accent_soft;
    unsigned long nav_background;
    unsigned long nav_pill;
    unsigned long success;
    msys_native_layout layout;
    msys_native_gesture gesture;
    msys_mipc_client ipc;
    int supervised;
    int running;
    int recents_visible;
    int recents_mapped;
    int controls_visible;
    int launch_transition_visible;
    int toast_visible;
    int buttons_mode;
    int navigation_vertical;
    int nav_feedback;
    enum msys_native_navigation_action nav_feedback_action;
    enum msys_native_navigation_action button_pressed_action;
    int chrome_pressed_action;
    int chrome_second_valid;
    int clip_active;
    XRectangle clip;
    uint64_t nav_feedback_until_ms;
    uint64_t nav_interaction_until_ms;
    uint64_t toast_until_ms;
    uint64_t toast_animation_at_ms;
    uint64_t overview_accent_until_ms;
    uint64_t launcher_pulse_until_ms;
    uint64_t recents_pulse_until_ms;
    uint64_t launch_transition_animation_at_ms;
    uint64_t launch_transition_until_ms;
    uint64_t next_request_id;
    time_t chrome_second;
    char toast_text[192];
    char launch_transition_component[MSYS_NATIVE_COMPONENT_CAPACITY];
    char last_display_recovery[256];
    char locale[MSYS_I18N_LOCALE_CAPACITY];
    msys_native_app apps[MSYS_NATIVE_MAX_APPS];
    size_t app_count;
    enum catalog_state apps_state;
    char apps_message[192];
    msys_native_task tasks[MSYS_NATIVE_MAX_TASKS];
    size_t task_count;
    enum catalog_state tasks_state;
    char tasks_message[192];
    int apps_refresh_queued;
    int tasks_refresh_queued;
    enum msys_native_shell_profile profile;
    int launcher_scroll;
    int launcher_drag_start_y;
    int launcher_drag_start_scroll;
    int launcher_dragging;
    int launcher_pointer_active;
    int launcher_pressed;
    int launcher_pulse;
    int recents_scroll;
    int recents_drag_start_x;
    int recents_drag_start_y;
    int recents_drag_start_scroll;
    int recents_dragging;
    int recents_pointer_active;
    int recents_horizontal_drag;
    int recents_drag_offset;
    int recents_pressed;
    int recents_pulse;
    int recents_close_pressed;
    int controls_pressed;
    int controls_pressed_zone;
    int audio_known;
    int audio_loading;
    int audio_available;
    int audio_muted;
    int audio_volume_percent;
    char audio_output_name[128];
    char audio_reason[64];
    int root_width;
    int root_height;
    int launcher_width;
    int launcher_height;
    int chrome_width;
    int chrome_height;
    int navigation_width;
    int navigation_height;
    int recents_width;
    int recents_height;
    int controls_width;
    int controls_height;
    int launch_transition_width;
    int launch_transition_height;
    size_t launch_transition_app_index;
    int launch_transition_frame;
    int toast_animation_frame;
    native_image_cache app_icons[MSYS_NATIVE_MAX_APPS];
    native_image_cache task_previews[MSYS_NATIVE_MAX_TASKS];
    pending_call pending[MAX_PENDING_CALLS];
    msys_native_preferences preferences;
    char preferences_path[MSYS_NATIVE_PREFERENCES_PATH_CAPACITY];
} native_shell;

static volatile sig_atomic_t stop_requested = 0;

static void request_apps(native_shell *shell);
static void request_recents(native_shell *shell);
static void activate_app(native_shell *shell, size_t index);
static void activate_task(native_shell *shell, size_t index);
static void close_task(native_shell *shell, size_t index);
static void draw_launcher(native_shell *shell);
static void draw_navigation_action_damage(
    native_shell *shell,
    enum msys_native_navigation_action action
);
static void redraw_recents_viewport(
    native_shell *shell,
    const XWindowAttributes *attributes,
    const msys_native_recents_layout *layout
);
static void redraw_controls_row(native_shell *shell, int index);
static void request_audio_state(native_shell *shell);
static const char *tr(native_shell *shell, const char *key);
static void hide_launch_transition(native_shell *shell);

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static uint64_t now_ms(void)
{
    uint64_t value = 0u;
    if (msys_mipc_monotonic_ms(&value) != MSYS_MIPC_OK) {
        return 0u;
    }
    return value;
}

static int animation_frame_limit(int normal_frames)
{
    const char *reduced = getenv("MSYS_NATIVE_REDUCED_MOTION");
    if (
        reduced != NULL &&
        (strcmp(reduced, "1") == 0 || strcmp(reduced, "true") == 0)
    ) {
        return 1;
    }
    return normal_frames;
}

static pending_call *pending_allocate(
    native_shell *shell,
    enum pending_kind kind,
    size_t index,
    uint64_t deadline_ms
)
{
    size_t position;
    for (position = 0u; position < MAX_PENDING_CALLS; position++) {
        if (shell->pending[position].kind == PENDING_NONE) {
            shell->pending[position].id = shell->next_request_id++;
            shell->pending[position].deadline_ms = deadline_ms;
            shell->pending[position].kind = kind;
            shell->pending[position].index = index;
            return &shell->pending[position];
        }
    }
    return NULL;
}

static pending_call *pending_find(native_shell *shell, uint64_t id)
{
    size_t position;
    for (position = 0u; position < MAX_PENDING_CALLS; position++) {
        if (
            shell->pending[position].kind != PENDING_NONE &&
            shell->pending[position].id == id
        ) {
            return &shell->pending[position];
        }
    }
    return NULL;
}

static int pending_has_kind(const native_shell *shell, enum pending_kind kind)
{
    size_t position;
    for (position = 0u; position < MAX_PENDING_CALLS; position++) {
        if (shell->pending[position].kind == kind) {
            return 1;
        }
    }
    return 0;
}

static uint64_t send_async(
    native_shell *shell,
    enum pending_kind kind,
    size_t index,
    const char *target,
    const char *method,
    const char *payload,
    uint64_t timeout_ms,
    int idempotent
)
{
    uint64_t current;
    pending_call *pending;
    int result;
    if (shell->supervised == 0) {
        return 0u;
    }
    current = now_ms();
    pending = pending_allocate(shell, kind, index, current + timeout_ms);
    if (pending == NULL) {
        return 0u;
    }
    result = msys_mipc_send_call_json(
        &shell->ipc,
        pending->id,
        target,
        method,
        payload,
        pending->deadline_ms,
        idempotent
    );
    if (result != MSYS_MIPC_OK) {
        memset(pending, 0, sizeof(*pending));
        return 0u;
    }
    return pending->id;
}

static int append_format(
    char *buffer,
    size_t capacity,
    size_t *used,
    const char *format,
    ...
)
{
    va_list arguments;
    int written;
    if (buffer == NULL || used == NULL || *used >= capacity) {
        return 0;
    }
    va_start(arguments, format);
    written = vsnprintf(buffer + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *used) {
        return 0;
    }
    *used += (size_t)written;
    return 1;
}

static int packet_payload(
    const char *packet,
    char *payload,
    size_t capacity
)
{
    const char *raw = NULL;
    size_t length = 0u;
    if (
        msys_mipc_json_get_raw(packet, "payload", &raw, &length) != MSYS_MIPC_OK ||
        length + 1u > capacity
    ) {
        return 0;
    }
    memcpy(payload, raw, length);
    payload[length] = '\0';
    return 1;
}

static void packet_error_text(
    const char *packet,
    char *output,
    size_t capacity
)
{
    char code[64] = "RPC_ERROR";
    char message[128] = "request failed";
    if (output == NULL || capacity == 0u) {
        return;
    }
    (void)msys_mipc_json_get_string(packet, "code", code, sizeof(code), NULL);
    (void)msys_mipc_json_get_string(packet, "message", message, sizeof(message), NULL);
    (void)snprintf(output, capacity, "%.48s: %.120s", code, message);
}

static unsigned long color(native_shell *shell, const char *name, unsigned long fallback)
{
    XColor exact;
    XColor screen_color;
    Colormap colormap = DefaultColormap(shell->display, shell->screen);
    if (
        XAllocNamedColor(shell->display, colormap, name, &screen_color, &exact) == 0
    ) {
        return fallback;
    }
    return screen_color.pixel;
}

static int xft_load_symbol(
    void *library,
    const char *name,
    void *destination,
    size_t destination_size
)
{
    void *symbol;
    if (
        library == NULL || name == NULL || destination == NULL ||
        destination_size != sizeof(symbol)
    ) {
        return 0;
    }
    symbol = dlsym(library, name);
    if (symbol == NULL) {
        return 0;
    }
    /* POSIX permits dlsym function pointers.  memcpy avoids a pedantic C cast. */
    memcpy(destination, &symbol, sizeof(symbol));
    return 1;
}

static void xft_dispose(Display *display, msys_xft_backend *backend)
{
    if (backend == NULL) {
        return;
    }
    if (display != NULL && backend->draw != NULL && backend->draw_destroy != NULL) {
        backend->draw_destroy(backend->draw);
    }
    if (display != NULL && backend->font != NULL && backend->font_close != NULL) {
        backend->font_close(display, backend->font);
    }
    /*
     * Xft may register Display-close hooks through Xlib.  This process closes
     * its Display immediately after this routine, so unloading libXft before
     * XCloseDisplay can leave a stale hook and crash during a clean shutdown.
     * Keeping this tiny optional library mapped until process exit is safe:
     * the kernel reclaims it with the process, while all server-side resources
     * are still released by XCloseDisplay below.
     */
    memset(backend, 0, sizeof(*backend));
}

static int xft_initialize(native_shell *shell)
{
    static const char *const fallback_patterns[] = {
        "Noto Sans CJK SC:style=Regular:lang=zh-cn:pixelsize=18:antialias=true:hinting=true:hintstyle=hintslight:autohint=false:embeddedbitmap=false:rgba=none",
        "Noto Sans CJK:style=Regular:lang=zh-cn:pixelsize=18:antialias=true:hinting=true:hintstyle=hintslight:autohint=false:embeddedbitmap=false:rgba=none",
        "Sans:lang=zh-cn:pixelsize=18:antialias=true:hinting=true:hintstyle=hintslight:autohint=false:embeddedbitmap=false:rgba=none"
    };
    static const char policy[] =
        ":style=Regular:lang=zh-cn:pixelsize=18:antialias=true"
        ":hinting=true:hintstyle=hintslight:autohint=false"
        ":embeddedbitmap=false:rgba=none";
    const char *font_patterns[4];
    char requested_pattern[256];
    const char *requested_family = getenv("MSYS_UI_FONT_FAMILY");
    size_t pattern_count = 0u;
    msys_xft_backend candidate;
    size_t index;
    if (requested_family != NULL && requested_family[0] != '\0') {
        const unsigned char *cursor = (const unsigned char *)requested_family;
        int safe = strlen(requested_family) <= 128u;
        while (safe != 0 && *cursor != '\0') {
            if (
                *cursor < 0x20u || *cursor == 0x7fu || *cursor == ':' ||
                *cursor == ',' || *cursor == '=' || *cursor == '\\'
            ) {
                safe = 0;
            }
            cursor++;
        }
        if (
            safe != 0 &&
            snprintf(
                requested_pattern,
                sizeof(requested_pattern),
                "%s%s",
                requested_family,
                policy
            ) > 0
        ) {
            font_patterns[pattern_count++] = requested_pattern;
        }
    }
    for (index = 0u; index < sizeof(fallback_patterns) / sizeof(fallback_patterns[0]); index++) {
        font_patterns[pattern_count++] = fallback_patterns[index];
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.library = dlopen("libXft.so.2", RTLD_LAZY | RTLD_LOCAL);
    if (candidate.library == NULL) {
        return 0;
    }
    if (
        xft_load_symbol(
            candidate.library,
            "XftDrawCreate",
            &candidate.draw_create,
            sizeof(candidate.draw_create)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftDrawDestroy",
            &candidate.draw_destroy,
            sizeof(candidate.draw_destroy)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftDrawChange",
            &candidate.draw_change,
            sizeof(candidate.draw_change)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftDrawSetClip",
            &candidate.draw_set_clip,
            sizeof(candidate.draw_set_clip)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftDrawSetClipRectangles",
            &candidate.draw_set_clip_rectangles,
            sizeof(candidate.draw_set_clip_rectangles)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftFontOpenName",
            &candidate.font_open_name,
            sizeof(candidate.font_open_name)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftFontClose",
            &candidate.font_close,
            sizeof(candidate.font_close)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftDrawStringUtf8",
            &candidate.draw_string_utf8,
            sizeof(candidate.draw_string_utf8)
        ) == 0 ||
        xft_load_symbol(
            candidate.library,
            "XftTextExtentsUtf8",
            &candidate.text_extents_utf8,
            sizeof(candidate.text_extents_utf8)
        ) == 0
    ) {
        xft_dispose(shell->display, &candidate);
        return 0;
    }
    /*
     * XftCharExists has been present throughout the Xft versions used by
     * MSYS targets.  Keep it optional nevertheless: drawing UTF-8 through
     * an older compatible libXft is preferable to rejecting that backend.
     */
    (void)xft_load_symbol(
        candidate.library,
        "XftCharExists",
        &candidate.char_exists,
        sizeof(candidate.char_exists)
    );
    for (index = 0u; index < pattern_count; index++) {
        candidate.font = candidate.font_open_name(
            shell->display,
            shell->screen,
            font_patterns[index]
        );
        if (
            candidate.font != NULL &&
            (
                candidate.char_exists == NULL ||
                (
                    candidate.char_exists(shell->display, candidate.font, 0x5E94u) != 0 &&
                    candidate.char_exists(shell->display, candidate.font, 0x7528u) != 0
                )
            )
        ) {
            break;
        }
        if (candidate.font != NULL) {
            candidate.font_close(shell->display, candidate.font);
            candidate.font = NULL;
        }
    }
    if (candidate.font == NULL) {
        xft_dispose(shell->display, &candidate);
        return 0;
    }
    candidate.draw = candidate.draw_create(
        shell->display,
        shell->root,
        DefaultVisual(shell->display, shell->screen),
        DefaultColormap(shell->display, shell->screen)
    );
    if (candidate.draw == NULL) {
        xft_dispose(shell->display, &candidate);
        return 0;
    }
    shell->xft = candidate;
    return 1;
}

static int xft_ready(const native_shell *shell)
{
    return shell->xft.draw != NULL && shell->xft.font != NULL &&
        shell->xft.draw_change != NULL && shell->xft.draw_string_utf8 != NULL &&
        shell->xft.draw_set_clip != NULL &&
        shell->xft.draw_set_clip_rectangles != NULL &&
        shell->xft.text_extents_utf8 != NULL;
}

static void xft_color_for_pixel(
    native_shell *shell,
    unsigned long pixel,
    msys_xft_color *result
)
{
    XColor source;
    memset(&source, 0, sizeof(source));
    memset(result, 0, sizeof(*result));
    source.pixel = pixel;
    (void)XQueryColor(
        shell->display,
        DefaultColormap(shell->display, shell->screen),
        &source
    );
    result->pixel = pixel;
    result->color.red = source.red;
    result->color.green = source.green;
    result->color.blue = source.blue;
    result->color.alpha = 65535u;
}

static int text_metrics(
    native_shell *shell,
    const char *text,
    int *width,
    int *glyph_y,
    int *glyph_height
)
{
    size_t length;
    if (shell == NULL || text == NULL || width == NULL || glyph_y == NULL ||
            glyph_height == NULL) {
        return 0;
    }
    length = strlen(text);
    if (length > (size_t)INT_MAX) {
        return 0;
    }
    if (xft_ready(shell) != 0) {
        msys_xft_glyph_info extents;
        memset(&extents, 0, sizeof(extents));
        shell->xft.text_extents_utf8(
            shell->display,
            shell->xft.font,
            (const unsigned char *)text,
            (int)length,
            &extents
        );
        *width = extents.x_offset > 0 ? extents.x_offset : (int)extents.width;
        /* Xft's y bearing is the positive distance above the baseline,
         * while Xlib's XRectangle uses a signed top coordinate.  Normalize
         * both backends to the latter so one centring formula is valid. */
        *glyph_y = -(int)extents.y;
        *glyph_height = (int)extents.height;
        return 1;
    }
    if (shell->font_set != NULL) {
        XRectangle ink;
        XRectangle logical;
        (void)XmbTextExtents(
            shell->font_set,
            text,
            (int)length,
            &ink,
            &logical
        );
        *width = (int)logical.width;
        *glyph_y = (int)logical.y;
        *glyph_height = (int)logical.height;
        return 1;
    }
    if (shell->fallback_font != NULL) {
        *width = XTextWidth(shell->fallback_font, text, (int)length);
        *glyph_y = -shell->fallback_font->ascent;
        *glyph_height = shell->fallback_font->ascent + shell->fallback_font->descent;
        return 1;
    }
    return 0;
}

static int compare_apps_name(const void *left, const void *right)
{
    const msys_native_app *a = (const msys_native_app *)left;
    const msys_native_app *b = (const msys_native_app *)right;
    int result = strcmp(a->name, b->name);
    return result != 0 ? result : strcmp(a->component, b->component);
}

static int compare_apps_component(const void *left, const void *right)
{
    const msys_native_app *a = (const msys_native_app *)left;
    const msys_native_app *b = (const msys_native_app *)right;
    return strcmp(a->component, b->component);
}

static void sort_apps(native_shell *shell)
{
    if (shell->app_count >= 2u) {
        qsort(
            shell->apps,
            shell->app_count,
            sizeof(shell->apps[0]),
            strcmp(shell->preferences.sort, "component") == 0
                ? compare_apps_component : compare_apps_name
        );
    }
}

static void set_foreground(native_shell *shell, unsigned long value)
{
    XSetForeground(shell->display, shell->gc, value);
}

static int rectangles_intersect(
    int left,
    int top,
    int width,
    int height,
    const XRectangle *clip
)
{
    int right;
    int bottom;
    int clip_right;
    int clip_bottom;
    if (width <= 0 || height <= 0 || clip == NULL) return 0;
    right = left + width;
    bottom = top + height;
    clip_right = (int)clip->x + (int)clip->width;
    clip_bottom = (int)clip->y + (int)clip->height;
    return left < clip_right && right > (int)clip->x &&
        top < clip_bottom && bottom > (int)clip->y;
}

static void begin_clip(
    native_shell *shell,
    int x,
    int y,
    int width,
    int height
)
{
    XRectangle rectangle;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    rectangle.x = (short)x;
    rectangle.y = (short)y;
    rectangle.width = (unsigned short)(width > 65535 ? 65535 : width);
    rectangle.height = (unsigned short)(height > 65535 ? 65535 : height);
    shell->clip = rectangle;
    shell->clip_active = 1;
    XSetClipRectangles(shell->display, shell->gc, 0, 0, &rectangle, 1, Unsorted);
}

static void end_clip(native_shell *shell)
{
    XSetClipMask(shell->display, shell->gc, None);
    if (xft_ready(shell) != 0) {
        (void)shell->xft.draw_set_clip(shell->xft.draw, NULL);
    }
    shell->clip_active = 0;
}

static void restore_clip(
    native_shell *shell,
    int previous_active,
    const XRectangle *previous
)
{
    if (previous_active != 0 && previous != NULL) {
        begin_clip(
            shell,
            (int)previous->x,
            (int)previous->y,
            (int)previous->width,
            (int)previous->height
        );
    } else {
        end_clip(shell);
    }
}

/* Add a child clip without losing an Expose/damage clip owned by the caller. */
static int begin_clip_intersection(
    native_shell *shell,
    int x,
    int y,
    int width,
    int height,
    int *previous_active,
    XRectangle *previous
)
{
    int left = x;
    int top = y;
    int right = x + width;
    int bottom = y + height;
    if (previous_active == NULL || previous == NULL || width < 1 || height < 1) {
        return 0;
    }
    *previous_active = shell->clip_active;
    *previous = shell->clip;
    if (shell->clip_active != 0) {
        int old_left = (int)shell->clip.x;
        int old_top = (int)shell->clip.y;
        int old_right = old_left + (int)shell->clip.width;
        int old_bottom = old_top + (int)shell->clip.height;
        if (left < old_left) left = old_left;
        if (top < old_top) top = old_top;
        if (right > old_right) right = old_right;
        if (bottom > old_bottom) bottom = old_bottom;
    }
    if (right <= left || bottom <= top) return 0;
    begin_clip(shell, left, top, right - left, bottom - top);
    return 1;
}

static void image_cache_dispose(native_image_cache *cache)
{
    if (cache == NULL) return;
    if (cache->image != NULL) XDestroyImage(cache->image);
    memset(cache, 0, sizeof(*cache));
}

static void image_caches_dispose(native_image_cache *caches, size_t capacity)
{
    size_t index;
    for (index = 0u; index < capacity; index++) image_cache_dispose(&caches[index]);
}

static XImage *image_cache_get(
    native_shell *shell,
    native_image_cache *cache,
    const char *path,
    int box_width,
    int box_height
)
{
    msys_native_ppm source;
    int draw_width;
    int draw_height;
    if (
        cache == NULL || path == NULL || path[0] != '/' || box_width < 1 || box_height < 1
    ) return NULL;
    if (
        strcmp(cache->path, path) == 0 && cache->box_width == box_width &&
        cache->box_height == box_height
    ) return cache->image;
    image_cache_dispose(cache);
    (void)snprintf(cache->path, sizeof(cache->path), "%s", path);
    cache->box_width = box_width;
    cache->box_height = box_height;
    memset(&source, 0, sizeof(source));
    if (!msys_native_ppm_load(path, IMAGE_FILE_LIMIT, &source)) return NULL;
    draw_width = box_width;
    draw_height = source.height * box_width / source.width;
    if (draw_height > box_height) {
        draw_height = box_height;
        draw_width = source.width * box_height / source.height;
    }
    if (draw_width < 1) draw_width = 1;
    if (draw_height < 1) draw_height = 1;
    cache->image = msys_native_ppm_ximage(
        shell->display,
        DefaultVisual(shell->display, shell->screen),
        DefaultDepth(shell->display, shell->screen),
        &source,
        draw_width,
        draw_height
    );
    cache->draw_width = draw_width;
    cache->draw_height = draw_height;
    msys_native_ppm_free(&source);
    return cache->image;
}

static int draw_cached_image(
    native_shell *shell,
    Drawable drawable,
    native_image_cache *cache,
    const char *path,
    int x,
    int y,
    int width,
    int height
)
{
    if (
        shell->clip_active != 0 &&
        !rectangles_intersect(x, y, width, height, &shell->clip)
    ) return 0;
    XImage *image = image_cache_get(shell, cache, path, width, height);
    int target_x;
    int target_y;
    if (image == NULL) return 0;
    target_x = x + (width - cache->draw_width) / 2;
    target_y = y + (height - cache->draw_height) / 2;
    XPutImage(
        shell->display,
        drawable,
        shell->gc,
        image,
        0,
        0,
        target_x,
        target_y,
        (unsigned int)cache->draw_width,
        (unsigned int)cache->draw_height
    );
    return 1;
}

static void fill_rounded(
    native_shell *shell,
    Drawable drawable,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    unsigned int radius,
    unsigned long value
)
{
    unsigned int diameter;
    if (width == 0u || height == 0u) {
        return;
    }
    radius = radius > width / 2u ? width / 2u : radius;
    radius = radius > height / 2u ? height / 2u : radius;
    diameter = radius * 2u;
    set_foreground(shell, value);
    if (radius == 0u) {
        XFillRectangle(shell->display, drawable, shell->gc, x, y, width, height);
        return;
    }
    XFillRectangle(
        shell->display,
        drawable,
        shell->gc,
        x + (int)radius,
        y,
        width - diameter,
        height
    );
    XFillRectangle(
        shell->display,
        drawable,
        shell->gc,
        x,
        y + (int)radius,
        width,
        height - diameter
    );
    XFillArc(shell->display, drawable, shell->gc, x, y, diameter, diameter, 0, 360 * 64);
    XFillArc(
        shell->display,
        drawable,
        shell->gc,
        x + (int)width - (int)diameter,
        y,
        diameter,
        diameter,
        0,
        360 * 64
    );
    XFillArc(
        shell->display,
        drawable,
        shell->gc,
        x,
        y + (int)height - (int)diameter,
        diameter,
        diameter,
        0,
        360 * 64
    );
    XFillArc(
        shell->display,
        drawable,
        shell->gc,
        x + (int)width - (int)diameter,
        y + (int)height - (int)diameter,
        diameter,
        diameter,
        0,
        360 * 64
    );
}

static void draw_text(
    native_shell *shell,
    Drawable drawable,
    int x,
    int baseline,
    const char *text,
    unsigned long value
)
{
    size_t length = strlen(text);
    if (shell->clip_active != 0) {
        int width = 0;
        int glyph_y = 0;
        int glyph_height = 1;
        (void)text_metrics(shell, text, &width, &glyph_y, &glyph_height);
        if (!rectangles_intersect(
            x, baseline + glyph_y, width > 0 ? width : 1, glyph_height,
            &shell->clip
        )) return;
    }
    if (length > (size_t)INT_MAX) {
        return;
    }
    if (xft_ready(shell) != 0) {
        msys_xft_color xft_color;
        xft_color_for_pixel(shell, value, &xft_color);
        shell->xft.draw_change(shell->xft.draw, drawable);
        if (shell->clip_active != 0) {
            (void)shell->xft.draw_set_clip_rectangles(
                shell->xft.draw,
                0,
                0,
                &shell->clip,
                1
            );
        }
        shell->xft.draw_string_utf8(
            shell->xft.draw,
            &xft_color,
            shell->xft.font,
            x,
            baseline,
            (const unsigned char *)text,
            (int)length
        );
        return;
    }
    set_foreground(shell, value);
    if (shell->font_set != NULL) {
        Xutf8DrawString(
            shell->display,
            drawable,
            shell->font_set,
            shell->gc,
            x,
            baseline,
            text,
            (int)length
        );
    } else if (shell->fallback_font != NULL) {
        XSetFont(shell->display, shell->gc, shell->fallback_font->fid);
        XDrawString(shell->display, drawable, shell->gc, x, baseline, text, (int)length);
    }
}

static void draw_text_centered(
    native_shell *shell,
    Drawable drawable,
    int center_x,
    int surface_height,
    const char *text,
    unsigned long value
)
{
    int width = 0;
    int glyph_y = 0;
    int glyph_height = 1;
    (void)text_metrics(shell, text, &width, &glyph_y, &glyph_height);
    draw_text(
        shell,
        drawable,
        center_x - width / 2,
        msys_native_center_baseline(surface_height, glyph_y, glyph_height),
        text,
        value
    );
}

static void draw_text_centered_in_rect(
    native_shell *shell,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height,
    const char *text,
    unsigned long value
)
{
    int text_width = 0;
    int glyph_y = 0;
    int glyph_height = 1;
    (void)text_metrics(shell, text, &text_width, &glyph_y, &glyph_height);
    draw_text(
        shell,
        drawable,
        x + (width - text_width) / 2,
        y + msys_native_center_baseline(height, glyph_y, glyph_height),
        text,
        value
    );
}

static void draw_text_ellipsized(
    native_shell *shell,
    Drawable drawable,
    int x,
    int baseline,
    int maximum_width,
    const char *text,
    unsigned long value
)
{
    char buffer[MSYS_NATIVE_SUMMARY_CAPACITY + 8u];
    size_t length;
    int width = 0;
    int glyph_y = 0;
    int glyph_height = 1;
    if (text == NULL || maximum_width < 1) return;
    (void)snprintf(buffer, sizeof(buffer), "%s", text);
    (void)text_metrics(shell, buffer, &width, &glyph_y, &glyph_height);
    if (width <= maximum_width) {
        draw_text(shell, drawable, x, baseline, buffer, value);
        return;
    }
    length = strlen(buffer);
    while (length > 0u) {
        do {
            length--;
        } while (length > 0u && ((unsigned char)buffer[length] & 0xC0u) == 0x80u);
        buffer[length] = '\0';
        if (length + 4u >= sizeof(buffer)) continue;
        (void)memcpy(buffer + length, "…", 4u);
        (void)text_metrics(shell, buffer, &width, &glyph_y, &glyph_height);
        if (width <= maximum_width) {
            draw_text(shell, drawable, x, baseline, buffer, value);
            return;
        }
        buffer[length] = '\0';
    }
}

static void draw_fallback_icon(
    native_shell *shell,
    Drawable drawable,
    int x,
    int y,
    int size,
    const char *name
)
{
    char glyph[5] = "?";
    size_t length = 1u;
    int glyph_width = 0;
    int glyph_y = 0;
    int glyph_height = 1;
    if (name != NULL && *name != '\0') {
        unsigned char first = (unsigned char)name[0];
        if ((first & 0x80u) == 0u) length = 1u;
        else if ((first & 0xE0u) == 0xC0u) length = 2u;
        else if ((first & 0xF0u) == 0xE0u) length = 3u;
        else if ((first & 0xF8u) == 0xF0u) length = 4u;
        if (strlen(name) < length) length = 1u;
        memcpy(glyph, name, length);
        glyph[length] = '\0';
    }
    fill_rounded(
        shell, drawable, x, y, (unsigned int)size, (unsigned int)size,
        (unsigned int)(size / 5), shell->accent
    );
    (void)text_metrics(shell, glyph, &glyph_width, &glyph_y, &glyph_height);
    draw_text(
        shell,
        drawable,
        x + (size - glyph_width) / 2,
        y + msys_native_center_baseline(size, glyph_y, glyph_height),
        glyph,
        shell->surface
    );
}

static const char *tr(native_shell *shell, const char *key)
{
    const char *message = msys_i18n_lookup(&shell_catalog, shell->locale, key);
    return message != NULL ? message : key;
}

static void set_window_identity(
    native_shell *shell,
    Window window,
    const char *title,
    const char *identity,
    const char *role,
    int dock
)
{
    XClassHint class_hint;
    Atom utf8 = XInternAtom(shell->display, "UTF8_STRING", False);
    Atom net_name = XInternAtom(shell->display, "_NET_WM_NAME", False);
    Atom app_id = XInternAtom(shell->display, "_MSYS_APP_ID", False);
    Atom window_role = XInternAtom(shell->display, "_MSYS_WINDOW_ROLE", False);
    XStoreName(shell->display, window, title);
    XChangeProperty(
        shell->display,
        window,
        net_name,
        utf8,
        8,
        PropModeReplace,
        (const unsigned char *)title,
        (int)strlen(title)
    );
    class_hint.res_name = (char *)"msys-shell-native";
    class_hint.res_class = (char *)identity;
    XSetClassHint(shell->display, window, &class_hint);
    XChangeProperty(
        shell->display,
        window,
        app_id,
        utf8,
        8,
        PropModeReplace,
        (const unsigned char *)identity,
        (int)strlen(identity)
    );
    if (role != NULL && *role != '\0') {
        XChangeProperty(
            shell->display,
            window,
            window_role,
            utf8,
            8,
            PropModeReplace,
            (const unsigned char *)role,
            (int)strlen(role)
        );
    }
    XSetWMProtocols(shell->display, window, &shell->wm_delete, 1);
    if (dock != 0) {
        Atom type = XInternAtom(shell->display, "_NET_WM_WINDOW_TYPE", False);
        Atom dock_type = XInternAtom(shell->display, "_NET_WM_WINDOW_TYPE_DOCK", False);
        Atom state = XInternAtom(shell->display, "_NET_WM_STATE", False);
        Atom above = XInternAtom(shell->display, "_NET_WM_STATE_ABOVE", False);
        XChangeProperty(
            shell->display,
            window,
            type,
            XA_ATOM,
            32,
            PropModeReplace,
            (unsigned char *)&dock_type,
            1
        );
        XChangeProperty(
            shell->display,
            window,
            state,
            XA_ATOM,
            32,
            PropModeReplace,
            (unsigned char *)&above,
            1
        );
    }
}

static Window create_window(
    native_shell *shell,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    unsigned long background,
    const char *title,
    const char *identity,
    const char *role,
    int dock
)
{
    Window window = XCreateSimpleWindow(
        shell->display,
        shell->root,
        x,
        y,
        width,
        height,
        0u,
        background,
        background
    );
    XSelectInput(
        shell->display,
        window,
        ExposureMask | StructureNotifyMask | ButtonPressMask |
        ButtonReleaseMask | PointerMotionMask
    );
    set_window_identity(shell, window, title, identity, role, dock);
    return window;
}

static void current_profile(native_shell *shell, int width, int height)
{
    shell->profile = msys_native_profile_resolve(
        shell->preferences.layout,
        getenv("MSYS_LAYOUT_PROFILE"),
        width,
        height
    );
}

static void draw_scroll_indicator(
    native_shell *shell,
    Drawable drawable,
    int x,
    int top,
    int height,
    int content_height,
    int viewport_height,
    int scroll
)
{
    int thumb_height;
    int maximum;
    int thumb_y;
    if (content_height <= viewport_height || height < 8) return;
    maximum = content_height - viewport_height;
    thumb_height = height * viewport_height / content_height;
    if (thumb_height < 18) thumb_height = 18;
    if (thumb_height > height) thumb_height = height;
    thumb_y = top + (height - thumb_height) * scroll / maximum;
    fill_rounded(shell, drawable, x, thumb_y, 4u, (unsigned int)thumb_height, 2u, shell->muted);
}

static void launcher_grid(native_shell *shell, int width, int height, msys_native_grid_layout *grid)
{
    current_profile(shell, shell->root_width, shell->root_height);
    msys_native_grid_compute(
        grid,
        shell->profile,
        width,
        height,
        shell->preferences.icon_size,
        shell->app_count
    );
    shell->launcher_scroll = msys_native_scroll_clamp(
        shell->launcher_scroll, grid->content_height, grid->viewport_height
    );
}

static void draw_app_cell(
    native_shell *shell,
    const msys_native_grid_layout *grid,
    size_t index
)
{
    int row = (int)index / grid->columns;
    int column = (int)index % grid->columns;
    int x = grid->margin + column * (grid->cell_width + grid->gap);
    int y = grid->top + row * (grid->cell_height + grid->gap) - shell->launcher_scroll;
    int icon_size = grid->icon_size;
    int icon_x;
    int icon_y = y + 7;
    int label_width = 0;
    int label_y = 0;
    int label_height = 1;
    int active = shell->launcher_pressed == (int)index ||
        shell->launcher_pulse == (int)index;
    unsigned long cell_color = active != 0
        ? shell->surface_variant : shell->surface;
    if (y + grid->cell_height < grid->top || y > grid->top + grid->viewport_height) {
        image_cache_dispose(&shell->app_icons[index]);
        return;
    }
    fill_rounded(
        shell,
        shell->launcher,
        x,
        y,
        (unsigned int)grid->cell_width,
        (unsigned int)grid->cell_height,
        16u,
        cell_color
    );
    if (active != 0) {
        set_foreground(shell, shell->accent);
        XSetLineAttributes(shell->display, shell->gc, 2u, LineSolid, CapRound, JoinRound);
        XDrawRectangle(
            shell->display,
            shell->launcher,
            shell->gc,
            x + 2,
            y + 2,
            (unsigned int)(grid->cell_width > 6 ? grid->cell_width - 6 : 1),
            (unsigned int)(grid->cell_height > 6 ? grid->cell_height - 6 : 1)
        );
        XSetLineAttributes(shell->display, shell->gc, 1u, LineSolid, CapButt, JoinMiter);
    }
    if (icon_size > grid->cell_width - 14) icon_size = grid->cell_width - 14;
    if (icon_size < 24) icon_size = 24;
    icon_x = x + (grid->cell_width - icon_size) / 2;
    if (!draw_cached_image(
        shell,
        shell->launcher,
        &shell->app_icons[index],
        shell->apps[index].icon_path,
        icon_x,
        icon_y,
        icon_size,
        icon_size
    )) {
        draw_fallback_icon(
            shell, shell->launcher, icon_x, icon_y, icon_size, shell->apps[index].name
        );
    }
    if (shell->preferences.show_labels != 0) {
        (void)text_metrics(
            shell, shell->apps[index].name, &label_width, &label_y, &label_height
        );
        if (label_width <= grid->cell_width - 14) {
            draw_text(
                shell, shell->launcher,
                x + (grid->cell_width - label_width) / 2,
                icon_y + icon_size + 24,
                shell->apps[index].name,
                shell->foreground
            );
        } else {
            draw_text_ellipsized(
                shell, shell->launcher, x + 7, icon_y + icon_size + 24,
                grid->cell_width - 14, shell->apps[index].name, shell->foreground
            );
        }
    }
}

static void draw_launcher(native_shell *shell)
{
    XWindowAttributes attributes;
    msys_native_grid_layout grid;
    size_t index;
    if (!XGetWindowAttributes(shell->display, shell->launcher, &attributes)) return;
    launcher_grid(shell, attributes.width, attributes.height, &grid);
    set_foreground(shell, shell->background);
    XFillRectangle(
        shell->display, shell->launcher, shell->gc, 0, 0,
        (unsigned int)attributes.width, (unsigned int)attributes.height
    );
    draw_text(shell, shell->launcher, grid.margin, 36, tr(shell, "launcher.title"), shell->foreground);
    draw_text_ellipsized(
        shell,
        shell->launcher,
        attributes.width > 126 ? attributes.width - 116 : grid.margin,
        36,
        104,
        tr(
            shell,
            shell->profile == MSYS_NATIVE_PROFILE_DESKTOP
                ? "profile.desktop"
                : (shell->profile == MSYS_NATIVE_PROFILE_EMBEDDED
                    ? "profile.embedded" : "profile.mobile")
        ),
        shell->muted
    );
    if (shell->apps_state == CATALOG_LOADING) {
        draw_text(shell, shell->launcher, grid.margin, 78, tr(shell, "launcher.loading"), shell->muted);
    } else if (shell->apps_state == CATALOG_ERROR) {
        draw_text(shell, shell->launcher, grid.margin, 78, tr(shell, "launcher.unavailable"), shell->foreground);
        draw_text_ellipsized(
            shell, shell->launcher, grid.margin, 106,
            attributes.width - grid.margin * 2, shell->apps_message, shell->muted
        );
    } else if (shell->app_count == 0u) {
        draw_text(shell, shell->launcher, grid.margin, 78, tr(shell, "launcher.empty"), shell->muted);
    } else {
        for (index = 0u; index < shell->app_count; index++) draw_app_cell(shell, &grid, index);
        draw_scroll_indicator(
            shell, shell->launcher, attributes.width - 8, grid.top,
            grid.viewport_height, grid.content_height, grid.viewport_height,
            shell->launcher_scroll
        );
    }
}

static void chrome_clock_bounds(int width, int *x, int *clock_width)
{
    int bounded_width = width > 0 ? width : 1;
    *clock_width = bounded_width < 150 ? bounded_width / 2 : 112;
    if (*clock_width < 72) *clock_width = 72;
    if (*clock_width > bounded_width) *clock_width = bounded_width;
    *x = (bounded_width - *clock_width) / 2;
}

static void draw_chrome(native_shell *shell)
{
    XWindowAttributes attributes;
    char clock_text[16];
    time_t current = time(NULL);
    struct tm local_time;
    int clock_x;
    int clock_width;
    int middle_y;
    if (!XGetWindowAttributes(shell->display, shell->chrome, &attributes)) return;
    chrome_clock_bounds(attributes.width, &clock_x, &clock_width);
    set_foreground(shell, shell->surface);
    XFillRectangle(
        shell->display, shell->chrome, shell->gc, 0, 0,
        (unsigned int)attributes.width, (unsigned int)attributes.height
    );
    if (shell->chrome_pressed_action == 1) {
        fill_rounded(
            shell, shell->chrome, 4, 3, 38u,
            (unsigned int)(attributes.height > 6 ? attributes.height - 6 : 1),
            12u, shell->surface_variant
        );
    } else if (shell->chrome_pressed_action == 2) {
        fill_rounded(
            shell, shell->chrome, attributes.width - 42, 3, 38u,
            (unsigned int)(attributes.height > 6 ? attributes.height - 6 : 1),
            12u, shell->surface_variant
        );
    }
    if (localtime_r(&current, &local_time) == NULL) {
        (void)snprintf(clock_text, sizeof(clock_text), "--:--:--");
        shell->chrome_second_valid = 0;
    } else {
        (void)strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &local_time);
        shell->chrome_second = current;
        shell->chrome_second_valid = 1;
    }
    draw_text_centered(
        shell, shell->chrome, attributes.width / 2, attributes.height,
        clock_text, shell->foreground
    );
    middle_y = attributes.height / 2;
    set_foreground(shell, shell->foreground);
    XDrawArc(shell->display, shell->chrome, shell->gc, 13, middle_y - 8, 13u, 13u, 0, 360 * 64);
    XFillArc(shell->display, shell->chrome, shell->gc, 18, middle_y + 5, 4u, 4u, 0, 360 * 64);
    XDrawLine(shell->display, shell->chrome, shell->gc, attributes.width - 29, middle_y - 7, attributes.width - 12, middle_y - 7);
    XDrawLine(shell->display, shell->chrome, shell->gc, attributes.width - 29, middle_y, attributes.width - 12, middle_y);
    XDrawLine(shell->display, shell->chrome, shell->gc, attributes.width - 29, middle_y + 7, attributes.width - 12, middle_y + 7);
    XFillArc(shell->display, shell->chrome, shell->gc, attributes.width - 24, middle_y - 10, 5u, 5u, 0, 360 * 64);
    XFillArc(shell->display, shell->chrome, shell->gc, attributes.width - 18, middle_y - 3, 5u, 5u, 0, 360 * 64);
    XFillArc(shell->display, shell->chrome, shell->gc, attributes.width - 27, middle_y + 4, 5u, 5u, 0, 360 * 64);
    (void)clock_x;
    (void)clock_width;
}

static void draw_chrome_clock_damage(native_shell *shell)
{
    XWindowAttributes attributes;
    int x;
    int width;
    if (!XGetWindowAttributes(shell->display, shell->chrome, &attributes)) return;
    chrome_clock_bounds(attributes.width, &x, &width);
    begin_clip(shell, x, 0, width, attributes.height);
    draw_chrome(shell);
    end_clip(shell);
}

static void draw_chrome_action_damage(native_shell *shell, int action)
{
    XWindowAttributes attributes;
    if (
        action < 1 || action > 2 ||
        !XGetWindowAttributes(shell->display, shell->chrome, &attributes)
    ) return;
    begin_clip(
        shell,
        action == 1 ? 0 : attributes.width - 48,
        0,
        48,
        attributes.height
    );
    draw_chrome(shell);
    end_clip(shell);
}

static int chrome_action_at(int x, int width)
{
    if (width <= 0 || x < 0 || x >= width) return 0;
    if (x < width / 3) return 1;
    return x >= width * 2 / 3 ? 2 : 0;
}

static void draw_navigation_symbol(
    native_shell *shell,
    Drawable drawable,
    enum msys_native_navigation_action action,
    int center_x,
    int center_y,
    int active
)
{
    unsigned long value = active > 1
        ? shell->accent : (active < 0 ? shell->accent : (active == 1 ? shell->success : shell->nav_pill));
    if (active != 0) {
        fill_rounded(shell, drawable, center_x - 22, center_y - 18, 44u, 36u, 18u, shell->surface_variant);
    }
    set_foreground(shell, value);
    XSetLineAttributes(shell->display, shell->gc, 3u, LineSolid, CapRound, JoinRound);
    if (action == MSYS_NATIVE_NAV_BACK) {
        XDrawLine(shell->display, drawable, shell->gc, center_x + 7, center_y - 9, center_x - 5, center_y);
        XDrawLine(shell->display, drawable, shell->gc, center_x - 5, center_y, center_x + 7, center_y + 9);
    } else if (action == MSYS_NATIVE_NAV_HOME) {
        XDrawArc(shell->display, drawable, shell->gc, center_x - 10, center_y - 10, 20u, 20u, 0, 360 * 64);
    } else {
        XDrawRectangle(shell->display, drawable, shell->gc, center_x - 9, center_y - 9, 18u, 18u);
    }
    XSetLineAttributes(shell->display, shell->gc, 1u, LineSolid, CapButt, JoinMiter);
}

static void draw_navigation(native_shell *shell)
{
    XWindowAttributes attributes;
    int inward = msys_native_gesture_inward(&shell->gesture);
    int offset = inward / 4;
    unsigned long pill_color = shell->nav_pill;
    int vertical;
    if (!XGetWindowAttributes(shell->display, shell->navigation, &attributes)) return;
    vertical = attributes.height > attributes.width * 2;
    shell->navigation_vertical = vertical;
    set_foreground(shell, shell->nav_background);
    XFillRectangle(
        shell->display, shell->navigation, shell->gc, 0, 0,
        (unsigned int)attributes.width, (unsigned int)attributes.height
    );
    if (shell->nav_feedback > 0) pill_color = shell->success;
    else if (shell->nav_feedback < 0) pill_color = shell->accent;
    else if (shell->gesture.active != 0 || inward >= 28) pill_color = shell->accent_soft;
    if (shell->buttons_mode != 0) {
        int dimension = vertical != 0 ? attributes.height : attributes.width;
        int slot;
        for (slot = 0; slot < 3; slot++) {
            enum msys_native_navigation_action action = slot == 0
                ? MSYS_NATIVE_NAV_BACK : (slot == 1 ? MSYS_NATIVE_NAV_HOME : MSYS_NATIVE_NAV_APPS);
            int along = msys_native_navigation_slot_center(dimension, slot);
            int active = shell->button_pressed_action == action
                ? 2
                : (shell->nav_feedback != 0 && shell->nav_feedback_action == action
                    ? shell->nav_feedback : 0);
            draw_navigation_symbol(
                shell,
                shell->navigation,
                action,
                vertical != 0 ? attributes.width / 2 : along,
                vertical != 0 ? along : attributes.height / 2,
                active
            );
        }
    } else if (vertical != 0) {
        int height = 48 + (inward > 28 ? 16 : inward / 2);
        int thickness = shell->gesture.active != 0 || shell->nav_feedback != 0 ? 8 : 5;
        int x = attributes.width / 2 - thickness / 2 - (offset > 8 ? 8 : offset);
        int y = (attributes.height - height) / 2;
        if (shell->gesture.active != 0) {
            fill_rounded(shell, shell->navigation, x - 3, y - 5, (unsigned int)(thickness + 6), (unsigned int)(height + 10), 7u, shell->muted);
        }
        fill_rounded(shell, shell->navigation, x, y, (unsigned int)thickness, (unsigned int)height, 4u, pill_color);
    } else {
        int width = 48 + (inward > 28 ? 16 : inward / 2);
        int thickness = shell->gesture.active != 0 || shell->nav_feedback != 0 ? 8 : 5;
        int x = (attributes.width - width) / 2;
        int y = attributes.height / 2 - thickness / 2 - (offset > 8 ? 8 : offset);
        if (shell->gesture.active != 0) {
            fill_rounded(shell, shell->navigation, x - 5, y - 3, (unsigned int)(width + 10), (unsigned int)(thickness + 6), 7u, shell->muted);
        }
        fill_rounded(shell, shell->navigation, x, y, (unsigned int)width, (unsigned int)thickness, 4u, pill_color);
    }
}

static void system_insets(native_shell *shell, int *top, int *right, int *bottom)
{
    XWindowAttributes chrome;
    XWindowAttributes navigation;
    *top = shell->layout.bar_height;
    *right = 0;
    *bottom = shell->layout.bar_height;
    if (XGetWindowAttributes(shell->display, shell->chrome, &chrome) && chrome.height > 0) {
        *top = chrome.height;
    }
    if (XGetWindowAttributes(shell->display, shell->navigation, &navigation)) {
        if (navigation.height > navigation.width * 2) {
            *right = navigation.width;
            *bottom = 0;
        } else {
            *bottom = navigation.height;
        }
    }
}

static int window_root_origin(native_shell *shell, Window window, int *x, int *y)
{
    Window ignored = None;
    return XTranslateCoordinates(
        shell->display, window, shell->root, 0, 0, x, y, &ignored
    ) != 0;
}

/*
 * Overview may be managed either as a full-root mobile surface or as an
 * already-inset desktop work area.  Derive only the portions of the system
 * bars which actually overlap this surface, avoiding both hidden header
 * controls and double insets when a replacement policy manager is used.
 */
static void recents_surface_insets(
    native_shell *shell,
    int width,
    int height,
    int *top,
    int *right,
    int *bottom
)
{
    XWindowAttributes chrome;
    XWindowAttributes navigation;
    int surface_x = 0;
    int surface_y = 0;
    int surface_right;
    int surface_bottom;
    int bar_x;
    int bar_y;
    *top = 0;
    *right = 0;
    *bottom = 0;
    if (
        width < 1 || height < 1 ||
        window_root_origin(shell, shell->recents, &surface_x, &surface_y) == 0
    ) return;
    surface_right = surface_x + width;
    surface_bottom = surface_y + height;
    if (
        XGetWindowAttributes(shell->display, shell->chrome, &chrome) != 0 &&
        window_root_origin(shell, shell->chrome, &bar_x, &bar_y) != 0 &&
        bar_y <= surface_y && bar_y + chrome.height > surface_y &&
        bar_x < surface_right && bar_x + chrome.width > surface_x
    ) {
        *top = bar_y + chrome.height - surface_y;
        if (*top > height) *top = height;
    }
    if (
        XGetWindowAttributes(shell->display, shell->navigation, &navigation) == 0 ||
        window_root_origin(shell, shell->navigation, &bar_x, &bar_y) == 0
    ) return;
    if (navigation.height > navigation.width * 2) {
        if (
            bar_x < surface_right && bar_x + navigation.width >= surface_right &&
            bar_y < surface_bottom && bar_y + navigation.height > surface_y
        ) {
            *right = surface_right - bar_x;
            if (*right > width) *right = width;
        }
    } else if (
        bar_y < surface_bottom && bar_y + navigation.height >= surface_bottom &&
        bar_x < surface_right && bar_x + navigation.width > surface_x
    ) {
        *bottom = surface_bottom - bar_y;
        if (*bottom > height - *top) *bottom = height - *top;
    }
}

static void recents_layout(
    native_shell *shell,
    int width,
    int height,
    msys_native_recents_layout *layout
)
{
    int top;
    int right;
    int bottom;
    current_profile(shell, width, height);
    recents_surface_insets(shell, width, height, &top, &right, &bottom);
    msys_native_recents_compute(
        layout, shell->profile, width, height,
        top, right, bottom, shell->task_count
    );
    shell->recents_scroll = msys_native_scroll_clamp(
        shell->recents_scroll, layout->content_height, layout->viewport_height
    );
}

static void recents_card_rect(
    const msys_native_recents_layout *layout,
    int scroll,
    size_t index,
    int *x,
    int *y
)
{
    int row = (int)index / layout->columns;
    int column = (int)index % layout->columns;
    *x = layout->margin + column * (layout->card_width + layout->gap);
    *y = layout->top + row * (layout->card_height + layout->gap) - scroll;
}

static void draw_task_card(
    native_shell *shell,
    const msys_native_recents_layout *layout,
    size_t index
)
{
    const char *title = msys_native_task_display_name(
        &shell->tasks[index], shell->apps, shell->app_count
    );
    int x;
    int y;
    int preview_x;
    int preview_y;
    int preview_width;
    int title_y;
    int active = shell->recents_pressed == (int)index ||
        shell->recents_pulse == (int)index;
    int drag = shell->recents_horizontal_drag != 0 && shell->recents_pressed == (int)index
        ? shell->recents_drag_offset : 0;
    recents_card_rect(layout, shell->recents_scroll, index, &x, &y);
    x += drag;
    if (y + layout->card_height < layout->top || y > layout->top + layout->viewport_height) {
        image_cache_dispose(&shell->task_previews[index]);
        return;
    }
    fill_rounded(
        shell, shell->recents, x, y,
        (unsigned int)layout->card_width, (unsigned int)layout->card_height,
        18u, active != 0 ? shell->surface_variant : shell->surface
    );
    if (active != 0) {
        set_foreground(shell, shell->accent);
        XSetLineAttributes(shell->display, shell->gc, 2u, LineSolid, CapRound, JoinRound);
        XDrawRectangle(
            shell->display,
            shell->recents,
            shell->gc,
            x + 2,
            y + 2,
            (unsigned int)(layout->card_width > 6 ? layout->card_width - 6 : 1),
            (unsigned int)(layout->card_height > 6 ? layout->card_height - 6 : 1)
        );
        XSetLineAttributes(shell->display, shell->gc, 1u, LineSolid, CapButt, JoinMiter);
    }
    preview_x = x + 8;
    preview_y = y + 8;
    preview_width = layout->card_width - 16;
    fill_rounded(
        shell, shell->recents, preview_x, preview_y,
        (unsigned int)preview_width, (unsigned int)layout->preview_height,
        13u, shell->nav_background
    );
    if (!draw_cached_image(
        shell,
        shell->recents,
        &shell->task_previews[index],
        shell->tasks[index].thumbnail,
        preview_x,
        preview_y,
        preview_width,
        layout->preview_height
    )) {
        int icon = layout->preview_height < 72 ? layout->preview_height - 12 : 64;
        if (icon < 24) icon = 24;
        draw_fallback_icon(
            shell,
            shell->recents,
            preview_x + (preview_width - icon) / 2,
            preview_y + (layout->preview_height - icon) / 2,
            icon,
            title
        );
    }
    title_y = y + layout->preview_height + 37;
    draw_text_ellipsized(
        shell, shell->recents, x + 14, title_y,
        layout->card_width - 58, title, shell->foreground
    );
    set_foreground(shell, shell->accent);
    XDrawLine(shell->display, shell->recents, shell->gc, x + layout->card_width - 31, title_y - 12, x + layout->card_width - 19, title_y);
    XDrawLine(shell->display, shell->recents, shell->gc, x + layout->card_width - 19, title_y - 12, x + layout->card_width - 31, title_y);
}

static void draw_recents(native_shell *shell)
{
    XWindowAttributes attributes;
    msys_native_recents_layout layout;
    size_t index;
    int top;
    int right;
    int bottom;
    int accent_width;
    int previous_clip_active;
    XRectangle previous_clip;
    if (!XGetWindowAttributes(shell->display, shell->recents, &attributes)) return;
    recents_layout(shell, attributes.width, attributes.height, &layout);
    recents_surface_insets(
        shell, attributes.width, attributes.height, &top, &right, &bottom
    );
    set_foreground(shell, shell->surface_variant);
    XFillRectangle(
        shell->display, shell->recents, shell->gc, 0, 0,
        (unsigned int)attributes.width, (unsigned int)attributes.height
    );
    draw_text(shell, shell->recents, layout.margin, top + 37, tr(shell, "recents.title"), shell->foreground);
    if (shell->recents_pressed == -2) {
        fill_rounded(
            shell,
            shell->recents,
            attributes.width - right - 72,
            top + 7,
            68u,
            36u,
            18u,
            shell->surface
        );
    }
    draw_text(shell, shell->recents, attributes.width - right - 58, top + 37, tr(shell, "recents.exit"), shell->accent);
    accent_width = (int)((shell->overview_accent_until_ms > now_ms()
        ? shell->overview_accent_until_ms - now_ms() : 0u) * 96u / OVERVIEW_ACCENT_MS);
    if (accent_width > 96) accent_width = 96;
    fill_rounded(shell, shell->recents, layout.margin, top + 45, (unsigned int)(96 - accent_width), 4u, 2u, shell->accent);
    if (shell->tasks_state == CATALOG_LOADING) {
        draw_text(shell, shell->recents, layout.margin, layout.top + 24, tr(shell, "recents.loading"), shell->muted);
        return;
    }
    if (shell->tasks_state == CATALOG_ERROR) {
        draw_text(shell, shell->recents, layout.margin, layout.top + 24, tr(shell, "recents.unavailable"), shell->foreground);
        draw_text_ellipsized(
            shell, shell->recents, layout.margin, layout.top + 52,
            attributes.width - right - layout.margin * 2, shell->tasks_message, shell->muted
        );
        return;
    }
    if (shell->task_count == 0u) {
        draw_text(shell, shell->recents, layout.margin, layout.top + 24, tr(shell, "recents.empty"), shell->muted);
        return;
    }
    if (begin_clip_intersection(
        shell,
        0,
        layout.top,
        attributes.width,
        layout.viewport_height,
        &previous_clip_active,
        &previous_clip
    )) {
        for (index = 0u; index < shell->task_count; index++) {
            draw_task_card(shell, &layout, index);
        }
        draw_scroll_indicator(
            shell,
            shell->recents,
            attributes.width - right - 8,
            layout.top,
            layout.viewport_height,
            layout.content_height,
            layout.viewport_height,
            shell->recents_scroll
        );
        restore_clip(shell, previous_clip_active, &previous_clip);
    }
    (void)bottom;
}

static int controls_layout_compute(
    native_shell *shell,
    int width,
    int height,
    native_controls_layout *layout
)
{
    int top;
    int right;
    int bottom;
    int rows_bottom;
    int available;
    if (layout == NULL) return 0;
    system_insets(shell, &top, &right, &bottom);
    layout->panel_width = shell->profile == MSYS_NATIVE_PROFILE_DESKTOP
        ? (width - right) * 2 / 5 : width - right;
    if (layout->panel_width < 220) layout->panel_width = width - right;
    layout->panel_x = width - right - layout->panel_width;
    layout->rows_y = top + 58;
    rows_bottom = height - bottom - 30;
    available = rows_bottom - layout->rows_y;
    if (available < CONTROL_ROW_COUNT * 32) return 0;
    layout->row_pitch = available / CONTROL_ROW_COUNT;
    if (layout->row_pitch > 66) layout->row_pitch = 66;
    layout->row_height = layout->row_pitch - (layout->row_pitch >= 60 ? 10 : 6);
    return layout->row_height >= 26;
}

static const char *audio_reason_text(native_shell *shell)
{
    if (strcmp(shell->audio_reason, "controller-not-registered") == 0) {
        return tr(shell, "controls.audio_no_controller");
    }
    if (strcmp(shell->audio_reason, "no-connected-a2dp-output") == 0) {
        return tr(shell, "controls.audio_no_output");
    }
    if (strcmp(shell->audio_reason, "audio-stack-unavailable") == 0) {
        return tr(shell, "controls.audio_stack_unavailable");
    }
    return tr(shell, "controls.audio_unavailable");
}

static void audio_status_text(native_shell *shell, char *output, size_t capacity)
{
    if (output == NULL || capacity == 0u) return;
    if (shell->audio_loading != 0 && shell->audio_known == 0) {
        (void)snprintf(output, capacity, "%s", tr(shell, "controls.audio_loading"));
    } else if (shell->audio_available == 0) {
        (void)snprintf(output, capacity, "%s", audio_reason_text(shell));
    } else {
        (void)snprintf(
            output,
            capacity,
            "%d%% %s",
            shell->audio_volume_percent,
            tr(shell, shell->audio_muted != 0 ? "controls.audio_muted" : "controls.audio_on")
        );
    }
}

static void draw_control_icon(
    native_shell *shell,
    Drawable drawable,
    int x,
    int y,
    int index
)
{
    set_foreground(shell, shell->nav_pill);
    XSetLineAttributes(shell->display, shell->gc, 2u, LineSolid, CapRound, JoinRound);
    if (index == 0) {
        /* Wi-Fi: two open radio waves and a compact endpoint. */
        XDrawArc(shell->display, drawable, shell->gc, x + 7, y + 7, 22u, 22u, 0, 180 * 64);
        XDrawArc(shell->display, drawable, shell->gc, x + 11, y + 14, 14u, 14u, 0, 180 * 64);
        XFillArc(shell->display, drawable, shell->gc, x + 16, y + 25, 4u, 4u, 0, 360 * 64);
    } else if (index == 1) {
        /* Bluetooth rune, kept open so it remains legible on the SPI panel. */
        XDrawLine(shell->display, drawable, shell->gc, x + 18, y + 6, x + 18, y + 30);
        XDrawLine(shell->display, drawable, shell->gc, x + 18, y + 6, x + 25, y + 13);
        XDrawLine(shell->display, drawable, shell->gc, x + 25, y + 13, x + 12, y + 24);
        XDrawLine(shell->display, drawable, shell->gc, x + 12, y + 12, x + 25, y + 24);
        XDrawLine(shell->display, drawable, shell->gc, x + 25, y + 24, x + 18, y + 30);
    } else if (index == 2) {
        /* Audio: a compact speaker and two open sound waves. */
        XDrawLine(shell->display, drawable, shell->gc, x + 8, y + 15, x + 14, y + 15);
        XDrawLine(shell->display, drawable, shell->gc, x + 14, y + 15, x + 20, y + 9);
        XDrawLine(shell->display, drawable, shell->gc, x + 20, y + 9, x + 20, y + 27);
        XDrawLine(shell->display, drawable, shell->gc, x + 20, y + 27, x + 14, y + 21);
        XDrawLine(shell->display, drawable, shell->gc, x + 14, y + 21, x + 8, y + 21);
        XDrawArc(shell->display, drawable, shell->gc, x + 19, y + 12, 9u, 12u, -90 * 64, 180 * 64);
        XDrawArc(shell->display, drawable, shell->gc, x + 18, y + 8, 16u, 20u, -90 * 64, 180 * 64);
    } else {
        /* Settings: three bounded sliders rather than a dense small gear. */
        XDrawLine(shell->display, drawable, shell->gc, x + 8, y + 10, x + 28, y + 10);
        XDrawLine(shell->display, drawable, shell->gc, x + 8, y + 18, x + 28, y + 18);
        XDrawLine(shell->display, drawable, shell->gc, x + 8, y + 26, x + 28, y + 26);
        XFillArc(shell->display, drawable, shell->gc, x + 11, y + 7, 6u, 6u, 0, 360 * 64);
        XFillArc(shell->display, drawable, shell->gc, x + 21, y + 15, 6u, 6u, 0, 360 * 64);
        XFillArc(shell->display, drawable, shell->gc, x + 15, y + 23, 6u, 6u, 0, 360 * 64);
    }
    XSetLineAttributes(shell->display, shell->gc, 1u, LineSolid, CapButt, JoinMiter);
}

static void draw_controls(native_shell *shell)
{
    XWindowAttributes attributes;
    native_controls_layout layout;
    int top;
    int right;
    int bottom;
    const char *keys[] = {
        "controls.wifi",
        "controls.bluetooth",
        "controls.audio",
        "controls.settings"
    };
    int index;
    if (!XGetWindowAttributes(shell->display, shell->controls, &attributes)) return;
    current_profile(shell, attributes.width, attributes.height);
    system_insets(shell, &top, &right, &bottom);
    if (!controls_layout_compute(
        shell, attributes.width, attributes.height, &layout
    )) return;
    set_foreground(shell, shell->surface_variant);
    XFillRectangle(
        shell->display, shell->controls, shell->gc, 0, 0,
        (unsigned int)attributes.width, (unsigned int)attributes.height
    );
    set_foreground(shell, shell->surface);
    XFillRectangle(
        shell->display, shell->controls, shell->gc, layout.panel_x, top,
        (unsigned int)layout.panel_width,
        (unsigned int)(attributes.height - top - bottom)
    );
    draw_text(shell, shell->controls, layout.panel_x + 18, top + 38, tr(shell, "controls.title"), shell->foreground);
    for (index = 0; index < CONTROL_ROW_COUNT; index++) {
        int row_x = layout.panel_x + 14;
        int row_y = layout.rows_y + index * layout.row_pitch;
        int row_width = layout.panel_width - 28;
        int icon_y = row_y + (layout.row_height - 36) / 2;
        unsigned long value = shell->controls_pressed == index && index != AUDIO_CONTROL_ROW
            ? shell->surface_variant : shell->background;
        fill_rounded(
            shell, shell->controls, row_x, row_y,
            (unsigned int)row_width, (unsigned int)layout.row_height, 16u, value
        );
        if (index == AUDIO_CONTROL_ROW) {
            char status[96];
            int edge_width = row_width / AUDIO_EDGE_ZONE_DIVISOR;
            int middle_x = row_x + edge_width;
            int middle_width = row_width - edge_width * 2;
            int name_baseline = row_y + (layout.row_height >= 50 ? 20 : 16);
            int status_baseline = row_y + layout.row_height - 8;
            if (shell->controls_pressed == index && shell->controls_pressed_zone >= 0) {
                int pressed_x = shell->controls_pressed_zone == 0
                    ? row_x : (shell->controls_pressed_zone == 1
                        ? middle_x : middle_x + middle_width);
                int pressed_width = shell->controls_pressed_zone == 1
                    ? middle_width : (shell->controls_pressed_zone == 0
                        ? edge_width : row_width - edge_width - middle_width);
                fill_rounded(
                    shell, shell->controls,
                    pressed_x + 3, row_y + 3,
                    (unsigned int)(pressed_width - 6),
                    (unsigned int)(layout.row_height - 6),
                    12u,
                    shell->surface_variant
                );
            }
            audio_status_text(shell, status, sizeof(status));
            draw_text_centered_in_rect(
                shell, shell->controls, row_x, row_y, edge_width,
                layout.row_height, "-10", shell->foreground
            );
            draw_text_ellipsized(
                shell, shell->controls, middle_x + 5, name_baseline,
                middle_width - 10,
                shell->audio_available != 0 && shell->audio_output_name[0] != '\0'
                    ? shell->audio_output_name : tr(shell, keys[index]),
                shell->foreground
            );
            draw_text_ellipsized(
                shell, shell->controls, middle_x + 5, status_baseline,
                middle_width - 10, status,
                shell->audio_available != 0 ? shell->accent : shell->muted
            );
            draw_text_centered_in_rect(
                shell, shell->controls,
                middle_x + middle_width, row_y,
                row_width - edge_width - middle_width, layout.row_height,
                "+10", shell->foreground
            );
            continue;
        }
        fill_rounded(
            shell, shell->controls, layout.panel_x + 24, icon_y,
            36u, 36u, 12u, shell->accent
        );
        draw_control_icon(
            shell, shell->controls, layout.panel_x + 24, icon_y, index
        );
        draw_text(
            shell, shell->controls, layout.panel_x + 72,
            row_y + layout.row_height / 2 + 6,
            tr(shell, keys[index]), shell->foreground
        );
    }
    draw_text(
        shell, shell->controls, layout.panel_x + 18, attributes.height - bottom - 12,
        tr(shell, "controls.open_settings"), shell->muted
    );
    (void)right;
}

static void draw_toast(native_shell *shell)
{
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(shell->display, shell->toast, &attributes)) return;
    set_foreground(shell, shell->nav_background);
    XFillRectangle(
        shell->display,
        shell->toast,
        shell->gc,
        0,
        0,
        (unsigned int)attributes.width,
        (unsigned int)attributes.height
    );
    if (shell->toast_animation_frame < animation_frame_limit(TOAST_ANIMATION_FRAMES)) {
        int frames = animation_frame_limit(TOAST_ANIMATION_FRAMES);
        int width = attributes.width * (shell->toast_animation_frame + 1) / frames;
        set_foreground(shell, shell->accent);
        XFillRectangle(
            shell->display, shell->toast, shell->gc, 0, 0,
            (unsigned int)(width > 0 ? width : 1), 4u
        );
    }
    draw_text(shell, shell->toast, 16, 30, "MSYS", shell->nav_pill);
    draw_text_ellipsized(
        shell,
        shell->toast,
        16,
        58,
        attributes.width > 32 ? attributes.width - 32 : 1,
        shell->toast_text,
        shell->nav_pill
    );
}

static void draw_toast_animation_damage(native_shell *shell)
{
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(shell->display, shell->toast, &attributes)) return;
    begin_clip(shell, 0, 0, attributes.width, 6);
    draw_toast(shell);
    end_clip(shell);
}

static void launch_transition_geometry(
    native_shell *shell,
    int *x,
    int *y,
    int *width,
    int *height
)
{
    *width = shell->root_width - 24;
    if (*width > 236) *width = 236;
    if (*width < 1) *width = 1;
    *height = shell->root_height - 24;
    if (*height > 132) *height = 132;
    if (*height < 1) *height = 1;
    *x = (shell->root_width - *width) / 2;
    *y = (shell->root_height - *height) / 2;
}

static void layout_launch_transition(native_shell *shell)
{
    XWindowAttributes attributes;
    int x;
    int y;
    int width;
    int height;
    if (shell->launch_transition == None) return;
    launch_transition_geometry(shell, &x, &y, &width, &height);
    if (
        XGetWindowAttributes(
            shell->display, shell->launch_transition, &attributes
        ) && attributes.x == x && attributes.y == y &&
        attributes.width == width && attributes.height == height
    ) return;
    XMoveResizeWindow(
        shell->display,
        shell->launch_transition,
        x,
        y,
        (unsigned int)width,
        (unsigned int)height
    );
}

static void draw_launch_transition(native_shell *shell)
{
    XWindowAttributes attributes;
    size_t index = shell->launch_transition_app_index;
    int icon = 64;
    int icon_x;
    int icon_y = 12;
    int frame_limit = animation_frame_limit(LAUNCH_TRANSITION_FRAMES);
    int pulse;
    const char *name;
    if (
        index >= shell->app_count ||
        !XGetWindowAttributes(
            shell->display, shell->launch_transition, &attributes
        )
    ) return;
    name = shell->apps[index].name;
    set_foreground(
        shell,
        shell->launch_transition_frame == 0 ? shell->surface_variant : shell->surface
    );
    XFillRectangle(
        shell->display, shell->launch_transition, shell->gc, 0, 0,
        (unsigned int)attributes.width, (unsigned int)attributes.height
    );
    if (icon > attributes.height - 54) icon = attributes.height - 54;
    if (icon > attributes.width - 30) icon = attributes.width - 30;
    if (icon < 24) icon = 24;
    icon_x = (attributes.width - icon) / 2;
    if (!draw_cached_image(
        shell,
        shell->launch_transition,
        &shell->app_icons[index],
        shell->apps[index].icon_path,
        icon_x,
        icon_y,
        icon,
        icon
    )) {
        draw_fallback_icon(
            shell, shell->launch_transition, icon_x, icon_y, icon, name
        );
    }
    pulse = frame_limit > 1
        ? shell->launch_transition_frame * 5 / (frame_limit - 1) : 0;
    set_foreground(shell, shell->accent);
    XSetLineAttributes(shell->display, shell->gc, 2u, LineSolid, CapRound, JoinRound);
    XDrawRectangle(
        shell->display,
        shell->launch_transition,
        shell->gc,
        icon_x - 4 - pulse,
        icon_y - 4 - pulse,
        (unsigned int)(icon + 8 + pulse * 2),
        (unsigned int)(icon + 8 + pulse * 2)
    );
    XSetLineAttributes(shell->display, shell->gc, 1u, LineSolid, CapButt, JoinMiter);
    draw_text_ellipsized(
        shell,
        shell->launch_transition,
        14,
        attributes.height - 28,
        attributes.width - 28,
        name,
        shell->foreground
    );
    draw_text_ellipsized(
        shell,
        shell->launch_transition,
        14,
        attributes.height - 9,
        attributes.width - 28,
        tr(shell, "launcher.starting"),
        shell->muted
    );
}

static void redraw(native_shell *shell, Window window)
{
    if (window == shell->launcher) {
        draw_launcher(shell);
    } else if (window == shell->chrome) {
        draw_chrome(shell);
    } else if (window == shell->navigation) {
        draw_navigation(shell);
    } else if (window == shell->recents) {
        draw_recents(shell);
    } else if (window == shell->controls) {
        draw_controls(shell);
    } else if (window == shell->launch_transition) {
        draw_launch_transition(shell);
    } else if (window == shell->toast) {
        draw_toast(shell);
    }
}

static int initialize_x11(native_shell *shell)
{
    char **missing = NULL;
    int missing_count = 0;
    char *default_string = NULL;
    int width;
    int height;
    int transition_x;
    int transition_y;
    int transition_width;
    int transition_height;
    (void)setlocale(LC_ALL, "");
    (void)XSetLocaleModifiers("");
    shell->display = XOpenDisplay(NULL);
    if (shell->display == NULL) {
        fprintf(stderr, "msys-shell-native: cannot open DISPLAY\n");
        return 0;
    }
    shell->screen = DefaultScreen(shell->display);
    shell->root = RootWindow(shell->display, shell->screen);
    width = DisplayWidth(shell->display, shell->screen);
    height = DisplayHeight(shell->display, shell->screen);
    shell->root_width = width;
    shell->root_height = height;
    current_profile(shell, width, height);
    msys_native_layout_compute(&shell->layout, width, height);
    XSelectInput(shell->display, shell->root, StructureNotifyMask);
    shell->gc = XCreateGC(shell->display, shell->root, 0u, NULL);
    if (shell->gc == NULL) {
        return 0;
    }
    if (xft_initialize(shell) == 0) {
        shell->font_set = XCreateFontSet(
            shell->display,
            "-*-sans-medium-r-normal--14-*-*-*-*-*-*-*,-misc-fixed-medium-r-normal--14-*-*-*-*-*-*-*",
            &missing,
            &missing_count,
            &default_string
        );
        if (missing != NULL) {
            XFreeStringList(missing);
        }
        if (shell->font_set == NULL) {
            shell->fallback_font = XLoadQueryFont(shell->display, "fixed");
        }
    }
    shell->background = color(
        shell,
        shell->preferences.wallpaper_color,
        WhitePixel(shell->display, shell->screen)
    );
    shell->surface = color(shell, "#FFFBFE", shell->background);
    shell->surface_variant = color(shell, "#E7E0EC", shell->background);
    shell->foreground = color(shell, "#1D1B20", BlackPixel(shell->display, shell->screen));
    shell->muted = color(shell, "#625B71", shell->foreground);
    shell->accent = color(shell, shell->preferences.accent_color, shell->foreground);
    shell->accent_soft = shell->accent;
    shell->nav_background = color(shell, "#111318", BlackPixel(shell->display, shell->screen));
    shell->nav_pill = color(shell, "#E7EBF0", WhitePixel(shell->display, shell->screen));
    shell->success = color(shell, "#81C784", shell->nav_pill);
    shell->wm_delete = XInternAtom(shell->display, "WM_DELETE_WINDOW", False);

    shell->launcher = create_window(
        shell,
        0,
        shell->layout.content_y,
        (unsigned int)shell->layout.width,
        (unsigned int)shell->layout.content_height,
        shell->background,
        "MSYS Launcher",
        "org.msys.shell.native.launcher",
        "launcher",
        0
    );
    shell->chrome = create_window(
        shell,
        0,
        0,
        (unsigned int)shell->layout.width,
        (unsigned int)shell->layout.bar_height,
        shell->surface,
        "MSYS Chrome",
        "org.msys.shell.native.chrome",
        "system-chrome",
        1
    );
    shell->navigation = create_window(
        shell,
        0,
        shell->layout.height - shell->layout.bar_height,
        (unsigned int)shell->layout.width,
        (unsigned int)shell->layout.bar_height,
        shell->nav_background,
        "MSYS Navigation",
        "org.msys.shell.native.navigation-pill",
        "navigation-bar",
        1
    );
    shell->recents = create_window(
        shell,
        0,
        0,
        (unsigned int)shell->layout.width,
        (unsigned int)shell->layout.height,
        shell->surface_variant,
        "MSYS Recents",
        "org.msys.shell.task-switcher",
        "task-switcher",
        1
    );
    shell->controls = create_window(
        shell,
        0,
        0,
        (unsigned int)shell->layout.width,
        (unsigned int)shell->layout.height,
        shell->surface_variant,
        "MSYS Quick Controls Surface",
        "org.msys.shell.native.quick-controls-surface",
        NULL,
        0
    );
    launch_transition_geometry(
        shell,
        &transition_x,
        &transition_y,
        &transition_width,
        &transition_height
    );
    shell->launch_transition = create_window(
        shell,
        transition_x,
        transition_y,
        (unsigned int)transition_width,
        (unsigned int)transition_height,
        shell->surface,
        "MSYS Launch Transition",
        "org.msys.shell.native.launch-transition",
        "animation-mask",
        0
    );
    {
        XSetWindowAttributes overlay_attributes;
        overlay_attributes.override_redirect = True;
        XChangeWindowAttributes(
            shell->display, shell->controls, CWOverrideRedirect, &overlay_attributes
        );
        XChangeWindowAttributes(
            shell->display,
            shell->launch_transition,
            CWOverrideRedirect,
            &overlay_attributes
        );
    }
    {
        int toast_width = shell->layout.width > 20 ? shell->layout.width - 20 : 1;
        int toast_x;
        if (toast_width > TOAST_MAX_WIDTH) toast_width = TOAST_MAX_WIDTH;
        toast_x = (shell->layout.width - toast_width) / 2;
        shell->toast = create_window(
            shell,
            toast_x,
            shell->layout.bar_height + 10,
            (unsigned int)toast_width,
            76u,
            shell->nav_background,
            "MSYS Notifications",
            "org.msys.shell.native.notifications",
            "notification-presenter",
            1
        );
    }
    XMapWindow(shell->display, shell->launcher);
    XMapRaised(shell->display, shell->chrome);
    XMapRaised(shell->display, shell->navigation);
    XFlush(shell->display);
    return 1;
}

static int initialize_ipc(native_shell *shell)
{
    char *packet;
    char type[32];
    int result;
    if (getenv("MSYS_CONTROL_FD") == NULL) {
        shell->supervised = 0;
        shell->apps_state = CATALOG_ERROR;
        shell->tasks_state = CATALOG_ERROR;
        (void)snprintf(shell->apps_message, sizeof(shell->apps_message), "%s", tr(shell, "error.core_unavailable"));
        (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.window_manager_unavailable"));
        draw_launcher(shell);
        XFlush(shell->display);
        return 1;
    }
    shell->supervised = 1;
    result = msys_mipc_client_from_env(&shell->ipc);
    if (result != MSYS_MIPC_OK) {
        fprintf(stderr, "msys-shell-native: invalid component channel: %s\n", msys_mipc_result_string(result));
        return 0;
    }
    packet = (char *)malloc(MSYS_MIPC_RECV_CAPACITY);
    if (packet == NULL) {
        return 0;
    }
    result = msys_mipc_send_hello_from_env(&shell->ipc);
    if (result == MSYS_MIPC_OK) {
        result = msys_mipc_recv_json(
            &shell->ipc,
            packet,
            MSYS_MIPC_RECV_CAPACITY,
            3000,
            NULL
        );
    }
    if (
        result != MSYS_MIPC_OK ||
        msys_mipc_json_get_string(packet, "type", type, sizeof(type), NULL) != MSYS_MIPC_OK ||
        strcmp(type, "welcome") != 0
    ) {
        fprintf(stderr, "msys-shell-native: component handshake failed\n");
        free(packet);
        return 0;
    }
    free(packet);
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.role.notification-presenter");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.display.output_recovered");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.role.navigation-bar");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.role.system-chrome");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.install.package_changed");
    (void)msys_mipc_send_subscribe(&shell->ipc, "msys.lifecycle.transition");
    result = msys_mipc_send_ready(&shell->ipc);
    if (result != MSYS_MIPC_OK) {
        return 0;
    }
    (void)msys_mipc_send_event_json(
        &shell->ipc,
        "msys.role.ready",
        "{\"role\":\"native-shell\",\"roles\":[\"launcher\",\"system-chrome\",\"navigation-bar\",\"task-switcher\",\"notification-presenter\"]}"
    );
    request_apps(shell);
    XFlush(shell->display);
    return 1;
}

static void apply_preferences_visual(native_shell *shell)
{
    shell->background = color(
        shell,
        shell->preferences.wallpaper_color,
        shell->background
    );
    shell->accent = color(
        shell,
        shell->preferences.accent_color,
        shell->accent
    );
    shell->accent_soft = shell->accent;
    current_profile(shell, shell->root_width, shell->root_height);
    image_caches_dispose(shell->app_icons, MSYS_NATIVE_MAX_APPS);
    XSetWindowBackground(shell->display, shell->launcher, shell->background);
    sort_apps(shell);
    draw_launcher(shell);
    draw_navigation(shell);
    if (shell->recents_visible != 0) {
        draw_recents(shell);
    }
    XFlush(shell->display);
}

static void raise_system_bars(native_shell *shell)
{
    XRaiseWindow(shell->display, shell->chrome);
    XRaiseWindow(shell->display, shell->navigation);
    if (shell->toast_visible != 0) XRaiseWindow(shell->display, shell->toast);
}

static void hide_launch_transition(native_shell *shell)
{
    if (shell->launch_transition_visible == 0) return;
    shell->launch_transition_visible = 0;
    shell->launch_transition_frame = 0;
    shell->launch_transition_animation_at_ms = 0u;
    shell->launch_transition_until_ms = 0u;
    shell->launch_transition_component[0] = '\0';
    XUnmapWindow(shell->display, shell->launch_transition);
}

static void hide_toast(native_shell *shell)
{
    if (shell->toast_visible == 0) return;
    shell->toast_visible = 0;
    shell->toast_until_ms = 0u;
    shell->toast_animation_frame = 0;
    shell->toast_animation_at_ms = 0u;
    XUnmapWindow(shell->display, shell->toast);
}

static void hide_recents(native_shell *shell, int restore_task)
{
    int was_mapped;
    if (shell->recents_visible == 0) return;
    was_mapped = shell->recents_mapped;
    shell->recents_visible = 0;
    shell->recents_mapped = 0;
    shell->recents_dragging = 0;
    shell->recents_horizontal_drag = 0;
    shell->recents_drag_offset = 0;
    shell->recents_pressed = -1;
    shell->recents_pulse = -1;
    shell->recents_pulse_until_ms = 0u;
    shell->recents_close_pressed = 0;
    shell->tasks_refresh_queued = 0;
    if (shell->recents_pointer_active != 0) {
        shell->recents_pointer_active = 0;
        XUngrabPointer(shell->display, CurrentTime);
    }
    XUnmapWindow(shell->display, shell->recents);
    image_caches_dispose(shell->task_previews, MSYS_NATIVE_MAX_TASKS);
    if (
        restore_task != 0 && was_mapped != 0 && shell->task_count > 0u &&
        pending_has_kind(shell, PENDING_TASK_ACTIVATE) == 0
    ) activate_task(shell, 0u);
}

static void hide_controls(native_shell *shell)
{
    if (shell->controls_visible == 0) return;
    shell->controls_visible = 0;
    shell->controls_pressed = -1;
    shell->controls_pressed_zone = -1;
    XUnmapWindow(shell->display, shell->controls);
}

static void show_launch_transition(native_shell *shell, size_t index)
{
    uint64_t current;
    if (index >= shell->app_count) return;
    current = now_ms();
    hide_toast(shell);
    hide_recents(shell, 0);
    hide_controls(shell);
    shell->launch_transition_visible = 1;
    shell->launch_transition_app_index = index;
    shell->launch_transition_frame = 0;
    shell->launch_transition_animation_at_ms =
        animation_frame_limit(LAUNCH_TRANSITION_FRAMES) > 1
            ? current + LAUNCH_TRANSITION_FRAME_MS : 0u;
    shell->launch_transition_until_ms = current + LAUNCH_TRANSITION_MAX_MS;
    (void)snprintf(
        shell->launch_transition_component,
        sizeof(shell->launch_transition_component),
        "%s",
        shell->apps[index].component
    );
    layout_launch_transition(shell);
    XMapRaised(shell->display, shell->launch_transition);
    raise_system_bars(shell);
    XFlush(shell->display);
}

static void present_recents(native_shell *shell)
{
    if (shell->recents_visible == 0) return;
    if (shell->recents_mapped == 0) {
        XMapRaised(shell->display, shell->recents);
        shell->recents_mapped = 1;
    }
    raise_system_bars(shell);
    draw_recents(shell);
}

static void refresh_recents_presentation(native_shell *shell)
{
    XWindowAttributes attributes;
    msys_native_recents_layout layout;

    if (
        shell->recents_visible != 0 && shell->recents_mapped != 0 &&
        XGetWindowAttributes(shell->display, shell->recents, &attributes)
    ) {
        recents_layout(shell, attributes.width, attributes.height, &layout);
        redraw_recents_viewport(shell, &attributes, &layout);
        return;
    }
    present_recents(shell);
}

static void show_recents(native_shell *shell)
{
    hide_launch_transition(shell);
    hide_controls(shell);
    shell->recents_visible = 1;
    shell->recents_scroll = 0;
    shell->recents_pressed = -1;
    shell->overview_accent_until_ms = now_ms() + OVERVIEW_ACCENT_MS;
    /*
     * list_windows is asynchronous. Keep the opaque task-switcher unmapped
     * until its reply (or error/timeout) so the policy can capture the real
     * foreground application instead of this surface.
     */
    request_recents(shell);
    if (pending_has_kind(shell, PENDING_RECENTS_LIST) == 0) {
        present_recents(shell);
    }
    XFlush(shell->display);
}

static void show_controls(native_shell *shell)
{
    hide_launch_transition(shell);
    hide_recents(shell, 0);
    if (shell->controls_visible != 0) {
        hide_controls(shell);
        XFlush(shell->display);
        return;
    }
    shell->controls_visible = 1;
    shell->controls_pressed = -1;
    shell->controls_pressed_zone = -1;
    XMapRaised(shell->display, shell->controls);
    raise_system_bars(shell);
    request_audio_state(shell);
    draw_controls(shell);
    XFlush(shell->display);
}

static void hide_overlays(native_shell *shell, int restore_task)
{
    hide_recents(shell, restore_task);
    hide_controls(shell);
    hide_launch_transition(shell);
    hide_toast(shell);
    XFlush(shell->display);
}

static void show_toast_for(
    native_shell *shell,
    const char *message,
    uint64_t duration_ms
)
{
    uint64_t current = now_ms();
    (void)snprintf(
        shell->toast_text,
        sizeof(shell->toast_text),
        "%s",
        message != NULL && *message != '\0' ? message : tr(shell, "toast.notification")
    );
    /* A burst may replace the text, but it must not keep an overlay mapped
     * forever by extending the deadline on every notification. */
    if (shell->toast_visible == 0) {
        shell->toast_visible = 1;
        shell->toast_until_ms = current + duration_ms;
        shell->toast_animation_frame = 0;
        shell->toast_animation_at_ms =
            animation_frame_limit(TOAST_ANIMATION_FRAMES) > 1
                ? current + TOAST_ANIMATION_FRAME_MS : 0u;
    }
    XMapRaised(shell->display, shell->toast);
    draw_toast(shell);
    XFlush(shell->display);
}

static void show_toast(native_shell *shell, const char *message)
{
    show_toast_for(shell, message, TOAST_VISIBLE_MS);
}

static void show_notification_center(native_shell *shell)
{
    if (pending_has_kind(shell, PENDING_NOTIFICATION_CENTER) != 0) return;
    hide_launch_transition(shell);
    hide_controls(shell);
    if (send_async(
        shell,
        PENDING_NOTIFICATION_CENTER,
        0u,
        "role:notification-center",
        "show",
        "{}",
        6000u,
        0
    ) == 0u) {
        show_toast(shell, tr(shell, "error.notification_center_unavailable"));
    }
}

static void start_settings_panel(native_shell *shell, const char *panel, size_t index)
{
    char payload[256];
    if (panel == NULL || pending_has_kind(shell, PENDING_SETTINGS_PANEL) != 0) return;
    (void)snprintf(
        payload,
        sizeof(payload),
        "{\"component\":\"org.msys.settings:main\",\"activation\":{\"action\":\"settings-panel\",\"name\":\"%s\"}}",
        panel
    );
    if (send_async(
        shell,
        PENDING_SETTINGS_PANEL,
        index,
        "msys.core",
        "start",
        payload,
        8000u,
        0
    ) == 0u) {
        show_toast(shell, tr(shell, "error.settings_unavailable"));
        return;
    }
    hide_controls(shell);
    XFlush(shell->display);
}

static int payload_bool_value(const char *payload, const char *field, int *value)
{
    const char *raw = NULL;
    size_t length = 0u;
    if (
        value == NULL ||
        msys_mipc_json_get_raw(payload, field, &raw, &length) != MSYS_MIPC_OK
    ) return 0;
    if (length == 4u && memcmp(raw, "true", 4u) == 0) {
        *value = 1;
        return 1;
    }
    if (length == 5u && memcmp(raw, "false", 5u) == 0) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int payload_percent_value(const char *payload, const char *field, int *value)
{
    const char *raw = NULL;
    size_t length = 0u;
    size_t index;
    int result = 0;
    if (
        value == NULL ||
        msys_mipc_json_get_raw(payload, field, &raw, &length) != MSYS_MIPC_OK ||
        length == 0u || length > 3u
    ) return 0;
    for (index = 0u; index < length; index++) {
        if (raw[index] < '0' || raw[index] > '9') return 0;
        result = result * 10 + (raw[index] - '0');
    }
    if (result > 100) return 0;
    *value = result;
    return 1;
}

static void audio_mark_unavailable(native_shell *shell, const char *reason)
{
    shell->audio_known = 1;
    shell->audio_loading = 0;
    shell->audio_available = 0;
    shell->audio_muted = 0;
    shell->audio_volume_percent = -1;
    shell->audio_output_name[0] = '\0';
    (void)snprintf(
        shell->audio_reason,
        sizeof(shell->audio_reason),
        "%s",
        reason != NULL && reason[0] != '\0' ? reason : "provider-unavailable"
    );
}

static int apply_audio_state(native_shell *shell, const char *payload)
{
    char schema[64];
    char name[sizeof(shell->audio_output_name)];
    char reason[sizeof(shell->audio_reason)];
    int available;
    int muted;
    int volume;
    if (
        msys_mipc_json_get_string(
            payload, "schema", schema, sizeof(schema), NULL
        ) != MSYS_MIPC_OK ||
        strcmp(schema, "msys.audio-state.v1") != 0 ||
        payload_bool_value(payload, "available", &available) == 0
    ) return 0;
    shell->audio_known = 1;
    shell->audio_loading = 0;
    shell->audio_available = available;
    if (available == 0) {
        if (
            msys_mipc_json_get_string(
                payload, "reason", reason, sizeof(reason), NULL
            ) != MSYS_MIPC_OK
        ) (void)snprintf(reason, sizeof(reason), "%s", "provider-unavailable");
        audio_mark_unavailable(shell, reason);
        return 1;
    }
    if (
        msys_mipc_json_get_string(
            payload, "output_name", name, sizeof(name), NULL
        ) != MSYS_MIPC_OK ||
        payload_percent_value(payload, "volume_percent", &volume) == 0 ||
        payload_bool_value(payload, "muted", &muted) == 0
    ) return 0;
    shell->audio_muted = muted;
    shell->audio_volume_percent = volume;
    (void)snprintf(
        shell->audio_output_name,
        sizeof(shell->audio_output_name),
        "%s",
        name
    );
    shell->audio_reason[0] = '\0';
    return 1;
}

static int audio_call_pending(const native_shell *shell)
{
    return (
        pending_has_kind(shell, PENDING_AUDIO_STATE) != 0 ||
        pending_has_kind(shell, PENDING_AUDIO_VOLUME) != 0 ||
        pending_has_kind(shell, PENDING_AUDIO_MUTE) != 0
    );
}

static void request_audio_state(native_shell *shell)
{
    if (audio_call_pending(shell) != 0) return;
    shell->audio_loading = 1;
    if (
        send_async(
            shell,
            PENDING_AUDIO_STATE,
            0u,
            "role:audio-manager",
            "get_state",
            "{}",
            AUDIO_STATE_TIMEOUT_MS,
            1
        ) == 0u
    ) {
        audio_mark_unavailable(shell, "provider-unavailable");
    }
    if (shell->controls_visible != 0) redraw_controls_row(shell, AUDIO_CONTROL_ROW);
}

static void activate_audio_control(native_shell *shell, int zone)
{
    char payload[64];
    enum pending_kind kind;
    const char *method;
    int percent;
    if (zone < 0 || zone > 2) return;
    if (shell->audio_available == 0) {
        request_audio_state(shell);
        show_toast(shell, tr(shell, "error.audio_unavailable"));
        return;
    }
    if (audio_call_pending(shell) != 0) {
        show_toast(shell, tr(shell, "error.busy"));
        return;
    }
    if (zone == 1) {
        kind = PENDING_AUDIO_MUTE;
        method = "set_muted";
        (void)snprintf(
            payload,
            sizeof(payload),
            "{\"muted\":%s}",
            shell->audio_muted != 0 ? "false" : "true"
        );
    } else {
        percent = shell->audio_volume_percent + (zone == 0 ? -10 : 10);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        kind = PENDING_AUDIO_VOLUME;
        method = "set_volume";
        (void)snprintf(payload, sizeof(payload), "{\"percent\":%d}", percent);
    }
    if (
        send_async(
            shell,
            kind,
            0u,
            "role:audio-manager",
            method,
            payload,
            AUDIO_WRITE_TIMEOUT_MS,
            1
        ) == 0u
    ) {
        show_toast(shell, tr(shell, "error.audio_update_failed"));
    }
}

static void request_apps(native_shell *shell)
{
    if (pending_has_kind(shell, PENDING_APPS_LIST) != 0) {
        return;
    }
    if (shell->apps_state != CATALOG_READY && shell->apps_state != CATALOG_EMPTY) {
        shell->apps_state = CATALOG_LOADING;
    }
    shell->apps_message[0] = '\0';
    if (shell->apps_state == CATALOG_LOADING) draw_launcher(shell);
    if (
        send_async(
            shell,
            PENDING_APPS_LIST,
            0u,
            "msys.core",
            "list_apps",
            "{}",
            7000u,
            1
        ) == 0u
    ) {
        shell->apps_state = CATALOG_ERROR;
        (void)snprintf(
            shell->apps_message,
            sizeof(shell->apps_message),
            "%s", tr(shell, "error.core_unavailable")
        );
        draw_launcher(shell);
    }
}

static void request_recents(native_shell *shell)
{
    if (pending_has_kind(shell, PENDING_RECENTS_LIST) != 0) {
        shell->tasks_refresh_queued = 1;
        return;
    }
    if (shell->tasks_state != CATALOG_READY && shell->tasks_state != CATALOG_EMPTY) {
        shell->tasks_state = CATALOG_LOADING;
    }
    shell->tasks_message[0] = '\0';
    if (
        send_async(
            shell,
            PENDING_RECENTS_LIST,
            0u,
            "role:window-manager",
            "list_windows",
            "{}",
            7000u,
            1
        ) == 0u
    ) {
        shell->tasks_state = CATALOG_ERROR;
        (void)snprintf(
            shell->tasks_message,
            sizeof(shell->tasks_message),
            "%s", tr(shell, "error.window_manager_unavailable")
        );
    }
}

static int component_payload(
    const char *component,
    char *payload,
    size_t capacity
)
{
    char escaped[MSYS_NATIVE_COMPONENT_CAPACITY * 2u];
    int written;
    if (
        component == NULL ||
        payload == NULL ||
        capacity == 0u ||
        strchr(component, ':') == NULL ||
        msys_native_json_escape(component, escaped, sizeof(escaped)) == 0
    ) {
        return 0;
    }
    written = snprintf(payload, capacity, "{\"component\":\"%s\"}", escaped);
    return written >= 0 && (size_t)written < capacity;
}

static int window_payload(
    const char *window_id,
    char *payload,
    size_t capacity
)
{
    char escaped[MSYS_NATIVE_WINDOW_ID_CAPACITY * 2u];
    int written;
    if (
        window_id == NULL ||
        payload == NULL ||
        capacity == 0u ||
        *window_id == '\0' ||
        msys_native_json_escape(window_id, escaped, sizeof(escaped)) == 0
    ) {
        return 0;
    }
    written = snprintf(payload, capacity, "{\"id\":\"%s\"}", escaped);
    return written >= 0 && (size_t)written < capacity;
}

static void activate_app(native_shell *shell, size_t index)
{
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 32u];
    if (pending_has_kind(shell, PENDING_APP_START) != 0) {
        show_toast(shell, tr(shell, "error.busy"));
        return;
    }
    if (index >= shell->app_count || component_payload(
        shell->apps[index].component,
        payload,
        sizeof(payload)
    ) == 0) {
        return;
    }
    (void)snprintf(
        shell->apps_message,
        sizeof(shell->apps_message),
        "%s %s…",
        tr(shell, "launcher.starting"),
        shell->apps[index].name
    );
    show_launch_transition(shell, index);
    if (
        send_async(
            shell,
            PENDING_APP_START,
            index,
            "msys.core",
            "start",
            payload,
            8000u,
            0
        ) == 0u
    ) {
        hide_launch_transition(shell);
        (void)snprintf(shell->apps_message, sizeof(shell->apps_message), "%s", tr(shell, "error.busy"));
    }
    if (shell->apps_message[0] != '\0' && pending_has_kind(shell, PENDING_APP_START) == 0) {
        show_toast(shell, shell->apps_message);
    }
}

static void activate_task(native_shell *shell, size_t index)
{
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 32u];
    const char *target;
    const char *method;
    if (index >= shell->task_count) {
        return;
    }
    if (shell->tasks[index].component[0] != '\0') {
        if (component_payload(shell->tasks[index].component, payload, sizeof(payload)) == 0) {
            return;
        }
        target = "msys.core";
        method = "start";
    } else {
        if (window_payload(shell->tasks[index].window_id, payload, sizeof(payload)) == 0) {
            return;
        }
        target = "role:window-manager";
        method = "focus_window";
    }
    if (
        send_async(
            shell,
            PENDING_TASK_ACTIVATE,
            index,
            target,
            method,
            payload,
            8000u,
            0
        ) == 0u
    ) {
        shell->tasks_state = CATALOG_ERROR;
        (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.busy"));
        draw_recents(shell);
    }
}

static void close_task(native_shell *shell, size_t index)
{
    char payload[MSYS_NATIVE_COMPONENT_CAPACITY * 2u + 32u];
    const char *target;
    const char *method;
    if (index >= shell->task_count) {
        return;
    }
    if (shell->tasks[index].component[0] != '\0') {
        if (component_payload(shell->tasks[index].component, payload, sizeof(payload)) == 0) {
            return;
        }
        target = "msys.core";
        method = "stop";
    } else {
        if (window_payload(shell->tasks[index].window_id, payload, sizeof(payload)) == 0) {
            return;
        }
        target = "role:window-manager";
        method = "close_window";
    }
    if (
        send_async(
            shell,
            PENDING_TASK_CLOSE,
            index,
            target,
            method,
            payload,
            8000u,
            0
        ) == 0u
    ) {
        shell->tasks_state = CATALOG_ERROR;
        (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.busy"));
        draw_recents(shell);
    }
}

static void send_navigation(native_shell *shell, enum msys_native_navigation_action action)
{
    const char *name;
    char payload[96];
    if (action == MSYS_NATIVE_NAV_NONE) {
        return;
    }
    hide_launch_transition(shell);
    if (
        action == MSYS_NATIVE_NAV_BACK &&
        (shell->recents_visible != 0 || shell->controls_visible != 0)
    ) {
        hide_recents(shell, 1);
        hide_controls(shell);
        shell->nav_feedback = 1;
        shell->nav_feedback_action = action;
        shell->nav_feedback_until_ms = now_ms() + NAV_FEEDBACK_MS;
        draw_navigation_action_damage(shell, action);
        XFlush(shell->display);
        return;
    }
    if (action == MSYS_NATIVE_NAV_APPS && shell->recents_visible != 0) {
        hide_recents(shell, 1);
        shell->nav_feedback = 1;
        shell->nav_feedback_action = action;
        shell->nav_feedback_until_ms = now_ms() + NAV_FEEDBACK_MS;
        draw_navigation_action_damage(shell, action);
        XFlush(shell->display);
        return;
    }
    if (action == MSYS_NATIVE_NAV_HOME) {
        hide_recents(shell, 0);
        hide_controls(shell);
    }
    name = action == MSYS_NATIVE_NAV_BACK
        ? "back"
        : (action == MSYS_NATIVE_NAV_HOME ? "home" : "apps");
    shell->nav_feedback = 1;
    shell->nav_feedback_action = action;
    shell->nav_feedback_until_ms = now_ms() + NAV_FEEDBACK_MS;
    draw_navigation_action_damage(shell, action);
    XFlush(shell->display);
    if (shell->supervised == 0) {
        if (action == MSYS_NATIVE_NAV_APPS) {
            show_recents(shell);
        }
        return;
    }
    (void)snprintf(
        payload,
        sizeof(payload),
        "{\"action\":\"%s\",\"input\":\"%s\"}",
        name,
        shell->buttons_mode != 0 ? "button" : "swipe"
    );
    if (
        send_async(
            shell,
            PENDING_NAVIGATION,
            0u,
            "role:window-manager",
            "navigation_action",
            payload,
            7000u,
            0
        ) == 0u
    ) {
        shell->nav_feedback = -1;
    }
}

static int payload_message(
    const char *packet,
    char *payload,
    size_t payload_capacity,
    char *message,
    size_t message_capacity
)
{
    const char *raw = NULL;
    size_t length = 0u;
    if (
        msys_mipc_json_get_raw(packet, "payload", &raw, &length) != MSYS_MIPC_OK ||
        length + 1u > payload_capacity
    ) {
        (void)snprintf(payload, payload_capacity, "{}");
        return 0;
    }
    memcpy(payload, raw, length);
    payload[length] = '\0';
    return msys_mipc_json_get_string(
        payload,
        "message",
        message,
        message_capacity,
        NULL
    ) == MSYS_MIPC_OK;
}

static const char *catalog_state_name(enum catalog_state state)
{
    switch (state) {
    case CATALOG_LOADING:
        return "loading";
    case CATALOG_READY:
        return "ready";
    case CATALOG_EMPTY:
        return "empty";
    case CATALOG_ERROR:
        return "error";
    default:
        return "error";
    }
}

static int serialize_apps(native_shell *shell, char *response, size_t capacity)
{
    size_t used = 0u;
    size_t index;
    size_t emitted = 0u;
    char error[MSYS_NATIVE_SUMMARY_CAPACITY * 6u + 1u];
    if (msys_native_json_escape(shell->apps_message, error, sizeof(error)) == 0) {
        return 0;
    }
    if (!append_format(
        response,
        capacity,
        &used,
        "{\"schema\":\"msys.native-launcher-list.v1\",\"state\":\"%s\",\"error\":\"%s\",\"apps\":[",
        catalog_state_name(shell->apps_state),
        error
    )) {
        return 0;
    }
    for (index = 0u; index < shell->app_count; index++) {
        char component[MSYS_NATIVE_COMPONENT_CAPACITY * 6u + 1u];
        char name[MSYS_NATIVE_NAME_CAPACITY * 6u + 1u];
        char summary[MSYS_NATIVE_SUMMARY_CAPACITY * 6u + 1u];
        char icon[MSYS_NATIVE_PATH_CAPACITY * 6u + 1u];
        size_t checkpoint = used;
        if (
            msys_native_json_escape(shell->apps[index].component, component, sizeof(component)) == 0 ||
            msys_native_json_escape(shell->apps[index].name, name, sizeof(name)) == 0 ||
            msys_native_json_escape(shell->apps[index].summary, summary, sizeof(summary)) == 0 ||
            msys_native_json_escape(shell->apps[index].icon_path, icon, sizeof(icon)) == 0 ||
            !append_format(
                response,
                capacity,
                &used,
                "%s{\"component\":\"%s\",\"name\":\"%s\",\"summary\":\"%s\",\"icon\":\"%s\"}",
                emitted == 0u ? "" : ",",
                component,
                name,
                summary,
                icon
            )
        ) {
            used = checkpoint;
            response[used] = '\0';
            break;
        }
        emitted++;
    }
    return append_format(
        response,
        capacity,
        &used,
        "],\"count\":%zu,\"total\":%zu,\"truncated\":%s}",
        emitted,
        shell->app_count,
        emitted < shell->app_count ? "true" : "false"
    );
}

static int serialize_tasks(native_shell *shell, char *response, size_t capacity)
{
    size_t used = 0u;
    size_t index;
    size_t emitted = 0u;
    char error[MSYS_NATIVE_SUMMARY_CAPACITY * 6u + 1u];
    if (msys_native_json_escape(shell->tasks_message, error, sizeof(error)) == 0) {
        return 0;
    }
    if (!append_format(
        response,
        capacity,
        &used,
        "{\"schema\":\"msys.native-recents-list.v1\",\"state\":\"%s\",\"error\":\"%s\",\"tasks\":[",
        catalog_state_name(shell->tasks_state),
        error
    )) {
        return 0;
    }
    for (index = 0u; index < shell->task_count; index++) {
        char component[MSYS_NATIVE_COMPONENT_CAPACITY * 6u + 1u];
        char window_id[MSYS_NATIVE_WINDOW_ID_CAPACITY * 6u + 1u];
        char title[MSYS_NATIVE_NAME_CAPACITY * 6u + 1u];
        char thumbnail[MSYS_NATIVE_PATH_CAPACITY * 6u + 1u];
        size_t checkpoint = used;
        if (
            msys_native_json_escape(shell->tasks[index].component, component, sizeof(component)) == 0 ||
            msys_native_json_escape(shell->tasks[index].window_id, window_id, sizeof(window_id)) == 0 ||
            msys_native_json_escape(shell->tasks[index].title, title, sizeof(title)) == 0 ||
            msys_native_json_escape(shell->tasks[index].thumbnail, thumbnail, sizeof(thumbnail)) == 0 ||
            !append_format(
                response,
                capacity,
                &used,
                "%s{\"component\":\"%s\",\"id\":\"%s\",\"title\":\"%s\",\"thumbnail\":\"%s\"}",
                emitted == 0u ? "" : ",",
                component,
                window_id,
                title,
                thumbnail
            )
        ) {
            used = checkpoint;
            response[used] = '\0';
            break;
        }
        emitted++;
    }
    return append_format(
        response,
        capacity,
        &used,
        "],\"count\":%zu,\"total\":%zu,\"truncated\":%s}",
        emitted,
        shell->task_count,
        emitted < shell->task_count ? "true" : "false"
    );
}

static int payload_component_index(
    const char *payload,
    const msys_native_app *apps,
    size_t count,
    size_t *index
)
{
    char component[MSYS_NATIVE_COMPONENT_CAPACITY];
    size_t position;
    if (
        msys_mipc_json_get_string(
            payload,
            "component",
            component,
            sizeof(component),
            NULL
        ) != MSYS_MIPC_OK
    ) {
        return 0;
    }
    for (position = 0u; position < count; position++) {
        if (strcmp(apps[position].component, component) == 0) {
            *index = position;
            return 1;
        }
    }
    return 0;
}

static int payload_task_index(
    const char *payload,
    const msys_native_task *tasks,
    size_t count,
    size_t *index
)
{
    char component[MSYS_NATIVE_COMPONENT_CAPACITY] = "";
    char window_id[MSYS_NATIVE_WINDOW_ID_CAPACITY] = "";
    size_t position;
    (void)msys_mipc_json_get_string(
        payload,
        "component",
        component,
        sizeof(component),
        NULL
    );
    (void)msys_mipc_json_get_string(
        payload,
        "id",
        window_id,
        sizeof(window_id),
        NULL
    );
    for (position = 0u; position < count; position++) {
        if (
            (component[0] != '\0' && strcmp(tasks[position].component, component) == 0) ||
            (window_id[0] != '\0' && strcmp(tasks[position].window_id, window_id) == 0)
        ) {
            *index = position;
            return 1;
        }
    }
    return 0;
}

static void handle_call(native_shell *shell, const char *packet)
{
    uint64_t request_id = 0u;
    char method[80];
    char payload[2048];
    char message[192];
    int has_message;
    enum msys_native_method_action action;
    if (
        msys_mipc_json_get_u64(packet, "id", &request_id) != MSYS_MIPC_OK ||
        msys_mipc_json_get_string(packet, "method", method, sizeof(method), NULL) != MSYS_MIPC_OK
    ) {
        return;
    }
    has_message = payload_message(
        packet,
        payload,
        sizeof(payload),
        message,
        sizeof(message)
    );
    if (strcmp(method, "refresh_apps") == 0) {
        request_apps(shell);
        (void)msys_mipc_send_return_json(
            &shell->ipc,
            request_id,
            "{\"ok\":true,\"queued\":true}"
        );
        return;
    }
    if (strcmp(method, "activate_app") == 0) {
        size_t index = 0u;
        if (payload_component_index(payload, shell->apps, shell->app_count, &index) == 0) {
            (void)msys_mipc_send_error(
                &shell->ipc,
                request_id,
                "UNKNOWN_APP",
                "component is not in the authoritative cached app list"
            );
        } else {
            activate_app(shell, index);
            (void)msys_mipc_send_return_json(
                &shell->ipc,
                request_id,
                "{\"ok\":true,\"queued\":true}"
            );
        }
        return;
    }
    if (strcmp(method, "list_recents") == 0) {
        char response[RESPONSE_CAPACITY];
        if (serialize_tasks(shell, response, sizeof(response)) == 0) {
            (void)msys_mipc_send_error(&shell->ipc, request_id, "RESPONSE_TOO_LARGE", method);
        } else {
            (void)msys_mipc_send_return_json(&shell->ipc, request_id, response);
        }
        return;
    }
    if (strcmp(method, "refresh_recents") == 0) {
        request_recents(shell);
        (void)msys_mipc_send_return_json(
            &shell->ipc,
            request_id,
            "{\"ok\":true,\"queued\":true}"
        );
        return;
    }
    if (strcmp(method, "activate_task") == 0 || strcmp(method, "close_task") == 0) {
        size_t index = 0u;
        if (payload_task_index(payload, shell->tasks, shell->task_count, &index) == 0) {
            (void)msys_mipc_send_error(
                &shell->ipc,
                request_id,
                "UNKNOWN_TASK",
                "task is not in the authoritative cached window list"
            );
        } else {
            if (strcmp(method, "activate_task") == 0) {
                activate_task(shell, index);
            } else {
                close_task(shell, index);
            }
            (void)msys_mipc_send_return_json(
                &shell->ipc,
                request_id,
                "{\"ok\":true,\"queued\":true}"
            );
        }
        return;
    }
    action = msys_native_route_method(method, has_message);
    switch (action) {
    case MSYS_NATIVE_METHOD_LIST_APPS:
        {
            char response[RESPONSE_CAPACITY];
            if (serialize_apps(shell, response, sizeof(response)) == 0) {
                (void)msys_mipc_send_error(&shell->ipc, request_id, "RESPONSE_TOO_LARGE", method);
            } else {
                (void)msys_mipc_send_return_json(&shell->ipc, request_id, response);
            }
        }
        break;
    case MSYS_NATIVE_METHOD_STATUS:
    case MSYS_NATIVE_METHOD_GET_PREFERENCES: {
        char response[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
        if (msys_native_preferences_empty_request(payload) == 0) {
            (void)msys_mipc_send_error(
                &shell->ipc, request_id, "BAD_REQUEST", "payload must be an empty object"
            );
        } else if (!msys_native_preferences_state_json(
            &shell->preferences, response, sizeof(response)
        )) {
            (void)msys_mipc_send_error(
                &shell->ipc, request_id, "BAD_PREFERENCES", "preference state is too large"
            );
        } else {
            (void)msys_mipc_send_return_json(&shell->ipc, request_id, response);
        }
        break;
    }
    case MSYS_NATIVE_METHOD_SET_PREFERENCES:
    case MSYS_NATIVE_METHOD_RESET_PREFERENCES: {
        msys_native_preferences candidate;
        enum msys_native_preferences_result preference_result;
        char response[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
        char event[MSYS_NATIVE_PREFERENCES_JSON_CAPACITY];
        int reset = action == MSYS_NATIVE_METHOD_RESET_PREFERENCES;
        if (reset != 0) {
            if (msys_native_preferences_empty_request(payload) == 0) {
                (void)msys_mipc_send_error(
                    &shell->ipc, request_id, "BAD_REQUEST", "payload must be an empty object"
                );
                break;
            }
            msys_native_preferences_defaults(&candidate);
        } else {
            preference_result = msys_native_preferences_merge(
                payload, &shell->preferences, &candidate
            );
            if (preference_result != MSYS_NATIVE_PREFERENCES_OK) {
                (void)msys_mipc_send_error(
                    &shell->ipc,
                    request_id,
                    preference_result == MSYS_NATIVE_PREFERENCES_BAD_REQUEST
                        ? "BAD_REQUEST" : "BAD_PREFERENCES",
                    "invalid launcher preferences"
                );
                break;
            }
        }
        if (shell->preferences.revision == UINT64_MAX) {
            (void)msys_mipc_send_error(
                &shell->ipc, request_id, "BAD_PREFERENCES", "preference revision is exhausted"
            );
            break;
        }
        candidate.revision = shell->preferences.revision + 1u;
        preference_result = msys_native_preferences_commit(
            shell->preferences_path, &candidate
        );
        if (preference_result != MSYS_NATIVE_PREFERENCES_OK) {
            (void)msys_mipc_send_error(
                &shell->ipc, request_id, "BAD_PREFERENCES", "atomic preference commit failed"
            );
            break;
        }
        shell->preferences = candidate;
        apply_preferences_visual(shell);
        if (!msys_native_preferences_state_json(
                &shell->preferences, response, sizeof(response)
            ) || !msys_native_preferences_event_json(
                &shell->preferences, reset, event, sizeof(event)
            )) {
            (void)msys_mipc_send_error(
                &shell->ipc, request_id, "BAD_PREFERENCES", "preference state is too large"
            );
            break;
        }
        (void)msys_mipc_send_return_json(&shell->ipc, request_id, response);
        (void)msys_mipc_send_event_json(
            &shell->ipc, "msys.shell.preferences.changed", event
        );
        break;
    }
    case MSYS_NATIVE_METHOD_SHOW_RECENTS:
        show_recents(shell);
        (void)msys_mipc_send_return_json(
            &shell->ipc,
            request_id,
            "{\"ok\":true,\"visible\":true,\"loading\":true}"
        );
        break;
    case MSYS_NATIVE_METHOD_SHOW_NOTIFICATION:
        show_toast(shell, message);
        (void)msys_mipc_send_return_json(
            &shell->ipc,
            request_id,
            "{\"ok\":true,\"visible\":true}"
        );
        break;
    case MSYS_NATIVE_METHOD_HIDE_OVERLAYS:
        hide_overlays(shell, 1);
        (void)msys_mipc_send_return_json(
            &shell->ipc,
            request_id,
            "{\"ok\":true,\"visible\":false}"
        );
        break;
    case MSYS_NATIVE_METHOD_NOT_IMPLEMENTED:
        (void)msys_mipc_send_error(
            &shell->ipc,
            request_id,
            "NOT_IMPLEMENTED",
            "method is outside the supported native shell surface"
        );
        break;
    case MSYS_NATIVE_METHOD_UNKNOWN:
    default:
        (void)msys_mipc_send_error(&shell->ipc, request_id, "NO_METHOD", method);
        break;
    }
}

static int payload_has_field(const char *payload, const char *field)
{
    const char *raw = NULL;
    size_t length = 0u;
    return msys_mipc_json_get_raw(payload, field, &raw, &length) == MSYS_MIPC_OK;
}

static int payload_is_false(const char *payload, const char *field)
{
    const char *raw = NULL;
    size_t length = 0u;
    return (
        msys_mipc_json_get_raw(payload, field, &raw, &length) == MSYS_MIPC_OK &&
        length == 5u &&
        memcmp(raw, "false", 5u) == 0
    );
}

static int display_recovery_requires_notice(native_shell *shell, const char *payload)
{
    char schema[64];
    char fault[64];
    char provider[MSYS_NATIVE_COMPONENT_CAPACITY];
    char signature[256];
    uint64_t failed_generation = 0u;
    uint64_t generation = 0u;
    int written;
    if (
        shell == NULL || payload == NULL ||
        msys_mipc_json_get_string(
            payload, "schema", schema, sizeof(schema), NULL
        ) != MSYS_MIPC_OK ||
        strcmp(schema, "msys.display-output-recovered.v1") != 0 ||
        msys_mipc_json_get_string(
            payload, "fault", fault, sizeof(fault), NULL
        ) != MSYS_MIPC_OK ||
        strcmp(fault, "display-session-lost") != 0 ||
        payload_is_false(payload, "session_preserved") == 0 ||
        payload_is_false(payload, "applications_reopened") == 0 ||
        msys_mipc_json_get_string(
            payload, "provider", provider, sizeof(provider), NULL
        ) != MSYS_MIPC_OK ||
        msys_mipc_json_get_u64(
            payload, "failed_generation", &failed_generation
        ) != MSYS_MIPC_OK ||
        msys_mipc_json_get_u64(payload, "generation", &generation) != MSYS_MIPC_OK
    ) {
        return 0;
    }
    written = snprintf(
        signature,
        sizeof(signature),
        "%s:%llu:%llu",
        provider,
        (unsigned long long)failed_generation,
        (unsigned long long)generation
    );
    if (
        written < 0 || (size_t)written >= sizeof(signature) ||
        strcmp(signature, shell->last_display_recovery) == 0
    ) {
        return 0;
    }
    memcpy(shell->last_display_recovery, signature, (size_t)written + 1u);
    return 1;
}

static void handle_reply(native_shell *shell, const char *packet, const char *type)
{
    uint64_t response_id = 0u;
    pending_call *owned;
    pending_call pending;
    char payload[RESPONSE_CAPACITY];
    int success = strcmp(type, "return") == 0;
    if (
        msys_mipc_json_get_u64(packet, "id", &response_id) != MSYS_MIPC_OK
    ) {
        return;
    }
    owned = pending_find(shell, response_id);
    if (owned == NULL) {
        return;
    }
    pending = *owned;
    memset(owned, 0, sizeof(*owned));
    if (success == 0 || packet_payload(packet, payload, sizeof(payload)) == 0) {
        if (pending.kind == PENDING_APPS_LIST) {
            shell->apps_state = CATALOG_ERROR;
            shell->app_count = 0u;
            packet_error_text(packet, shell->apps_message, sizeof(shell->apps_message));
            draw_launcher(shell);
            if (shell->apps_refresh_queued != 0) {
                shell->apps_refresh_queued = 0;
                request_apps(shell);
            }
        } else if (pending.kind == PENDING_RECENTS_LIST) {
            shell->tasks_state = CATALOG_ERROR;
            shell->task_count = 0u;
            packet_error_text(packet, shell->tasks_message, sizeof(shell->tasks_message));
            refresh_recents_presentation(shell);
            if (shell->tasks_refresh_queued != 0) {
                shell->tasks_refresh_queued = 0;
                request_recents(shell);
            }
        } else if (pending.kind == PENDING_APP_START) {
            hide_launch_transition(shell);
            packet_error_text(packet, shell->apps_message, sizeof(shell->apps_message));
            show_toast(shell, shell->apps_message);
        } else if (
            pending.kind == PENDING_TASK_ACTIVATE ||
            pending.kind == PENDING_TASK_CLOSE
        ) {
            packet_error_text(packet, shell->tasks_message, sizeof(shell->tasks_message));
            if (shell->recents_visible != 0) {
                draw_recents(shell);
            }
        } else if (pending.kind == PENDING_NAVIGATION) {
            shell->nav_feedback = -1;
            shell->nav_feedback_until_ms = now_ms() + NAV_FEEDBACK_MS;
            draw_navigation_action_damage(shell, shell->nav_feedback_action);
        } else if (pending.kind == PENDING_NOTIFICATION_CENTER) {
            show_toast(shell, tr(shell, "error.notification_center_unavailable"));
        } else if (pending.kind == PENDING_SETTINGS_PANEL) {
            show_toast(shell, tr(shell, "error.settings_unavailable"));
        } else if (pending.kind == PENDING_AUDIO_STATE) {
            audio_mark_unavailable(shell, "provider-unavailable");
            if (shell->controls_visible != 0) {
                redraw_controls_row(shell, AUDIO_CONTROL_ROW);
            }
        } else if (
            pending.kind == PENDING_AUDIO_VOLUME ||
            pending.kind == PENDING_AUDIO_MUTE
        ) {
            show_toast(shell, tr(shell, "error.audio_update_failed"));
            request_audio_state(shell);
        }
        return;
    }
    switch (pending.kind) {
    case PENDING_APPS_LIST:
        image_caches_dispose(shell->app_icons, MSYS_NATIVE_MAX_APPS);
        if (
            msys_native_parse_apps(
                payload,
                shell->apps,
                MSYS_NATIVE_MAX_APPS,
                &shell->app_count
            ) == 0
        ) {
            shell->apps_state = CATALOG_ERROR;
            shell->app_count = 0u;
            (void)snprintf(shell->apps_message, sizeof(shell->apps_message), "%s", tr(shell, "error.invalid_app_list"));
        } else {
            shell->apps_state = shell->app_count == 0u ? CATALOG_EMPTY : CATALOG_READY;
            shell->apps_message[0] = '\0';
            sort_apps(shell);
        }
        draw_launcher(shell);
        if (shell->recents_mapped != 0) {
            draw_recents(shell);
        }
        if (shell->apps_refresh_queued != 0) {
            shell->apps_refresh_queued = 0;
            request_apps(shell);
        }
        break;
    case PENDING_APP_START:
        hide_launch_transition(shell);
        if (payload_has_field(payload, "activation_error") != 0) {
            (void)snprintf(shell->apps_message, sizeof(shell->apps_message), "%s", tr(shell, "error.app_activation_failed"));
            show_toast(shell, shell->apps_message);
        } else if (pending.index < shell->app_count) {
            (void)snprintf(
                shell->apps_message,
                sizeof(shell->apps_message),
                "%s %s",
                tr(shell, "launcher.started"),
                shell->apps[pending.index].name
            );
        }
        break;
    case PENDING_RECENTS_LIST:
        image_caches_dispose(shell->task_previews, MSYS_NATIVE_MAX_TASKS);
        if (
            msys_native_parse_tasks(
                payload,
                shell->tasks,
                MSYS_NATIVE_MAX_TASKS,
                &shell->task_count
            ) == 0
        ) {
            shell->tasks_state = CATALOG_ERROR;
            shell->task_count = 0u;
            (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.invalid_window_list"));
        } else {
            shell->tasks_state = shell->task_count == 0u ? CATALOG_EMPTY : CATALOG_READY;
            shell->tasks_message[0] = '\0';
        }
        shell->recents_horizontal_drag = 0;
        shell->recents_drag_offset = 0;
        shell->recents_pressed = -1;
        shell->recents_pulse = -1;
        shell->recents_pulse_until_ms = 0u;
        refresh_recents_presentation(shell);
        if (shell->tasks_refresh_queued != 0) {
            shell->tasks_refresh_queued = 0;
            request_recents(shell);
        }
        break;
    case PENDING_TASK_ACTIVATE:
        if (payload_is_false(payload, "ok") != 0 || payload_has_field(payload, "activation_error") != 0) {
            (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.activation_failed"));
            if (shell->recents_visible != 0) {
                draw_recents(shell);
            }
        } else {
            hide_overlays(shell, 0);
        }
        break;
    case PENDING_TASK_CLOSE:
        if (payload_is_false(payload, "ok") != 0) {
            (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.close_failed"));
            if (shell->recents_visible != 0) {
                draw_recents(shell);
            }
        } else {
            request_recents(shell);
        }
        break;
    case PENDING_NAVIGATION:
        shell->nav_feedback = payload_is_false(payload, "ok") != 0 ? -1 : 1;
        shell->nav_feedback_until_ms = now_ms() + NAV_FEEDBACK_MS;
        draw_navigation_action_damage(shell, shell->nav_feedback_action);
        break;
    case PENDING_NOTIFICATION_CENTER:
        break;
    case PENDING_SETTINGS_PANEL:
        break;
    case PENDING_AUDIO_STATE:
    case PENDING_AUDIO_VOLUME:
    case PENDING_AUDIO_MUTE:
        if (apply_audio_state(shell, payload) == 0) {
            audio_mark_unavailable(shell, "invalid-state");
            if (pending.kind != PENDING_AUDIO_STATE) {
                show_toast(shell, tr(shell, "error.audio_update_failed"));
            }
        }
        if (shell->controls_visible != 0) {
            redraw_controls_row(shell, AUDIO_CONTROL_ROW);
        }
        break;
    case PENDING_NONE:
    default:
        break;
    }
}

static const char *installed_app_name(
    native_shell *shell,
    const char *component
)
{
    size_t index;
    if (component == NULL || *component == '\0') return NULL;
    for (index = 0u; index < shell->app_count; index++) {
        if (strcmp(shell->apps[index].component, component) == 0) {
            return shell->apps[index].name;
        }
    }
    return NULL;
}

static void handle_event(native_shell *shell, const char *packet)
{
    char topic[128];
    char payload[RESPONSE_CAPACITY];
    char message[192];
    if (
        msys_mipc_json_get_string(packet, "topic", topic, sizeof(topic), NULL) != MSYS_MIPC_OK
    ) {
        return;
    }
    if (
        strcmp(topic, "msys.role.notification-presenter") == 0 &&
        payload_message(packet, payload, sizeof(payload), message, sizeof(message)) != 0
    ) {
        show_toast(shell, message);
    } else if (strcmp(topic, "msys.display.output_recovered") == 0) {
        (void)payload_message(packet, payload, sizeof(payload), message, sizeof(message));
        if (display_recovery_requires_notice(shell, payload) != 0) {
            show_toast(shell, tr(shell, "warning.display_session_rebuilt"));
        }
    } else if (strcmp(topic, "msys.install.package_changed") == 0) {
        if (pending_has_kind(shell, PENDING_APPS_LIST) != 0) {
            shell->apps_refresh_queued = 1;
        } else {
            request_apps(shell);
        }
    } else if (strcmp(topic, "msys.lifecycle.transition") == 0) {
        char phase[32];
        char component[MSYS_NATIVE_COMPONENT_CAPACITY];
        char lifecycle_message[192];
        const char *app_name;
        size_t index;
        int tracked = 0;

        if (
            packet_payload(packet, payload, sizeof(payload)) == 0 ||
            msys_mipc_json_get_string(
                payload, "phase", phase, sizeof(phase), NULL
            ) != MSYS_MIPC_OK ||
            msys_mipc_json_get_string(
                payload, "component", component, sizeof(component), NULL
            ) != MSYS_MIPC_OK
        ) {
            return;
        }
        if (
            shell->launch_transition_visible != 0 &&
            strcmp(shell->launch_transition_component, component) == 0 &&
            (
                strcmp(phase, "ready") == 0 ||
                strcmp(phase, "visible") == 0 ||
                strcmp(phase, "closed") == 0 ||
                strcmp(phase, "failed") == 0
            )
        ) {
            hide_launch_transition(shell);
        }
        if (strcmp(phase, "closed") != 0 && strcmp(phase, "failed") != 0) {
            return;
        }
        for (index = 0u; index < shell->task_count; index++) {
            if (strcmp(shell->tasks[index].component, component) == 0) {
                tracked = 1;
                break;
            }
        }
        if (shell->recents_visible != 0 && tracked != 0) {
            request_recents(shell);
            return;
        }
        app_name = installed_app_name(shell, component);
        if (app_name != NULL) {
            (void)snprintf(
                lifecycle_message,
                sizeof(lifecycle_message),
                "%s · %s",
                app_name,
                tr(
                    shell,
                    strcmp(phase, "failed") == 0
                        ? "lifecycle.failed" : "lifecycle.closed"
                )
            );
            show_toast_for(
                shell, lifecycle_message, EXIT_TOAST_VISIBLE_MS
            );
        }
    }
}

static void handle_ipc(native_shell *shell, char *packet)
{
    char type[32];
    int result = msys_mipc_recv_json(
        &shell->ipc,
        packet,
        MSYS_MIPC_RECV_CAPACITY,
        0,
        NULL
    );
    if (result == MSYS_MIPC_EOF) {
        shell->running = 0;
        return;
    }
    if (result != MSYS_MIPC_OK) {
        return;
    }
    if (
        msys_mipc_json_get_string(packet, "type", type, sizeof(type), NULL) != MSYS_MIPC_OK
    ) {
        return;
    }
    if (strcmp(type, "shutdown") == 0) {
        shell->running = 0;
    } else if (strcmp(type, "call") == 0) {
        handle_call(shell, packet);
    } else if (strcmp(type, "event") == 0) {
        handle_event(shell, packet);
    } else if (strcmp(type, "return") == 0 || strcmp(type, "error") == 0) {
        handle_reply(shell, packet, type);
    }
}

static int surface_size_changed(native_shell *shell, Window window, int width, int height)
{
    int *stored_width = NULL;
    int *stored_height = NULL;
    if (window == shell->launcher) {
        stored_width = &shell->launcher_width;
        stored_height = &shell->launcher_height;
    } else if (window == shell->chrome) {
        stored_width = &shell->chrome_width;
        stored_height = &shell->chrome_height;
    } else if (window == shell->navigation) {
        stored_width = &shell->navigation_width;
        stored_height = &shell->navigation_height;
    } else if (window == shell->recents) {
        stored_width = &shell->recents_width;
        stored_height = &shell->recents_height;
    } else if (window == shell->controls) {
        stored_width = &shell->controls_width;
        stored_height = &shell->controls_height;
    } else if (window == shell->launch_transition) {
        stored_width = &shell->launch_transition_width;
        stored_height = &shell->launch_transition_height;
    }
    if (stored_width == NULL || stored_height == NULL) return 0;
    if (*stored_width == width && *stored_height == height) return 0;
    *stored_width = width;
    *stored_height = height;
    return 1;
}

static void draw_navigation_action_damage(
    native_shell *shell,
    enum msys_native_navigation_action action
)
{
    XWindowAttributes attributes;
    int vertical;
    if (!XGetWindowAttributes(shell->display, shell->navigation, &attributes)) return;
    vertical = attributes.height > attributes.width * 2;
    if (shell->buttons_mode != 0 && action != MSYS_NATIVE_NAV_NONE) {
        int slot = action == MSYS_NATIVE_NAV_BACK
            ? 0 : (action == MSYS_NATIVE_NAV_HOME ? 1 : 2);
        int dimension = vertical != 0 ? attributes.height : attributes.width;
        int begin = dimension * slot / 3;
        int end = dimension * (slot + 1) / 3;
        if (vertical != 0) {
            begin_clip(shell, 0, begin, attributes.width, end - begin);
        } else {
            begin_clip(shell, begin, 0, end - begin, attributes.height);
        }
    } else if (vertical != 0) {
        begin_clip(shell, 0, attributes.height / 2 - 60, attributes.width, 120);
    } else {
        begin_clip(shell, attributes.width / 2 - 60, 0, 120, attributes.height);
    }
    draw_navigation(shell);
    end_clip(shell);
}

static void redraw_launcher_cell(native_shell *shell, size_t index)
{
    XWindowAttributes attributes;
    msys_native_grid_layout grid;
    int row;
    int column;
    int x;
    int y;
    if (!XGetWindowAttributes(shell->display, shell->launcher, &attributes)) return;
    launcher_grid(shell, attributes.width, attributes.height, &grid);
    row = (int)index / grid.columns;
    column = (int)index % grid.columns;
    x = grid.margin + column * (grid.cell_width + grid.gap);
    y = grid.top + row * (grid.cell_height + grid.gap) - shell->launcher_scroll;
    begin_clip(shell, x - 2, y - 2, grid.cell_width + 4, grid.cell_height + 4);
    draw_launcher(shell);
    end_clip(shell);
}

static void redraw_launcher_viewport(
    native_shell *shell,
    const XWindowAttributes *attributes,
    const msys_native_grid_layout *grid
)
{
    if (attributes == NULL || grid == NULL) return;
    begin_clip(
        shell,
        0,
        grid->top,
        attributes->width,
        grid->viewport_height
    );
    draw_launcher(shell);
    end_clip(shell);
}

static void redraw_recents_viewport(
    native_shell *shell,
    const XWindowAttributes *attributes,
    const msys_native_recents_layout *layout
)
{
    if (attributes == NULL || layout == NULL) return;
    begin_clip(
        shell,
        0,
        layout->top,
        attributes->width,
        layout->viewport_height
    );
    draw_recents(shell);
    end_clip(shell);
}

static void redraw_recents_damage(
    native_shell *shell,
    const XWindowAttributes *attributes,
    const msys_native_recents_layout *layout,
    int x,
    int y,
    int width,
    int height
)
{
    int previous_clip_active;
    XRectangle previous_clip;
    if (attributes == NULL || layout == NULL) return;
    if (!begin_clip_intersection(
        shell,
        x,
        y < layout->top ? layout->top : y,
        width,
        (y + height > layout->top + layout->viewport_height
            ? layout->top + layout->viewport_height : y + height) -
            (y < layout->top ? layout->top : y),
        &previous_clip_active,
        &previous_clip
    )) return;
    draw_recents(shell);
    restore_clip(shell, previous_clip_active, &previous_clip);
}

static void redraw_recents_card(native_shell *shell, size_t index)
{
    XWindowAttributes attributes;
    msys_native_recents_layout layout;
    int x;
    int y;
    if (
        shell->recents_mapped == 0 || index >= shell->task_count ||
        !XGetWindowAttributes(shell->display, shell->recents, &attributes)
    ) return;
    recents_layout(shell, attributes.width, attributes.height, &layout);
    recents_card_rect(&layout, shell->recents_scroll, index, &x, &y);
    redraw_recents_damage(
        shell, &attributes, &layout,
        x - 3, y - 3, layout.card_width + 6, layout.card_height + 6
    );
}

static void pulse_recents_card(native_shell *shell, size_t index, uint64_t current)
{
    if (index >= shell->task_count) return;
    shell->recents_pulse = (int)index;
    shell->recents_pulse_until_ms = current + TRANSITION_PULSE_MS;
    redraw_recents_card(shell, index);
}

static int controls_hit(native_shell *shell, int x, int y)
{
    XWindowAttributes attributes;
    native_controls_layout layout;
    int top;
    int right;
    int bottom;
    int relative;
    if (!XGetWindowAttributes(shell->display, shell->controls, &attributes)) return -2;
    system_insets(shell, &top, &right, &bottom);
    if (!controls_layout_compute(
        shell, attributes.width, attributes.height, &layout
    )) return -2;
    if (
        x < layout.panel_x || y < top || y >= attributes.height - bottom
    ) return -2;
    relative = y - layout.rows_y;
    if (
        relative >= 0 && relative / layout.row_pitch < CONTROL_ROW_COUNT &&
        relative % layout.row_pitch < layout.row_height &&
        x >= layout.panel_x + 14 &&
        x < layout.panel_x + layout.panel_width - 14
    ) return relative / layout.row_pitch;
    return -1;
}

static int controls_audio_zone(native_shell *shell, int x)
{
    XWindowAttributes attributes;
    native_controls_layout layout;
    int row_x;
    int row_width;
    int zone;
    if (
        !XGetWindowAttributes(shell->display, shell->controls, &attributes) ||
        !controls_layout_compute(
            shell, attributes.width, attributes.height, &layout
        )
    ) return -1;
    row_x = layout.panel_x + 14;
    row_width = layout.panel_width - 28;
    if (x < row_x || x >= row_x + row_width) return -1;
    zone = x - row_x;
    if (zone < row_width / AUDIO_EDGE_ZONE_DIVISOR) return 0;
    if (zone >= row_width - row_width / AUDIO_EDGE_ZONE_DIVISOR) return 2;
    return 1;
}

static void redraw_controls_row(native_shell *shell, int index)
{
    XWindowAttributes attributes;
    native_controls_layout layout;
    if (index < 0 || index >= CONTROL_ROW_COUNT ||
        !XGetWindowAttributes(shell->display, shell->controls, &attributes)) return;
    if (!controls_layout_compute(
        shell, attributes.width, attributes.height, &layout
    )) return;
    begin_clip(
        shell,
        layout.panel_x + 10,
        layout.rows_y - 4 + index * layout.row_pitch,
        layout.panel_width - 20,
        layout.row_pitch
    );
    draw_controls(shell);
    end_clip(shell);
}

static void handle_x_event(native_shell *shell, XEvent *event)
{
    uint64_t current;
    if (event->type == MotionNotify) {
        XEvent latest;
        while (XCheckTypedWindowEvent(
            shell->display, event->xmotion.window, MotionNotify, &latest
        )) {
            *event = latest;
        }
    }
    current = now_ms();
    if (event->type == Expose) {
        begin_clip(
            shell,
            event->xexpose.x,
            event->xexpose.y,
            event->xexpose.width,
            event->xexpose.height
        );
        redraw(shell, event->xexpose.window);
        end_clip(shell);
        return;
    }
    if (event->type == ConfigureNotify) {
        if (event->xconfigure.window == shell->root) {
            if (
                event->xconfigure.width != shell->root_width ||
                event->xconfigure.height != shell->root_height
            ) {
                shell->root_width = event->xconfigure.width;
                shell->root_height = event->xconfigure.height;
                msys_native_layout_compute(
                    &shell->layout, shell->root_width, shell->root_height
                );
                current_profile(shell, shell->root_width, shell->root_height);
                XMoveResizeWindow(
                    shell->display, shell->recents, 0, 0,
                    (unsigned int)shell->root_width, (unsigned int)shell->root_height
                );
                XMoveResizeWindow(
                    shell->display, shell->controls, 0, 0,
                    (unsigned int)shell->root_width, (unsigned int)shell->root_height
                );
                layout_launch_transition(shell);
                {
                    int toast_width = shell->root_width > 20 ? shell->root_width - 20 : 1;
                    int toast_x;
                    if (toast_width > TOAST_MAX_WIDTH) toast_width = TOAST_MAX_WIDTH;
                    toast_x = (shell->root_width - toast_width) / 2;
                    XMoveResizeWindow(
                        shell->display,
                        shell->toast,
                        toast_x,
                        shell->layout.bar_height + 10,
                        (unsigned int)toast_width,
                        76u
                    );
                }
            }
        } else if (surface_size_changed(
            shell,
            event->xconfigure.window,
            event->xconfigure.width,
            event->xconfigure.height
        )) {
            if (
                (event->xconfigure.window != shell->recents || shell->recents_visible != 0) &&
                (event->xconfigure.window != shell->controls || shell->controls_visible != 0) &&
                (
                    event->xconfigure.window != shell->launch_transition ||
                    shell->launch_transition_visible != 0
                )
            ) redraw(shell, event->xconfigure.window);
        }
        return;
    }
    if (
        event->type == ClientMessage &&
        (Atom)event->xclient.data.l[0] == shell->wm_delete
    ) {
        shell->running = 0;
        return;
    }
    if (event->type == ButtonPress && event->xbutton.window == shell->chrome) {
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(shell->display, shell->chrome, &attributes)) return;
        shell->chrome_pressed_action = chrome_action_at(
            event->xbutton.x, attributes.width
        );
        if (shell->chrome_pressed_action != 0) {
            (void)XGrabPointer(
                shell->display,
                shell->chrome,
                False,
                PointerMotionMask | ButtonReleaseMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                None,
                CurrentTime
            );
            draw_chrome_action_damage(shell, shell->chrome_pressed_action);
        }
    } else if (event->type == ButtonPress && event->xbutton.window == shell->navigation) {
        if (shell->nav_feedback != 0) {
            enum msys_native_navigation_action old_feedback = shell->nav_feedback_action;
            shell->nav_feedback = 0;
            draw_navigation_action_damage(shell, old_feedback);
        }
        shell->nav_interaction_until_ms = current + NAV_INTERACTION_MAX_MS;
        if (shell->buttons_mode != 0) {
            XWindowAttributes attributes;
            if (!XGetWindowAttributes(shell->display, shell->navigation, &attributes)) return;
            shell->button_pressed_action = msys_native_button_action_at(
                event->xbutton.x,
                event->xbutton.y,
                attributes.width,
                attributes.height
            );
            (void)XGrabPointer(
                shell->display,
                shell->navigation,
                False,
                PointerMotionMask | ButtonReleaseMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                None,
                CurrentTime
            );
            draw_navigation_action_damage(shell, shell->button_pressed_action);
        } else {
            XWindowAttributes attributes;
            int coordinate = event->xbutton.y_root;
            if (XGetWindowAttributes(shell->display, shell->navigation, &attributes) &&
                attributes.height > attributes.width * 2) {
                coordinate = event->xbutton.x_root;
            }
            msys_native_gesture_begin(&shell->gesture, coordinate, current);
            (void)XGrabPointer(
                shell->display,
                shell->navigation,
                True,
                PointerMotionMask | ButtonReleaseMask,
                GrabModeAsync,
                GrabModeAsync,
                None,
                None,
                CurrentTime
            );
            draw_navigation_action_damage(shell, MSYS_NATIVE_NAV_NONE);
        }
    } else if (event->type == MotionNotify && shell->gesture.active != 0) {
        int coordinate = shell->navigation_vertical != 0
            ? event->xmotion.x_root : event->xmotion.y_root;
        enum msys_native_navigation_action action = msys_native_gesture_motion(
            &shell->gesture,
            coordinate,
            current
        );
        draw_navigation_action_damage(shell, MSYS_NATIVE_NAV_NONE);
        send_navigation(shell, action);
    } else if (event->type == ButtonRelease && shell->gesture.active != 0) {
        int coordinate = shell->navigation_vertical != 0
            ? event->xbutton.x_root : event->xbutton.y_root;
        enum msys_native_navigation_action action = msys_native_gesture_release(
            &shell->gesture,
            coordinate,
            current
        );
        XUngrabPointer(shell->display, CurrentTime);
        shell->gesture.latched = 0;
        shell->nav_interaction_until_ms = 0u;
        draw_navigation_action_damage(shell, MSYS_NATIVE_NAV_NONE);
        send_navigation(shell, action);
    } else if (
        event->type == ButtonRelease && shell->buttons_mode != 0 &&
        shell->button_pressed_action != MSYS_NATIVE_NAV_NONE
    ) {
        XWindowAttributes attributes;
        enum msys_native_navigation_action pressed = shell->button_pressed_action;
        enum msys_native_navigation_action released = MSYS_NATIVE_NAV_NONE;
        int local_x = event->xbutton.x;
        int local_y = event->xbutton.y;
        Window ignored = None;
        if (event->xbutton.window != shell->navigation) {
            (void)XTranslateCoordinates(
                shell->display,
                shell->root,
                shell->navigation,
                event->xbutton.x_root,
                event->xbutton.y_root,
                &local_x,
                &local_y,
                &ignored
            );
        }
        if (XGetWindowAttributes(shell->display, shell->navigation, &attributes)) {
            if (
                local_x >= 0 && local_y >= 0 &&
                local_x < attributes.width && local_y < attributes.height
            ) {
                released = msys_native_button_action_at(
                    local_x,
                    local_y,
                    attributes.width,
                    attributes.height
                );
            }
        }
        if (released != shell->button_pressed_action) released = MSYS_NATIVE_NAV_NONE;
        shell->button_pressed_action = MSYS_NATIVE_NAV_NONE;
        shell->nav_interaction_until_ms = 0u;
        XUngrabPointer(shell->display, CurrentTime);
        if (released == MSYS_NATIVE_NAV_NONE) {
            draw_navigation_action_damage(shell, pressed);
        }
        send_navigation(shell, released);
    } else if (event->type == ButtonPress && event->xbutton.window == shell->recents) {
        XWindowAttributes attributes;
        msys_native_recents_layout layout;
        size_t index;
        int card_x;
        int card_y;
        int top = 0;
        int right = 0;
        int bottom = 0;
        if (!XGetWindowAttributes(shell->display, shell->recents, &attributes)) return;
        recents_layout(shell, attributes.width, attributes.height, &layout);
        recents_surface_insets(
            shell, attributes.width, attributes.height, &top, &right, &bottom
        );
        shell->recents_pointer_active = 1;
        shell->recents_dragging = 0;
        shell->recents_horizontal_drag = 0;
        shell->recents_drag_offset = 0;
        shell->recents_pressed = -1;
        shell->recents_close_pressed = 0;
        shell->recents_drag_start_x = event->xbutton.x;
        shell->recents_drag_start_y = event->xbutton.y;
        shell->recents_drag_start_scroll = shell->recents_scroll;
        (void)XGrabPointer(
            shell->display,
            shell->recents,
            False,
            PointerMotionMask | ButtonReleaseMask,
            GrabModeAsync,
            GrabModeAsync,
            None,
            None,
            CurrentTime
        );
        if (msys_native_recents_exit_hit(
            event->xbutton.x,
            event->xbutton.y,
            attributes.width,
            top,
            right,
            layout.top
        )) {
            shell->recents_pressed = -2;
        } else if (msys_native_recents_hit(
            event->xbutton.x,
            event->xbutton.y,
            shell->recents_scroll,
            &layout,
            shell->task_count,
            &index
        )) {
            shell->recents_pressed = (int)index;
            recents_card_rect(&layout, shell->recents_scroll, index, &card_x, &card_y);
            shell->recents_close_pressed = msys_native_recents_close_hit(
                event->xbutton.x,
                event->xbutton.y,
                card_x,
                card_y,
                &layout
            );
        }
        if (shell->recents_pressed == -2) {
            begin_clip(
                shell,
                attributes.width - right - 78,
                top,
                78,
                layout.top - top
            );
            draw_recents(shell);
            end_clip(shell);
        } else if (shell->recents_pressed >= 0) {
            redraw_recents_card(shell, (size_t)shell->recents_pressed);
        }
        (void)bottom;
    } else if (
        event->type == MotionNotify && event->xmotion.window == shell->recents &&
        shell->recents_pointer_active != 0
    ) {
        XWindowAttributes attributes;
        msys_native_recents_layout layout;
        int delta_x = event->xmotion.x - shell->recents_drag_start_x;
        int delta_y = event->xmotion.y - shell->recents_drag_start_y;
        if (!XGetWindowAttributes(shell->display, shell->recents, &attributes)) return;
        recents_layout(shell, attributes.width, attributes.height, &layout);
        if (
            shell->recents_pressed >= 0 && abs(delta_x) > abs(delta_y) + 6 &&
            abs(delta_x) > 8
        ) {
            int card_x;
            int card_y;
            int old_offset = shell->recents_drag_offset;
            int left;
            int width;
            shell->recents_horizontal_drag = 1;
            shell->recents_dragging = 1;
            if (delta_x > layout.card_width) delta_x = layout.card_width;
            if (delta_x < -layout.card_width) delta_x = -layout.card_width;
            shell->recents_drag_offset = delta_x;
            recents_card_rect(
                &layout, shell->recents_scroll, (size_t)shell->recents_pressed,
                &card_x, &card_y
            );
            left = card_x + (old_offset < delta_x ? old_offset : delta_x) - 3;
            width = layout.card_width + abs(delta_x - old_offset) + 6;
            redraw_recents_damage(
                shell, &attributes, &layout,
                left, card_y - 3, width, layout.card_height + 6
            );
        } else if (shell->recents_horizontal_drag == 0 && abs(delta_y) > 8) {
            int old_scroll = shell->recents_scroll;
            shell->recents_dragging = 1;
            shell->recents_pressed = -1;
            shell->recents_scroll = msys_native_scroll_clamp(
                shell->recents_drag_start_scroll - delta_y,
                layout.content_height,
                layout.viewport_height
            );
            if (shell->recents_scroll != old_scroll) {
                redraw_recents_viewport(shell, &attributes, &layout);
            }
        }
    } else if (
        event->type == ButtonRelease && shell->recents_pointer_active != 0
    ) {
        XWindowAttributes attributes;
        msys_native_recents_layout layout;
        int pressed = shell->recents_pressed;
        int top = 0;
        int right = 0;
        int bottom = 0;
        int local_x = event->xbutton.x;
        int local_y = event->xbutton.y;
        int exit_released;
        int same_card = 0;
        int close_released = 0;
        size_t released_index = 0u;
        Window ignored = None;
        if (event->xbutton.window != shell->recents) {
            if (XTranslateCoordinates(
                shell->display,
                shell->root,
                shell->recents,
                event->xbutton.x_root,
                event->xbutton.y_root,
                &local_x,
                &local_y,
                &ignored
            ) == 0) {
                /* Translation failure is a cancelled release, never a click. */
                local_x = -1;
                local_y = -1;
            }
        }
        XUngrabPointer(shell->display, CurrentTime);
        if (!XGetWindowAttributes(shell->display, shell->recents, &attributes)) {
            shell->recents_pointer_active = 0;
            shell->recents_dragging = 0;
            shell->recents_horizontal_drag = 0;
            shell->recents_drag_offset = 0;
            shell->recents_pressed = -1;
            shell->recents_close_pressed = 0;
            return;
        }
        recents_layout(shell, attributes.width, attributes.height, &layout);
        recents_surface_insets(
            shell, attributes.width, attributes.height, &top, &right, &bottom
        );
        exit_released = msys_native_recents_exit_hit(
            local_x,
            local_y,
            attributes.width,
            top,
            right,
            layout.top
        );
        if (pressed >= 0 && (size_t)pressed < shell->task_count) {
            int card_x;
            int card_y;
            same_card = msys_native_recents_hit(
                local_x,
                local_y,
                shell->recents_scroll,
                &layout,
                shell->task_count,
                &released_index
            ) && released_index == (size_t)pressed;
            recents_card_rect(
                &layout, shell->recents_scroll, (size_t)pressed, &card_x, &card_y
            );
            close_released = msys_native_recents_close_hit(
                local_x,
                local_y,
                card_x,
                card_y,
                &layout
            );
        }
        if (pressed == -2) {
            if (shell->recents_dragging == 0 && exit_released != 0) {
                hide_recents(shell, 1);
            } else {
                shell->recents_pressed = -1;
                draw_recents(shell);
            }
        } else if (
            shell->recents_horizontal_drag != 0 && pressed >= 0 &&
            abs(shell->recents_drag_offset) >= layout.card_width / 3
        ) {
            shell->recents_horizontal_drag = 0;
            shell->recents_drag_offset = 0;
            shell->recents_pressed = -1;
            shell->recents_pulse = pressed;
            shell->recents_pulse_until_ms = current + TRANSITION_PULSE_MS;
            redraw_recents_card(shell, (size_t)pressed);
            close_task(shell, (size_t)pressed);
        } else if (shell->recents_horizontal_drag != 0 && pressed >= 0) {
            shell->recents_drag_offset = 0;
            shell->recents_horizontal_drag = 0;
            shell->recents_pressed = -1;
            redraw_recents_card(shell, (size_t)pressed);
        } else if (shell->recents_dragging != 0) {
            redraw_recents_viewport(shell, &attributes, &layout);
        } else if (pressed >= 0 && (size_t)pressed < shell->task_count) {
            if (shell->recents_close_pressed != 0) {
                if (same_card != 0 && close_released != 0) {
                    shell->recents_pressed = -1;
                    pulse_recents_card(shell, (size_t)pressed, current);
                    close_task(shell, (size_t)pressed);
                } else {
                    shell->recents_pressed = -1;
                    draw_recents(shell);
                }
            } else if (same_card != 0) {
                shell->recents_pressed = -1;
                pulse_recents_card(shell, (size_t)pressed, current);
                activate_task(shell, (size_t)pressed);
            } else {
                shell->recents_pressed = -1;
                draw_recents(shell);
            }
        } else {
            hide_recents(shell, 1);
        }
        (void)bottom;
        shell->recents_pointer_active = 0;
        shell->recents_dragging = 0;
        shell->recents_horizontal_drag = 0;
        shell->recents_drag_offset = 0;
        shell->recents_pressed = -1;
        shell->recents_close_pressed = 0;
    } else if (event->type == ButtonRelease && event->xbutton.window == shell->toast) {
        hide_toast(shell);
    } else if (event->type == ButtonPress && event->xbutton.window == shell->launcher) {
        XWindowAttributes attributes;
        msys_native_grid_layout grid;
        size_t index;
        if (!XGetWindowAttributes(shell->display, shell->launcher, &attributes)) return;
        launcher_grid(shell, attributes.width, attributes.height, &grid);
        shell->launcher_pointer_active = 1;
        shell->launcher_dragging = 0;
        shell->launcher_drag_start_y = event->xbutton.y;
        shell->launcher_drag_start_scroll = shell->launcher_scroll;
        shell->launcher_pressed = -1;
        (void)XGrabPointer(
            shell->display,
            shell->launcher,
            False,
            PointerMotionMask | ButtonReleaseMask,
            GrabModeAsync,
            GrabModeAsync,
            None,
            None,
            CurrentTime
        );
        if (msys_native_grid_hit(
            event->xbutton.x,
            event->xbutton.y,
            shell->launcher_scroll,
            &grid,
            shell->app_count,
            &index
        )) {
            shell->launcher_pressed = (int)index;
            redraw_launcher_cell(shell, index);
        }
    } else if (
        event->type == MotionNotify && event->xmotion.window == shell->launcher &&
        shell->launcher_pointer_active != 0
    ) {
        XWindowAttributes attributes;
        msys_native_grid_layout grid;
        int delta = shell->launcher_drag_start_y - event->xmotion.y;
        if (abs(delta) > 8 && XGetWindowAttributes(shell->display, shell->launcher, &attributes)) {
            if (shell->launcher_dragging == 0 && shell->launcher_pressed >= 0) {
                size_t old_pressed = (size_t)shell->launcher_pressed;
                shell->launcher_pressed = -1;
                redraw_launcher_cell(shell, old_pressed);
            }
            launcher_grid(shell, attributes.width, attributes.height, &grid);
            shell->launcher_dragging = 1;
            {
                int old_scroll = shell->launcher_scroll;
                shell->launcher_scroll = msys_native_scroll_clamp(
                shell->launcher_drag_start_scroll + delta,
                grid.content_height,
                grid.viewport_height
                );
                if (shell->launcher_scroll != old_scroll) {
                    redraw_launcher_viewport(shell, &attributes, &grid);
                }
            }
        }
    } else if (event->type == ButtonRelease && event->xbutton.window == shell->launcher) {
        XWindowAttributes attributes;
        msys_native_grid_layout grid;
        size_t released_index;
        int pressed = shell->launcher_pressed;
        int same = 0;
        XUngrabPointer(shell->display, CurrentTime);
        if (!XGetWindowAttributes(shell->display, shell->launcher, &attributes)) {
            shell->launcher_pressed = -1;
            shell->launcher_pointer_active = 0;
            shell->launcher_dragging = 0;
            return;
        }
        launcher_grid(shell, attributes.width, attributes.height, &grid);
        same = msys_native_grid_hit(
            event->xbutton.x,
            event->xbutton.y,
            shell->launcher_scroll,
            &grid,
            shell->app_count,
            &released_index
        ) && pressed >= 0 && released_index == (size_t)pressed;
        shell->launcher_pressed = -1;
        shell->launcher_pointer_active = 0;
        if (shell->launcher_dragging != 0) {
            shell->launcher_dragging = 0;
            redraw_launcher_viewport(shell, &attributes, &grid);
        } else if (same != 0) {
            shell->launcher_pulse = pressed;
            shell->launcher_pulse_until_ms = current + TRANSITION_PULSE_MS;
            redraw_launcher_cell(shell, (size_t)pressed);
            activate_app(shell, (size_t)pressed);
        }
    } else if (
        event->type == ButtonRelease && shell->chrome_pressed_action != 0
    ) {
        XWindowAttributes attributes;
        Window ignored = None;
        int local_x = event->xbutton.x;
        int local_y = event->xbutton.y;
        int pressed = shell->chrome_pressed_action;
        int released = 0;
        if (event->xbutton.window != shell->chrome) {
            (void)XTranslateCoordinates(
                shell->display,
                shell->root,
                shell->chrome,
                event->xbutton.x_root,
                event->xbutton.y_root,
                &local_x,
                &local_y,
                &ignored
            );
        }
        if (
            XGetWindowAttributes(shell->display, shell->chrome, &attributes) &&
            local_y >= 0 && local_y < attributes.height
        ) {
            released = chrome_action_at(local_x, attributes.width);
        }
        shell->chrome_pressed_action = 0;
        XUngrabPointer(shell->display, CurrentTime);
        draw_chrome_action_damage(shell, pressed);
        if (released == pressed) {
            if (released == 1) show_notification_center(shell);
            else if (released == 2) show_controls(shell);
        }
    } else if (event->type == ButtonPress && event->xbutton.window == shell->controls) {
        shell->controls_pressed = controls_hit(shell, event->xbutton.x, event->xbutton.y);
        shell->controls_pressed_zone = shell->controls_pressed == AUDIO_CONTROL_ROW
            ? controls_audio_zone(shell, event->xbutton.x) : -1;
        redraw_controls_row(shell, shell->controls_pressed);
    } else if (event->type == ButtonRelease && event->xbutton.window == shell->controls) {
        static const char *panels[] = {"wifi", "bluetooth", NULL, "system"};
        int released = controls_hit(shell, event->xbutton.x, event->xbutton.y);
        int released_zone = released == AUDIO_CONTROL_ROW
            ? controls_audio_zone(shell, event->xbutton.x) : -1;
        int pressed = shell->controls_pressed;
        int pressed_zone = shell->controls_pressed_zone;
        shell->controls_pressed = -1;
        shell->controls_pressed_zone = -1;
        if (released == -2) hide_controls(shell);
        else if (
            released >= 0 && released == pressed &&
            (released != AUDIO_CONTROL_ROW || released_zone == pressed_zone)
        ) {
            redraw_controls_row(shell, pressed);
            if (released == AUDIO_CONTROL_ROW) {
                activate_audio_control(shell, released_zone);
                return;
            }
            start_settings_panel(shell, panels[released], (size_t)released);
        } else redraw_controls_row(shell, pressed);
    }
}

static int refresh_chrome_clock(native_shell *shell)
{
    time_t current = time(NULL);
    if (current == (time_t)-1) {
        return 0;
    }
    if (
        shell->chrome_second_valid == 0 ||
        current != shell->chrome_second
    ) {
        draw_chrome_clock_damage(shell);
        return 1;
    }
    return 0;
}

static void periodic(native_shell *shell)
{
    uint64_t current = now_ms();
    enum msys_native_navigation_action action;
    size_t position;
    int visual_change = 0;
    if (shell->gesture.active != 0) {
        action = msys_native_gesture_motion(
            &shell->gesture,
            shell->gesture.current_y,
            current
        );
        send_navigation(shell, action);
    }
    if (
        shell->nav_interaction_until_ms != 0u &&
        current >= shell->nav_interaction_until_ms
    ) {
        action = shell->button_pressed_action;
        shell->gesture.active = 0;
        shell->gesture.latched = 0;
        shell->button_pressed_action = MSYS_NATIVE_NAV_NONE;
        shell->nav_interaction_until_ms = 0u;
        XUngrabPointer(shell->display, CurrentTime);
        draw_navigation_action_damage(shell, action);
        visual_change = 1;
    }
    if (shell->nav_feedback != 0 && current >= shell->nav_feedback_until_ms) {
        action = shell->nav_feedback_action;
        shell->nav_feedback = 0;
        draw_navigation_action_damage(shell, action);
        visual_change = 1;
    }
    if (shell->toast_visible != 0 && current >= shell->toast_until_ms) {
        hide_toast(shell);
        visual_change = 1;
    }
    if (
        shell->toast_visible != 0 &&
        shell->toast_animation_at_ms != 0u &&
        current >= shell->toast_animation_at_ms
    ) {
        int frame_limit = animation_frame_limit(TOAST_ANIMATION_FRAMES);
        if (shell->toast_animation_frame < frame_limit) {
            shell->toast_animation_frame++;
            draw_toast_animation_damage(shell);
            visual_change = 1;
        }
        shell->toast_animation_at_ms =
            shell->toast_animation_frame < frame_limit
                ? current + TOAST_ANIMATION_FRAME_MS : 0u;
    }
    if (
        shell->launch_transition_visible != 0 &&
        current >= shell->launch_transition_until_ms
    ) {
        hide_launch_transition(shell);
        visual_change = 1;
    } else if (
        shell->launch_transition_visible != 0 &&
        shell->launch_transition_animation_at_ms != 0u &&
        current >= shell->launch_transition_animation_at_ms
    ) {
        int frame_limit = animation_frame_limit(LAUNCH_TRANSITION_FRAMES);
        if (shell->launch_transition_frame + 1 < frame_limit) {
            shell->launch_transition_frame++;
            draw_launch_transition(shell);
            visual_change = 1;
        }
        shell->launch_transition_animation_at_ms =
            shell->launch_transition_frame + 1 < frame_limit
                ? current + LAUNCH_TRANSITION_FRAME_MS : 0u;
    }
    if (shell->recents_mapped != 0 && current < shell->overview_accent_until_ms) {
        XWindowAttributes attributes;
        int top = 0;
        int right = 0;
        int bottom = 0;
        if (XGetWindowAttributes(shell->display, shell->recents, &attributes)) {
            recents_surface_insets(
                shell, attributes.width, attributes.height,
                &top, &right, &bottom
            );
            begin_clip(shell, 10, top + 42, 112, 10);
            draw_recents(shell);
            end_clip(shell);
            visual_change = 1;
        }
        (void)right;
        (void)bottom;
    } else if (shell->recents_mapped != 0 && shell->overview_accent_until_ms != 0u) {
        XWindowAttributes attributes;
        int top = 0;
        int right = 0;
        int bottom = 0;
        shell->overview_accent_until_ms = 0u;
        if (XGetWindowAttributes(shell->display, shell->recents, &attributes)) {
            recents_surface_insets(
                shell, attributes.width, attributes.height,
                &top, &right, &bottom
            );
            begin_clip(shell, 10, top + 42, 112, 10);
            draw_recents(shell);
            end_clip(shell);
            visual_change = 1;
        }
        (void)right;
        (void)bottom;
    }
    if (
        shell->launcher_pulse >= 0 &&
        current >= shell->launcher_pulse_until_ms
    ) {
        size_t index = (size_t)shell->launcher_pulse;
        shell->launcher_pulse = -1;
        shell->launcher_pulse_until_ms = 0u;
        if (index < shell->app_count) {
            redraw_launcher_cell(shell, index);
            visual_change = 1;
        }
    }
    if (
        shell->recents_pulse >= 0 &&
        current >= shell->recents_pulse_until_ms
    ) {
        size_t index = (size_t)shell->recents_pulse;
        shell->recents_pulse = -1;
        shell->recents_pulse_until_ms = 0u;
        if (index < shell->task_count && shell->recents_mapped != 0) {
            redraw_recents_card(shell, index);
            visual_change = 1;
        }
    }
    for (position = 0u; position < MAX_PENDING_CALLS; position++) {
        pending_call expired = shell->pending[position];
        if (
            expired.kind == PENDING_NONE ||
            current < expired.deadline_ms
        ) {
            continue;
        }
        memset(&shell->pending[position], 0, sizeof(shell->pending[position]));
        if (expired.kind == PENDING_APPS_LIST || expired.kind == PENDING_APP_START) {
            if (expired.kind == PENDING_APPS_LIST) {
                shell->apps_state = CATALOG_ERROR;
                shell->app_count = 0u;
            }
            (void)snprintf(shell->apps_message, sizeof(shell->apps_message), "%s", tr(shell, "error.request_timeout"));
            if (expired.kind == PENDING_APPS_LIST) draw_launcher(shell);
            else {
                hide_launch_transition(shell);
                show_toast(shell, shell->apps_message);
            }
            visual_change = 1;
            if (
                expired.kind == PENDING_APPS_LIST &&
                shell->apps_refresh_queued != 0
            ) {
                shell->apps_refresh_queued = 0;
                request_apps(shell);
            }
        } else if (
            expired.kind == PENDING_RECENTS_LIST ||
            expired.kind == PENDING_TASK_ACTIVATE ||
            expired.kind == PENDING_TASK_CLOSE
        ) {
            if (expired.kind == PENDING_RECENTS_LIST) {
                shell->tasks_state = CATALOG_ERROR;
                shell->task_count = 0u;
            }
            (void)snprintf(shell->tasks_message, sizeof(shell->tasks_message), "%s", tr(shell, "error.request_timeout"));
            if (expired.kind == PENDING_RECENTS_LIST) {
                refresh_recents_presentation(shell);
                if (shell->recents_visible != 0) visual_change = 1;
                if (shell->tasks_refresh_queued != 0) {
                    shell->tasks_refresh_queued = 0;
                    request_recents(shell);
                }
            } else if (shell->recents_mapped != 0) {
                draw_recents(shell);
                visual_change = 1;
            }
        } else if (expired.kind == PENDING_NAVIGATION) {
            shell->nav_feedback = -1;
            shell->nav_feedback_until_ms = current + NAV_FEEDBACK_MS;
            draw_navigation_action_damage(shell, shell->nav_feedback_action);
            visual_change = 1;
        } else if (expired.kind == PENDING_NOTIFICATION_CENTER) {
            show_toast(shell, tr(shell, "error.notification_center_unavailable"));
            visual_change = 1;
        } else if (expired.kind == PENDING_SETTINGS_PANEL) {
            show_toast(shell, tr(shell, "error.settings_unavailable"));
            visual_change = 1;
        } else if (expired.kind == PENDING_AUDIO_STATE) {
            audio_mark_unavailable(shell, "provider-unavailable");
            if (shell->controls_visible != 0) {
                redraw_controls_row(shell, AUDIO_CONTROL_ROW);
                visual_change = 1;
            }
        } else if (
            expired.kind == PENDING_AUDIO_VOLUME ||
            expired.kind == PENDING_AUDIO_MUTE
        ) {
            show_toast(shell, tr(shell, "error.audio_update_failed"));
            request_audio_state(shell);
            visual_change = 1;
        }
    }
    if (refresh_chrome_clock(shell) != 0) {
        visual_change = 1;
    }
    if (visual_change != 0) {
        XFlush(shell->display);
    }
}

static int event_loop(native_shell *shell)
{
    char *packet = NULL;
    int x_fd = ConnectionNumber(shell->display);
    int ipc_fd = shell->supervised != 0 ? msys_mipc_client_fd(&shell->ipc) : -1;
    if (shell->supervised != 0) {
        packet = (char *)malloc(MSYS_MIPC_RECV_CAPACITY);
        if (packet == NULL) {
            return 0;
        }
    }
    shell->running = 1;
    while (shell->running != 0 && stop_requested == 0) {
        fd_set reads;
        struct timeval timeout;
        int maximum = x_fd;
        int selected;
        int queued_events = XPending(shell->display);
        int handled_x_event = 0;
        while (queued_events > 0) {
            XEvent event;
            XNextEvent(shell->display, &event);
            handle_x_event(shell, &event);
            handled_x_event = 1;
            queued_events = XEventsQueued(shell->display, QueuedAlready);
        }
        if (handled_x_event != 0) {
            XFlush(shell->display);
        }
        FD_ZERO(&reads);
        FD_SET(x_fd, &reads);
        if (ipc_fd >= 0) {
            FD_SET(ipc_fd, &reads);
            maximum = ipc_fd > maximum ? ipc_fd : maximum;
        }
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;
        selected = select(maximum + 1, &reads, NULL, NULL, &timeout);
        if (selected < 0 && errno != EINTR) {
            break;
        }
        if (selected > 0 && ipc_fd >= 0 && FD_ISSET(ipc_fd, &reads)) {
            handle_ipc(shell, packet);
            XFlush(shell->display);
        }
        periodic(shell);
    }
    free(packet);
    return 1;
}

static void shutdown_shell(native_shell *shell)
{
    if (shell->supervised != 0) {
        msys_mipc_client_close(&shell->ipc);
    }
    if (shell->display == NULL) {
        return;
    }
    image_caches_dispose(shell->app_icons, MSYS_NATIVE_MAX_APPS);
    image_caches_dispose(shell->task_previews, MSYS_NATIVE_MAX_TASKS);
    xft_dispose(shell->display, &shell->xft);
    if (shell->font_set != NULL) {
        XFreeFontSet(shell->display, shell->font_set);
    }
    if (shell->fallback_font != NULL) {
        XFreeFont(shell->display, shell->fallback_font);
    }
    XFreeGC(shell->display, shell->gc);
    XCloseDisplay(shell->display);
}

int main(int argc, char **argv)
{
    native_shell shell;
    const char *mode = getenv("MSYS_NATIVE_NAV_MODE");
    enum msys_native_preferences_result preference_result;
    memset(&shell, 0, sizeof(shell));
    shell.launcher_pressed = -1;
    shell.launcher_pulse = -1;
    shell.recents_pressed = -1;
    shell.recents_pulse = -1;
    shell.controls_pressed = -1;
    shell.controls_pressed_zone = -1;
    shell.audio_volume_percent = -1;
    if (msys_i18n_locale_from_environment(shell.locale, sizeof(shell.locale)) != MSYS_I18N_OK) {
        shell.locale[0] = '\0';
    }
    preference_result = msys_native_preferences_load(
        &shell.preferences,
        shell.preferences_path,
        sizeof(shell.preferences_path)
    );
    if (
        preference_result != MSYS_NATIVE_PREFERENCES_OK &&
        preference_result != MSYS_NATIVE_PREFERENCES_NO_STATE_DIR
    ) {
        fprintf(stderr, "msys-shell-native: invalid preference state; using defaults\n");
    }
    shell.next_request_id = 1000u;
    shell.buttons_mode = mode != NULL && strcmp(mode, "buttons") == 0;
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        puts(APP_VERSION);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--buttons") == 0) {
        shell.buttons_mode = 1;
    }
    (void)signal(SIGINT, handle_signal);
    (void)signal(SIGTERM, handle_signal);
    if (initialize_x11(&shell) == 0) {
        shutdown_shell(&shell);
        return 2;
    }
    if (initialize_ipc(&shell) == 0) {
        shutdown_shell(&shell);
        return 3;
    }
    fprintf(
        stderr,
        "msys-shell-native: ready version=%s mode=%s font=%s display=%s\n",
        APP_VERSION,
        shell.buttons_mode != 0 ? "buttons" : "pill",
        xft_ready(&shell) != 0 ? "xft-utf8" : "xlib-fontset",
        DisplayString(shell.display)
    );
    (void)event_loop(&shell);
    shutdown_shell(&shell);
    return 0;
}
