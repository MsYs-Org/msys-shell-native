from __future__ import annotations

import json
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class NativeShellManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
        cls.component = cls.document["components"][0]
        cls.lvgl_component = cls.document["components"][1]

    def test_default_native_component_owns_only_implemented_phase_two_roles(self) -> None:
        self.assertEqual(self.document["package"]["version"], "0.6.13")
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn('#define APP_VERSION "0.6.13"', implementation)
        self.assertEqual(len(self.document["components"]), 2)
        self.assertEqual(self.component["runtime"], "native")
        self.assertEqual(self.component["lifecycle"], "manual")
        self.assertEqual(self.component["restart"], "never")
        self.assertNotIn("MSYS_NATIVE_NAV_MODE", self.component.get("env", {}))
        self.assertEqual(self.component["readiness"]["mode"], "mipc-ready")
        roles = {
            item["role"]: item["priority"]
            for item in self.component["provides"]
        }
        self.assertEqual(
            roles,
            {
                "launcher": 89,
                "system-chrome": 89,
                "navigation-bar": 89,
                "task-switcher": 89,
                "notification-presenter": 89,
                "notification-center": 89,
            },
        )
        self.assertTrue(all(item["exclusive"] for item in self.component["provides"]))

    def test_lvgl_default_is_one_process_with_dynamic_role_surfaces(self) -> None:
        component = self.lvgl_component
        self.assertEqual(component["id"], "desktop-shell-lvgl")
        self.assertEqual(component["runtime"], "native")
        self.assertEqual(component["lifecycle"], "background")
        self.assertEqual(component["restart"], "on-failure")
        self.assertEqual(
            component["exec"],
            ["@package/files/bin/msys-shell-lvgl", "--output", "spi"],
        )
        self.assertEqual(
            set(component["x-msys-role-windows"]),
            {"launcher", "system-chrome", "navigation-bar", "task-switcher",
             "notification-presenter", "notification-center"},
        )
        self.assertEqual(
            {item["role"] for item in component["provides"]},
            {"launcher", "system-chrome", "navigation-bar", "task-switcher",
             "notification-presenter", "notification-center"},
        )
        self.assertTrue(all(item["priority"] == 100 for item in component["provides"]))
        self.assertTrue(component["x-msys-ui-provider"]["default"])
        self.assertEqual(component["x-msys-ui-provider"]["phase"], "production")
        self.assertEqual(
            component["x-msys-ui-provider"]["fallback_component"],
            "org.msys.shell.native:desktop-shell",
        )
        self.assertIn("mipc.call:role:hal-manager", component["permissions"])
        source = (ROOT / "src" / "lvgl_main.c").read_text(encoding="utf-8")
        self.assertIn("msys_ui_document_load_file", source)
        self.assertIn("msys_ui_document_find", source)
        self.assertIn("msys_native_parse_apps", source)
        self.assertIn("msys_native_parse_tasks", source)
        self.assertIn("msys_native_apply_task_resources", source)
        self.assertIn("msys_native_ppm_sample_resized", source)
        self.assertNotIn("XPutImage", source)
        self.assertNotIn("XClearWindow", source)
        ui_dir = ROOT / "files" / "share" / "ui" / "shell"
        self.assertEqual(
            {path.name for path in ui_dir.glob("*.xml")},
            {"launcher.xml", "chrome.xml", "navigation.xml", "overview.xml",
             "notification.xml", "controls.xml", "toast.xml"},
        )
        self.assertIn("msys_native_notification_append", source)
        self.assertIn("msys_native_parse_wifi_state", source)
        self.assertIn("msys_native_gesture_motion", source)

    def test_lvgl_launcher_async_grid_keeps_nonzero_flex_geometry(self) -> None:
        launcher_path = ROOT / "files" / "share" / "ui" / "shell" / "launcher.xml"
        tree = ET.parse(launcher_path)
        named = {
            element.attrib["name"]: element
            for element in tree.iter()
            if "name" in element.attrib
        }
        grid = named["app_grid"]
        self.assertEqual(grid.attrib["height"], "1")
        self.assertEqual(grid.attrib["flex_grow"], "1")
        self.assertEqual(grid.attrib["scrollable"], "false")
        self.assertIn("launcher_header", named)
        view = tree.getroot().find("view")
        self.assertIsNotNone(view)
        header = list(view)[0]
        title_column = list(header)[0]
        self.assertEqual(title_column.attrib["width"], "1")
        self.assertEqual(title_column.attrib["flex_grow"], "1")
        available_width = 320 - 12 - 12
        page_navigation = named["page_navigation"]
        self.assertEqual(page_navigation.attrib["width"], "112")
        self.assertEqual(page_navigation.attrib["flex_flow"], "row")
        self.assertGreater(available_width - 5 - int(page_navigation.attrib["width"]),
                           150)
        for label_name in ("launcher_title", "launcher_hint"):
            self.assertEqual(named[label_name].attrib["width"], "100%")
            self.assertEqual(named[label_name].attrib["long_mode"], "dot")

        source = (ROOT / "src" / "lvgl_main.c").read_text(encoding="utf-8")
        reply_start = "if(call.kind == PENDING_APPS) {\n        if(msys_native_parse_apps"
        reply = source[source.index(reply_start) :]
        reply = reply[: reply.index("else if(call.kind == PENDING_TASKS)")]
        self.assertLess(reply.index("shell->apps_loaded = 1"),
                        reply.index("render_launcher(shell)"))
        render = source[source.index("static void render_launcher(shell_state *shell)") :]
        render = render[: render.index("static void render_metrics")]
        self.assertIn("msys_native_launcher_layout_reconcile", render)
        self.assertIn("lv_button_create(grid)", render)
        self.assertIn("launcher_resolve_grid_geometry", source)
        self.assertIn('view_object(view, "launcher_root")', source)
        self.assertIn("root = msys_ui_document_root(view->document)", source)
        self.assertIn("lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN)", source)
        self.assertIn("lv_obj_get_parent(grid) != root", source)
        self.assertIn("lv_obj_set_parent(grid, root)", source)
        self.assertIn("lv_obj_set_parent(status, root)", source)
        self.assertIn("lv_obj_move_to_index(header, 0)", source)
        self.assertIn("lv_obj_move_to_index(grid, 1)", source)
        self.assertIn("lv_obj_move_to_index(status, 2)", source)
        self.assertIn('"navigation_root"', source)
        self.assertIn("lv_obj_set_parent(pill_area, navigation_root)", source)
        self.assertIn("lv_obj_set_parent(pill, navigation_root)", source)
        self.assertIn("lv_obj_add_flag(pill, LV_OBJ_FLAG_FLOATING)", source)
        self.assertIn("lv_obj_set_style_bg_opa(pill, LV_OPA_COVER", source)
        self.assertIn("lv_obj_set_flex_grow(grid, 0)", source)
        self.assertIn("launcher geometry apps=%zu", source)
        self.assertIn('"--probe-launcher"', source)
        probe = (ROOT / "tests" / "probe_lvgl_shell.sh").read_text(encoding="utf-8")
        self.assertIn("launcher geometry apps=", probe)
        self.assertIn("launcher tile pixels are not visible", probe)
        self.assertIn("build/x11-pixel-probe", probe)
        self.assertNotIn("xwd", probe)
        pixel_probe = (ROOT / "tests" / "x11_pixel_probe.c").read_text(encoding="utf-8")
        self.assertIn("XGetImage", pixel_probe)
        self.assertIn("XGetPixel", pixel_probe)
        self.assertIn("image->green_mask", pixel_probe)

    def test_phase_two_role_boundary_does_not_claim_missing_contracts(self) -> None:
        source = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("authoritative Core application inventory", source)
        self.assertNotIn("placeholder", source.lower())
        roles = {
            item["role"] for item in self.component["provides"]
        }
        self.assertIn("notification-center", roles)
        self.assertNotIn("transition-presenter", roles)
        self.assertNotIn("chooser", roles)
        claims = {
            provide["role"]: provide.get("x-msys-contract")
            for provide in self.component["provides"]
        }
        self.assertEqual(
            claims["launcher"],
            {"id": "org.msys.role.launcher.v1", "version": "1.0.0"},
        )
        self.assertEqual(
            claims["navigation-bar"],
            {"id": "org.msys.role.navigation-bar.v1", "version": "1.0.0"},
        )
        self.assertIsNone(claims["task-switcher"])
        self.assertIsNone(claims["notification-presenter"])

    def test_manifest_paths_i18n_and_acl_are_explicit(self) -> None:
        self.assertEqual(self.component["exec"], ["@package/bin/msys-shell-native"])
        self.assertEqual(self.component["windowing"]["title"], "MSYS Launcher")
        self.assertEqual(
            self.component["windowing"]["identity"],
            {
                "app_id": "org.msys.shell.native.launcher",
                "x11_wm_class": "org.msys.shell.native.launcher",
                "x11_wm_instance": "msys-shell-native",
            },
        )
        role_windows = self.component["x-msys-role-windows"]
        self.assertEqual(
            set(role_windows),
            {"launcher", "navigation-bar", "notification-center"},
        )
        self.assertEqual(role_windows["launcher"], self.component["windowing"])
        self.assertEqual(role_windows["navigation-bar"]["mode"], "overlay")
        self.assertEqual(role_windows["navigation-bar"]["edge"], "bottom")
        self.assertEqual(role_windows["notification-center"]["mode"], "overlay")
        metadata = self.document["package"]["x-msys-i18n"]
        catalog = json.loads((ROOT / metadata["catalog"]).read_text(encoding="utf-8"))
        self.assertEqual(set(catalog["messages"]), {"en-US", "zh"})
        self.assertEqual(
            set(catalog["messages"]["en-US"]),
            set(catalog["messages"]["zh"]),
        )
        self.assertNotIn("chrome.wifi", catalog["messages"]["en-US"])
        self.assertNotIn("chrome.wifi", catalog["messages"]["zh"])
        self.assertEqual(
            catalog["messages"]["zh"]["package.name"],
            "MSYS 原生桌面",
        )
        self.assertEqual(
            catalog["messages"]["zh"]["package.summary"],
            "带 Xlib 回退的轻量 LVGL/XML 桌面",
        )
        self.assertEqual(
            catalog["messages"]["zh"]["warning.display_session_rebuilt"],
            "显示连接异常，显示会话已重启；应用未自动重开",
        )
        permissions = set(self.component["permissions"])
        self.assertIn("mipc.call:role:window-manager", permissions)
        self.assertNotIn("mipc.call:role:notification-center", permissions)
        self.assertIn("mipc.call:role:audio-manager", permissions)
        self.assertIn("mipc.call:role:hal-manager", permissions)
        self.assertIn("mipc.call:msys.core", permissions)
        self.assertIn(
            "mipc.event:subscribe:msys.install.package_changed",
            permissions,
        )
        self.assertIn(
            "mipc.event:subscribe:msys.display.output_recovered",
            permissions,
        )
        self.assertIn(
            "mipc.event:subscribe:msys.lifecycle.transition",
            permissions,
        )
        self.assertIn("mipc.event:subscribe:msys.hal.changed", permissions)
        self.assertIn("mipc.event:subscribe:msys.notification.post", permissions)
        self.assertIn(
            "mipc.event:subscribe:msys.session.preferences.changed",
            permissions,
        )
        self.assertIn("mipc.event:subscribe:msys.timezone.changed", permissions)
        timezone_implementation = (ROOT / "src" / "main.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'strcmp(topic, "msys.timezone.changed") == 0',
            timezone_implementation,
        )
        self.assertIn("tzset();", timezone_implementation)
        self.assertIn("mipc.event:publish:msys.shell.preferences.changed", permissions)
        banned = ("systemctl", "dbus", "apt-get", "pip install", "openbox")
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8").lower()
        self.assertIn("msys launcher", implementation)
        self.assertIn("org.msys.shell.native.launcher", implementation)
        self.assertIn("msys-shell-native", implementation)
        self.assertIn('"msys.core"', implementation)
        self.assertIn('"list_apps"', implementation)
        self.assertIn('"role:window-manager"', implementation)
        self.assertIn('"list_windows"', implementation)
        self.assertIn("msys.install.package_changed", implementation)
        self.assertIn("msys.display.output_recovered", implementation)
        self.assertIn("msys.display-output-recovered.v1", implementation)
        self.assertIn("applications_reopened", implementation)
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("src/catalog.c", makefile)
        self.assertIn("src/image.c", makefile)
        self.assertIn("generated/shell_catalog.h", makefile)
        for token in banned:
            self.assertNotIn(token, implementation)

    def test_native_chrome_uses_actual_glyph_metrics_and_has_no_idle_flush(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        centered = implementation[
            implementation.index("static void draw_text_centered"):
            implementation.index("static void draw_text_ellipsized")
        ]
        self.assertIn("msys_native_center_baseline(height, glyph_y, glyph_height)", centered)
        self.assertIn("*glyph_y = -(int)extents.y;", implementation)
        self.assertNotIn("surface_height / 2 + 7", centered)
        periodic = implementation[
            implementation.index("static void periodic"):
            implementation.index("static int event_loop")
        ]
        self.assertIn("int visual_change = 0;", periodic)
        self.assertIn(
            "if (visual_change != 0) {\n        XFlush(shell->display);\n    }",
            periodic,
        )
        event_loop = implementation[
            implementation.index("static int event_loop"):
            implementation.index("static void shutdown_shell")
        ]
        self.assertIn("queued_events = XEventsQueued(shell->display, QueuedAlready);", event_loop)
        self.assertNotIn("handle_x_event(shell, &event);\n            XFlush", event_loop)

    def test_antialiased_utf8_font_path_precedes_last_resort_core_font(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        lower = implementation.casefold()
        self.assertIn('dlopen("libXft.so.2"', implementation)
        self.assertIn('"XftDrawStringUtf8"', implementation)
        self.assertIn('"XftDrawSetClip"', implementation)
        self.assertIn('"XftDrawSetClipRectangles"', implementation)
        self.assertIn('"Noto Sans CJK SC:style=Regular:lang=zh-cn', implementation)
        self.assertIn('"Sans:lang=zh-cn', implementation)
        self.assertIn("pixelsize=18", implementation)
        self.assertIn("antialias=true", implementation)
        self.assertIn("hinting=true", implementation)
        self.assertIn("hintstyle=hintslight", implementation)
        self.assertIn("autohint=false", implementation)
        self.assertIn("embeddedbitmap=false", implementation)
        self.assertIn("rgba=none", implementation)
        self.assertNotIn(":size=14", implementation)
        self.assertIn('getenv("MSYS_UI_FONT_FAMILY")', implementation)
        self.assertIn("candidate.char_exists", implementation)
        self.assertIn("Xutf8DrawString", implementation)
        self.assertNotIn("msyscjk", lower)
        self.assertNotIn(".bdf", lower)

        clipped_text = implementation[
            implementation.index("static void draw_text("):
            implementation.index("static void draw_text_centered")
        ]
        self.assertIn("shell->clip_active != 0", clipped_text)
        self.assertIn("draw_set_clip_rectangles", clipped_text)

        fallback = implementation[
            implementation.index("if (xft_initialize(shell) == 0)"):
            implementation.index("shell->background = color")
        ]
        self.assertIn("XCreateFontSet", fallback)
        self.assertIn('XLoadQueryFont(shell->display, "fixed")', fallback)
        self.assertLess(
            fallback.index("XCreateFontSet"),
            fallback.index('XLoadQueryFont(shell->display, "fixed")'),
        )

    def test_adaptive_overview_icons_and_damage_paths_are_native(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn('strftime(output, capacity, "%H:%M:%S"', implementation)
        self.assertIn("draw_chrome_clock_damage", implementation)
        self.assertIn("msys_native_recents_compute", implementation)
        self.assertIn("redraw_launcher_viewport", implementation)
        self.assertIn("redraw_recents_viewport", implementation)
        self.assertNotIn(
            "attributes.width - right - 12,\n                    layout.top",
            implementation,
        )
        self.assertIn('"_MSYS_WINDOW_ROLE"', implementation)
        for role in (
            '"launcher"',
            '"system-chrome"',
            '"navigation-bar"',
            '"task-switcher"',
            '"notification-presenter"',
            '"notification-center"',
        ):
            self.assertIn(role, implementation)
        self.assertIn("draw_cached_image", implementation)
        self.assertIn("static void draw_control_icon", implementation)
        self.assertIn("XDrawArc(shell->display, drawable", implementation)
        show_recents = implementation[
            implementation.index("static void show_recents"):
            implementation.index("static void show_controls")
        ]
        self.assertLess(
            show_recents.index("request_recents(shell)"),
            show_recents.index("present_recents(shell)"),
        )
        self.assertNotIn("XMapRaised", show_recents)
        self.assertIn("recents_mapped", implementation)

    def test_notification_center_is_a_managed_explicit_role_overlay(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        notification_window = implementation[
            implementation.index("shell->notification_center = create_window("):
            implementation.index("shell->controls = create_window(")
        ]
        self.assertIn('"notification-center"', notification_window)
        self.assertIn(
            '"org.msys.shell.native.notification-center"', notification_window
        )

        override_block = implementation[
            implementation.index("XSetWindowAttributes overlay_attributes;"):
            implementation.index("int toast_width =")
        ]
        self.assertNotIn("shell->notification_center", override_block)
        self.assertIn("shell->controls", override_block)
        self.assertIn("shell->launch_transition", override_block)

    def test_finite_transition_and_bus_paced_drag_contract(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn('#define LAUNCH_TRANSITION_FRAMES 4', implementation)
        self.assertIn('#define LAUNCH_TRANSITION_FRAME_MS 90u', implementation)
        self.assertIn('"animation-mask"', implementation)
        self.assertIn('"org.msys.shell.native.launch-transition"', implementation)
        activate = implementation[
            implementation.rindex("static void activate_app"):
            implementation.rindex("static void activate_task")
        ]
        self.assertLess(
            activate.index("show_launch_transition"),
            activate.index("send_async"),
        )
        recents_motion = implementation[
            implementation.index(
                "event->type == MotionNotify && event->xmotion.window == shell->recents"
            ):
            implementation.index(
                "event->type == ButtonRelease && shell->recents_pointer_active != 0"
            )
        ]
        launcher_motion = implementation[
            implementation.index(
                "event->type == MotionNotify && event->xmotion.window == shell->launcher"
            ):
            implementation.index(
                "event->type == ButtonRelease && event->xbutton.window == shell->launcher"
            )
        ]
        self.assertIn("redraw_recents_damage", recents_motion)
        self.assertIn("present_recents_drag_frame(shell, current, 0)", recents_motion)
        self.assertNotIn("redraw_recents_viewport", recents_motion)
        self.assertIn("redraw_launcher_cell", launcher_motion)
        self.assertIn("launcher_edge_at_ms", launcher_motion)
        self.assertNotIn("redraw_launcher_viewport", launcher_motion)
        self.assertIn("#define DRAG_FRAME_MS 80u", implementation)
        recents_press = implementation[
            implementation.index(
                "event->type == ButtonPress && event->xbutton.window == shell->recents"
            ):
            implementation.index(
                "event->type == MotionNotify && event->xmotion.window == shell->recents"
            )
        ]
        launcher_press = implementation[
            implementation.index(
                "event->type == ButtonPress && event->xbutton.window == shell->launcher"
            ):
            implementation.index(
                "event->type == MotionNotify && event->xmotion.window == shell->launcher"
            )
        ]
        self.assertIn("XGrabPointer", recents_press)
        self.assertIn("shell->recents,\n            False", recents_press)
        recents_release = implementation[
            implementation.index(
                "event->type == ButtonRelease && shell->recents_pointer_active != 0"
            ):
            implementation.index(
                "event->type == ButtonRelease && event->xbutton.window == shell->toast"
            )
        ]
        self.assertIn("XTranslateCoordinates", recents_release)
        self.assertIn("event->xbutton.x_root", recents_release)
        self.assertIn("event->xbutton.y_root", recents_release)
        self.assertNotIn("event->xbutton.window != shell->recents", recents_release)
        self.assertNotIn("event->xbutton.x,\n            event->xbutton.y", recents_release)
        self.assertIn("shell->recents_scroll != shell->recents_presented_scroll", recents_release)
        self.assertIn("present_recents_drag_frame(shell, current, 1)", recents_release)
        self.assertIn("XGrabPointer", launcher_press)
        self.assertIn("shell->launcher,\n            False", launcher_press)
        self.assertNotIn("layout.card_width + abs(offset)", implementation)
        launcher_release = implementation[
            implementation.index(
                "event->type == ButtonRelease && event->xbutton.window == shell->launcher"
            ):
            implementation.index("event->type == ButtonRelease && shell->chrome_pressed_action")
        ]
        self.assertIn("redraw_launcher_viewport", launcher_release)
        self.assertIn("begin_atomic_presentation(shell)", launcher_release)
        periodic = implementation[
            implementation.index("static void periodic"):
            implementation.index("static int event_loop")
        ]
        self.assertIn("LAUNCHER_LONG_PRESS_MS", periodic)
        self.assertIn("msys_native_launcher_move", periodic)
        self.assertIn("shell->recents_redraw_pending != 0", periodic)
        self.assertIn("present_recents_drag_frame(shell, current, 0)", periodic)
        runtime_probe = (ROOT / "tests" / "runtime_probe.py").read_text(encoding="utf-8")
        self.assertIn("capture_midpoint=True", runtime_probe)
        self.assertIn("did not visibly follow drag before release", runtime_probe)
        self.assertIn("XGrabServer(shell->display)", implementation)
        self.assertIn("XSync(shell->display, False)", implementation)
        self.assertIn("XUngrabServer(shell->display)", implementation)
        clock_damage = implementation[
            implementation.index("static void draw_chrome_clock_damage"):
            implementation.index("static void draw_chrome_wifi_damage")
        ]
        self.assertIn("chrome_clock_bounds", clock_damage)
        self.assertIn("msys_native_clock_changed_slots", clock_damage)
        self.assertIn("chrome_clock_pixmap", clock_damage)
        self.assertIn("begin_atomic_presentation(shell)", clock_damage)
        self.assertIn("XCopyArea", clock_damage)
        self.assertIn("mask & (1u << slot)", clock_damage)
        self.assertNotIn("draw_chrome(shell)", clock_damage)
        self.assertNotIn("begin_clip", clock_damage)
        self.assertIn("end_atomic_presentation(shell)", clock_damage)
        clock_model = (ROOT / "src" / "clock.c").read_text(encoding="utf-8")
        self.assertIn("MSYS_NATIVE_CLOCK_DIGIT_MASK", clock_model)
        self.assertIn("previous[index] != current[index]", clock_model)
        self.assertIn("clock damaged a fixed colon slot", runtime_probe)
        runtime_script = (ROOT / "tests" / "test_mobile_x11_runtime.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("MSYS_NATIVE_CLOCK_DEBUG=1", runtime_script)
        show_transition = implementation[
            implementation.index("static void show_launch_transition"):
            implementation.index("static void present_recents")
        ]
        self.assertNotIn("draw_launch_transition(shell)", show_transition)
        self.assertIn("chrome_pressed_action", implementation)
        self.assertIn("role:notification-center", implementation)
        self.assertIn("org.msys.settings:main", implementation)
        self.assertIn("event->xexpose.width", implementation)
        self.assertIn("XCheckTypedWindowEvent", implementation)
        self.assertIn("surface_size_changed", implementation)
        self.assertIn("NAV_INTERACTION_MAX_MS", implementation)
        self.assertIn("TRANSITION_PULSE_MS", implementation)
        self.assertIn("static void pulse_recents_card", implementation)
        self.assertIn("static void hide_toast", implementation)
        self.assertIn("must not keep an overlay mapped", implementation)
        self.assertNotIn("chinese_locale", implementation)
        generated = (ROOT / "generated" / "shell_catalog.h").read_text(encoding="utf-8")
        self.assertIn("shell_catalog", generated)

    def test_quick_controls_use_typed_async_audio_role(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn("#define CONTROL_ROW_COUNT 4", implementation)
        self.assertIn("#define AUDIO_STATE_TIMEOUT_MS 12000u", implementation)
        self.assertIn("#define AUDIO_WRITE_TIMEOUT_MS 10000u", implementation)
        self.assertIn("#define AUDIO_EDGE_ZONE_DIVISOR 5", implementation)
        self.assertIn('"role:audio-manager"', implementation)
        self.assertIn('"get_state"', implementation)
        self.assertIn('"set_volume"', implementation)
        self.assertIn('"set_muted"', implementation)
        self.assertIn("PENDING_AUDIO_STATE", implementation)
        self.assertIn("redraw_controls_row(shell, AUDIO_CONTROL_ROW)", implementation)
        self.assertNotIn("systemctl", implementation.lower())

    def test_background_process_page_is_bounded_read_only_and_on_demand(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        catalog = (ROOT / "src" / "catalog.c").read_text(encoding="utf-8")
        model = (ROOT / "src" / "model.c").read_text(encoding="utf-8")
        runtime = (ROOT / "tests" / "runtime_probe.py").read_text(encoding="utf-8")
        self.assertIn("PENDING_PROCESS_LIST", implementation)
        self.assertIn('"list_processes"', implementation)
        self.assertIn(
            '"{\\\"scope\\\":\\\"all-msys\\\",\\\"include_system\\\":%s,\\\"limit\\\":64}"',
            implementation,
        )
        self.assertIn("msys_native_parse_processes", catalog)
        self.assertIn('"msys.process-list.v1"', catalog)
        self.assertIn("MSYS_NATIVE_MAX_PROCESSES", catalog)
        self.assertIn("msys_native_recents_process_hit", model)
        self.assertIn("msys_native_process_checkbox_hit", model)
        self.assertIn("msys_native_process_row_hit", model)
        self.assertIn("present_process_drag_frame", implementation)
        self.assertIn("process_redraw_pending", implementation)
        self.assertNotIn('"kill"', implementation)
        self.assertIn('outbound_call("msys.core", "list_processes")', runtime)
        self.assertIn("process row click dismissed the page", runtime)
        self.assertIn("process list did not follow drag before release", runtime)
        self.assertIn('"scope": "all-msys"', runtime)
        self.assertIn("Touch input method", runtime)
        self.assertIn("system metrics repainted outside its header bounds", runtime)
        self.assertIn("SYSTEM_METRICS_INTERVAL_MS 1000u", implementation)
        self.assertIn("redraw_recents_system_metrics", implementation)

    def test_chrome_wifi_uses_hal_events_and_bounded_icon_damage(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn("#define WIFI_REFRESH_INTERVAL_MS 10000u", implementation)
        self.assertIn('"role:hal-manager"', implementation)
        self.assertIn('"inventory"', implementation)
        self.assertIn('"get_state"', implementation)
        self.assertIn('"msys.hal.changed"', implementation)
        self.assertIn("PENDING_WIFI_INVENTORY", implementation)
        self.assertIn("PENDING_WIFI_STATE", implementation)
        damage = implementation[
            implementation.index("static void draw_chrome_wifi_damage"):
            implementation.index("static void draw_chrome_action_damage")
        ]
        self.assertIn("chrome_wifi_bounds", damage)
        self.assertIn("begin_clip(shell, x, 0, width, attributes.height)", damage)
        self.assertNotIn("XClearWindow", damage)
        self.assertNotIn("/sys/", implementation)

    def test_launcher_actions_use_real_core_and_software_center_contracts(self) -> None:
        implementation = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn("org.msys.settings:software-center", implementation)
        self.assertIn('"msys.core", "activate"', implementation)
        self.assertIn("msys_native_quick_action_payload", implementation)
        self.assertNotIn('"role:install-manager"', implementation)
        self.assertNotIn('"role:settings"', implementation)


if __name__ == "__main__":
    unittest.main()
