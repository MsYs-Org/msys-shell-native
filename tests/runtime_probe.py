#!/usr/bin/env python3
"""Supervised socketpair/reply-correlation/RSS probe for the native shell."""

from __future__ import annotations

import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import re
import time
import ctypes
from pathlib import Path
from typing import Callable


class XSetWindowAttributes(ctypes.Structure):
    _fields_ = [
        ("background_pixmap", ctypes.c_ulong),
        ("background_pixel", ctypes.c_ulong),
        ("border_pixmap", ctypes.c_ulong),
        ("border_pixel", ctypes.c_ulong),
        ("bit_gravity", ctypes.c_int),
        ("win_gravity", ctypes.c_int),
        ("backing_store", ctypes.c_int),
        ("backing_planes", ctypes.c_ulong),
        ("backing_pixel", ctypes.c_ulong),
        ("save_under", ctypes.c_int),
        ("event_mask", ctypes.c_long),
        ("do_not_propagate_mask", ctypes.c_long),
        ("override_redirect", ctypes.c_int),
        ("colormap", ctypes.c_ulong),
        ("cursor", ctypes.c_ulong),
    ]


class X11StackFixture:
    """Own real X11 windows so Overview tests the server stack, not map state."""

    def __init__(self) -> None:
        self.x11 = ctypes.CDLL("libX11.so.6")
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XDefaultScreen.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultScreen.restype = ctypes.c_int
        self.x11.XRootWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XRootWindow.restype = ctypes.c_ulong
        self.x11.XWhitePixel.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XWhitePixel.restype = ctypes.c_ulong
        self.x11.XBlackPixel.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XBlackPixel.restype = ctypes.c_ulong
        self.x11.XCreateSimpleWindow.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_ulong,
            ctypes.c_ulong,
        ]
        self.x11.XCreateSimpleWindow.restype = ctypes.c_ulong
        self.x11.XStoreName.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_char_p,
        ]
        self.x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x11.XInternAtom.restype = ctypes.c_ulong
        self.x11.XChangeProperty.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_int,
        ]
        self.x11.XChangeWindowAttributes.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.POINTER(XSetWindowAttributes),
        ]
        self.x11.XMapRaised.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XDestroyWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XQueryTree.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.x11.XQueryTree.restype = ctypes.c_int
        self.x11.XFree.argtypes = [ctypes.c_void_p]
        self.x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.display = self.x11.XOpenDisplay(os.environ["DISPLAY"].encode("ascii"))
        if not self.display:
            raise RuntimeError("cannot open X display for stacking fixture")
        self.screen = self.x11.XDefaultScreen(self.display)
        self.root = self.x11.XRootWindow(self.display, self.screen)
        self.windows: list[int] = []

    def create(self, title: str, role: str, override_redirect: bool = False) -> int:
        window = int(self.x11.XCreateSimpleWindow(
            self.display,
            self.root,
            0,
            42,
            200,
            160,
            0,
            self.x11.XBlackPixel(self.display, self.screen),
            self.x11.XWhitePixel(self.display, self.screen),
        ))
        if window == 0:
            raise RuntimeError(f"cannot create X11 fixture window {title}")
        self.windows.append(window)
        self.x11.XStoreName(self.display, window, title.encode("utf-8"))
        for name, value in (
            ("_MSYS_WINDOW_ROLE", role),
            ("_MSYS_APP_ID", f"org.msys.probe.{role}"),
            ("_MSYS_COMPONENT_ID", f"org.msys.probe.{role}:main"),
        ):
            atom = self.x11.XInternAtom(self.display, name.encode("ascii"), 0)
            encoded = value.encode("utf-8")
            data = (ctypes.c_ubyte * len(encoded)).from_buffer_copy(encoded)
            self.x11.XChangeProperty(
                self.display, window, atom, 31, 8, 0, data, len(encoded)
            )
        if override_redirect:
            attributes = XSetWindowAttributes()
            attributes.override_redirect = 1
            self.x11.XChangeWindowAttributes(
                self.display, window, 1 << 9, ctypes.byref(attributes)
            )
        self.x11.XMapRaised(self.display, window)
        self.x11.XSync(self.display, 0)
        return window

    def stack(self) -> list[int]:
        root_return = ctypes.c_ulong()
        parent_return = ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_uint()
        if not self.x11.XQueryTree(
            self.display,
            self.root,
            ctypes.byref(root_return),
            ctypes.byref(parent_return),
            ctypes.byref(children),
            ctypes.byref(count),
        ):
            raise RuntimeError("XQueryTree failed")
        try:
            return [int(children[index]) for index in range(count.value)]
        finally:
            if children:
                self.x11.XFree(ctypes.cast(children, ctypes.c_void_p))

    def wait_above(self, upper: int, lower: int, timeout: float = 2) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            stack = self.stack()
            if upper in stack and lower in stack and stack.index(upper) > stack.index(lower):
                return True
            time.sleep(0.02)
        stack = self.stack()
        return upper in stack and lower in stack and stack.index(upper) > stack.index(lower)

    def close(self) -> None:
        if not self.display:
            return
        for window in reversed(self.windows):
            self.x11.XDestroyWindow(self.display, window)
        self.x11.XSync(self.display, 0)
        self.x11.XCloseDisplay(self.display)
        self.display = None
        self.windows.clear()


def receive(channel: socket.socket) -> dict:
    raw = channel.recv(256 * 1024)
    if not raw:
        raise EOFError("native shell closed its component channel")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("native shell sent a non-object packet")
    return value


def wait_for(channel: socket.socket, predicate: Callable[[dict], bool]) -> dict:
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        packet = receive(channel)
        if predicate(packet):
            return packet
    raise TimeoutError("expected component packet was not received")


def assert_no_outbound_call(
    channel: socket.socket,
    target: str,
    method: str,
    timeout: float = 0.25,
) -> None:
    previous_timeout = channel.gettimeout()
    channel.settimeout(timeout)
    try:
        packet = receive(channel)
    except socket.timeout:
        return
    finally:
        channel.settimeout(previous_timeout)
    if outbound_call(target, method)(packet):
        raise RuntimeError(f"cancelled release still called {target}.{method}: {packet}")
    raise RuntimeError(f"unexpected packet during cancelled release: {packet}")


def packet_type(name: str, request_id: int | None = None) -> Callable[[dict], bool]:
    return lambda packet: packet.get("type") == name and (
        request_id is None or packet.get("id") == request_id
    )


def outbound_call(target: str, method: str) -> Callable[[dict], bool]:
    return lambda packet: (
        packet.get("type") == "call"
        and packet.get("target") == target
        and packet.get("method") == method
    )


def send(channel: socket.socket, packet: dict) -> None:
    channel.send(json.dumps(packet, separators=(",", ":")).encode("utf-8"))


def reply(channel: socket.socket, request: dict, payload: dict) -> None:
    send(channel, {"type": "return", "id": request["id"], "payload": payload})


def rss_kib(pid: int) -> int:
    for line in Path(f"/proc/{pid}/status").read_text(encoding="ascii").splitlines():
        if line.startswith("VmRSS:"):
            return int(line.split()[1])
    raise RuntimeError("VmRSS is missing")


def debug_input(*arguments: str) -> None:
    binary = os.environ.get("MSYS_X11_POLICY_DEBUG", "")
    if not binary:
        raise RuntimeError("MSYS_X11_POLICY_DEBUG is required for input smoke")
    subprocess.run([binary, *arguments], check=True, env=os.environ)


def debug_overlay_click(title: str, x: int, y: int) -> None:
    """Click an override-redirect shell overlay through the XTEST runtime."""
    frame_x, frame_y, width, height = window_frame(title)
    if not 0 <= x < width or not 0 <= y < height:
        raise RuntimeError(f"overlay click outside {title}: {(x, y, width, height)}")
    x11 = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeMotionEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_ulong,
    ]
    xtst.XTestFakeButtonEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint,
        ctypes.c_int,
        ctypes.c_ulong,
    ]
    display = x11.XOpenDisplay(os.environ["DISPLAY"].encode("ascii"))
    if not display:
        raise RuntimeError("cannot open X display for overlay click")
    try:
        if not xtst.XTestFakeMotionEvent(display, -1, frame_x + x, frame_y + y, 0):
            raise RuntimeError("XTestFakeMotionEvent failed")
        if not xtst.XTestFakeButtonEvent(display, 1, 1, 0):
            raise RuntimeError("XTest button press failed")
        if not xtst.XTestFakeButtonEvent(display, 1, 0, 0):
            raise RuntimeError("XTest button release failed")
        x11.XFlush(display)
    finally:
        x11.XCloseDisplay(display)


def debug_cross_surface_swipe(
    title: str,
    start_x: int,
    start_y: int,
    end_root_x: int,
    end_root_y: int,
) -> None:
    """Start inside a shell surface and release over a different X11 window."""
    frame_x, frame_y, width, height = window_frame(title)
    if not 0 <= start_x < width or not 0 <= start_y < height:
        raise RuntimeError(
            f"cross-surface swipe starts outside {title}: "
            f"{(start_x, start_y, width, height)}"
        )
    x11 = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeMotionEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_ulong,
    ]
    xtst.XTestFakeButtonEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint,
        ctypes.c_int,
        ctypes.c_ulong,
    ]
    display = x11.XOpenDisplay(os.environ["DISPLAY"].encode("ascii"))
    if not display:
        raise RuntimeError("cannot open X display for cross-surface swipe")
    pressed = False
    try:
        root_start_x = frame_x + start_x
        root_start_y = frame_y + start_y
        if not xtst.XTestFakeMotionEvent(
            display, -1, root_start_x, root_start_y, 0
        ):
            raise RuntimeError("cross-surface initial motion failed")
        if not xtst.XTestFakeButtonEvent(display, 1, 1, 0):
            raise RuntimeError("cross-surface button press failed")
        pressed = True
        x11.XFlush(display)
        for step in range(1, 7):
            x = root_start_x + (end_root_x - root_start_x) * step // 6
            y = root_start_y + (end_root_y - root_start_y) * step // 6
            if not xtst.XTestFakeMotionEvent(display, -1, x, y, 0):
                raise RuntimeError("cross-surface motion failed")
            x11.XFlush(display)
            time.sleep(0.03)
        if not xtst.XTestFakeButtonEvent(display, 1, 0, 0):
            raise RuntimeError("cross-surface button release failed")
        pressed = False
        x11.XFlush(display)
    finally:
        if pressed:
            xtst.XTestFakeButtonEvent(display, 1, 0, 0)
            x11.XFlush(display)
        x11.XCloseDisplay(display)


def window_pixel_digest(title: str) -> bytes:
    result = subprocess.run(
        [
            "xwd",
            "-silent",
            "-display",
            os.environ["DISPLAY"],
            "-id",
            hex(window_id(title)),
        ],
        check=True,
        capture_output=True,
    )
    return hashlib.sha256(result.stdout).digest()


def window_geometry(title: str) -> tuple[int, int]:
    result = subprocess.run(
        ["xwininfo", "-display", os.environ["DISPLAY"], "-name", title],
        check=True,
        text=True,
        capture_output=True,
    )
    width = re.search(r"^  Width: (\d+)$", result.stdout, re.MULTILINE)
    height = re.search(r"^  Height: (\d+)$", result.stdout, re.MULTILINE)
    if width is None or height is None:
        raise RuntimeError(f"cannot read geometry for {title}: {result.stdout}")
    return int(width.group(1)), int(height.group(1))


def window_frame(title: str) -> tuple[int, int, int, int]:
    result = subprocess.run(
        ["xwininfo", "-display", os.environ["DISPLAY"], "-name", title],
        check=True,
        text=True,
        capture_output=True,
    )
    patterns = (
        r"^  Absolute upper-left X:\s+(-?\d+)$",
        r"^  Absolute upper-left Y:\s+(-?\d+)$",
        r"^  Width:\s+(\d+)$",
        r"^  Height:\s+(\d+)$",
    )
    values: list[int] = []
    for pattern in patterns:
        match = re.search(pattern, result.stdout, re.MULTILINE)
        if match is None:
            raise RuntimeError(f"cannot read frame for {title}: {result.stdout}")
        values.append(int(match.group(1)))
    return values[0], values[1], values[2], values[3]


def window_id(title: str) -> int:
    result = subprocess.run(
        ["xwininfo", "-display", os.environ["DISPLAY"], "-name", title],
        check=True,
        text=True,
        capture_output=True,
    )
    match = re.search(r"Window id:\s+(0x[0-9a-fA-F]+)", result.stdout)
    if match is None:
        raise RuntimeError(f"cannot read X11 id for {title}: {result.stdout}")
    return int(match.group(1), 16)


def assert_window_role(title: str, role: str, app_id: str) -> None:
    result = subprocess.run(
        [
            "xprop",
            "-display",
            os.environ["DISPLAY"],
            "-name",
            title,
            "_MSYS_WINDOW_ROLE",
            "_MSYS_APP_ID",
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    if (
        "_MSYS_WINDOW_ROLE" not in result.stdout
        or f'= "{role}"' not in result.stdout
        or "_MSYS_APP_ID" not in result.stdout
        or f'= "{app_id}"' not in result.stdout
    ):
        raise RuntimeError(f"wrong role properties for {title}: {result.stdout}")


def window_is_viewable(title: str) -> bool:
    result = subprocess.run(
        ["xwininfo", "-display", os.environ["DISPLAY"], "-name", title, "-stats"],
        text=True,
        capture_output=True,
    )
    return result.returncode == 0 and "Map State: IsViewable" in result.stdout


def wait_window_viewable(title: str, timeout: float = 2) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if window_is_viewable(title):
            return True
        time.sleep(0.05)
    return window_is_viewable(title)


def wait_window_hidden(title: str, timeout: float = 2) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not window_is_viewable(title):
            return True
        time.sleep(0.05)
    return not window_is_viewable(title)


def main() -> int:
    binary = Path(sys.argv[1] if len(sys.argv) > 1 else "bin/msys-shell-native").resolve()
    parent, child = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    parent.settimeout(8)
    env = dict(os.environ)
    env.update({
        "MSYS_CONTROL_FD": str(child.fileno()),
        "MSYS_COMPONENT_ID": "org.msys.shell.native:desktop-shell",
        "MSYS_GENERATION": "1",
    })
    process = subprocess.Popen([str(binary)], env=env, pass_fds=(child.fileno(),))
    child.close()
    thumbnail_fd, thumbnail_name = tempfile.mkstemp(
        prefix="msys-native-preview-", suffix=".ppm"
    )
    os.close(thumbnail_fd)
    thumbnail = Path(thumbnail_name)
    thumbnail.write_bytes(b"P6\n8 8\n255\n" + bytes((40, 100, 180)) * 64)
    stack_fixture: X11StackFixture | None = None
    try:
        hello = receive(parent)
        if hello.get("type") != "hello":
            raise RuntimeError(f"expected hello, got {hello}")
        send(parent, {
            "type": "welcome",
            "component": "org.msys.shell.native:desktop-shell",
            "generation": 1,
        })
        wait_for(parent, packet_type("ready"))

        # Startup list_apps remains pending while the single reader services an
        # unrelated inbound status call. This proves no synchronous RPC wait.
        app_request = wait_for(parent, outbound_call("msys.core", "list_apps"))
        send(parent, {"type": "call", "id": 40, "method": "status", "payload": {}})
        status = wait_for(parent, packet_type("return", 40))
        # A package event arriving while the first catalog request is pending
        # must be coalesced into one follow-up refresh, not dropped.
        send(parent, {
            "type": "event",
            "topic": "msys.install.package_changed",
            "payload": {"package": "org.example.beta"},
        })
        reply(parent, app_request, {"apps": [
            {
                "id": "org.example.stale:main",
                "name": "Stale",
                "summary": "Superseded inventory",
            },
        ]})
        refresh_apps_request = wait_for(
            parent,
            outbound_call("msys.core", "list_apps"),
        )
        reply(parent, refresh_apps_request, {"apps": [
            {
                "id": "org.example.alpha:main",
                "name": "Alpha",
                "summary": "Real first app",
                "package_root": str(thumbnail.parent),
                "icons": [{"path": thumbnail.name}],
            },
            {
                "id": "org.example.beta:main",
                "name": "Beta",
                "summary": "Real second app",
            },
        ]})

        send(parent, {"type": "call", "id": 41, "method": "list", "payload": {}})
        listed = wait_for(parent, packet_type("return", 41))
        listed_apps = listed.get("payload", {}).get("apps", [])
        if [item.get("component") for item in listed_apps] != [
            "org.example.alpha:main",
            "org.example.beta:main",
        ]:
            raise RuntimeError(f"launcher did not retain authoritative apps: {listed}")
        if listed_apps[0].get("icon") != str(thumbnail):
            raise RuntimeError(f"launcher did not resolve the package icon: {listed}")

        input_mode = os.environ.get("MSYS_PROBE_INPUT_MODE", "").strip()
        if input_mode:
            launcher_width, _launcher_height = window_geometry("MSYS Launcher")
            margin = 14
            gap = 12
            columns = max(1, min(5, (launcher_width - margin * 2 + gap) // 100))
            first_cell_width = (
                launcher_width - margin * 2 - gap * (columns - 1)
            ) // columns
            first_cell_x = margin + first_cell_width // 2
            debug_input(
                "--debug-swipe-identity",
                "org.msys.shell.native.launcher",
                str(first_cell_x),
                "108",
                str(first_cell_x),
                "188",
                "220",
            )
            assert_no_outbound_call(parent, "msys.core", "start")
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.native.launcher",
                str(first_cell_x),
                "108",
            )
        else:
            send(parent, {
                "type": "call",
                "id": 42,
                "method": "activate_app",
                "payload": {"component": "org.example.alpha:main"},
            })
        start_request = wait_for(parent, outbound_call("msys.core", "start"))
        if start_request.get("payload") != {"component": "org.example.alpha:main"}:
            raise RuntimeError(f"wrong app start payload: {start_request}")
        if not wait_window_viewable("MSYS Launch Transition"):
            raise RuntimeError("app activation did not map launch transition immediately")
        assert_window_role(
            "MSYS Launch Transition",
            "animation-mask",
            "org.msys.shell.native.launch-transition",
        )
        transition_width, transition_height = window_geometry(
            "MSYS Launch Transition"
        )
        if transition_width > 236 or transition_height > 132:
            raise RuntimeError(
                f"launch transition is not compact: {(transition_width, transition_height)}"
            )
        if not input_mode:
            wait_for(parent, packet_type("return", 42))
        reply(parent, start_request, {
            "component": "org.example.alpha:main",
            "state": "ready",
            "activation": {"ok": True},
        })
        if not wait_window_hidden("MSYS Launch Transition"):
            raise RuntimeError("ready app left launch transition visible")

        send(parent, {
            "type": "event",
            "topic": "msys.lifecycle.transition",
            "payload": {
                "component": "org.example.alpha:main",
                "phase": "closed",
            },
        })
        send(parent, {"type": "call", "id": 69, "method": "status", "payload": {}})
        wait_for(parent, packet_type("return", 69))
        if not wait_window_viewable("MSYS Notifications"):
            raise RuntimeError("application exit did not show bounded feedback")
        if not wait_window_hidden("MSYS Notifications", timeout=1.8):
            raise RuntimeError("application exit feedback outlived its bound")

        send(parent, {
            "type": "call",
            "id": 70,
            "method": "activate_app",
            "payload": {"component": "org.example.beta:main"},
        })
        failed_start = wait_for(parent, outbound_call("msys.core", "start"))
        wait_for(parent, packet_type("return", 70))
        if not wait_window_viewable("MSYS Launch Transition"):
            raise RuntimeError("second launch transition did not map")
        send(parent, {
            "type": "error",
            "id": failed_start["id"],
            "error": {"code": "START_FAILED", "message": "probe failure"},
        })
        if not wait_window_hidden("MSYS Launch Transition"):
            raise RuntimeError("failed app left launch transition visible")
        if not wait_window_viewable("MSYS Notifications"):
            raise RuntimeError("failed app did not expose bounded error feedback")
        send(parent, {"type": "call", "id": 71, "method": "hide", "payload": {}})
        wait_for(parent, packet_type("return", 71))

        send(parent, {
            "type": "call",
            "id": 72,
            "method": "activate_app",
            "payload": {"component": "org.example.beta:main"},
        })
        late_success = wait_for(parent, outbound_call("msys.core", "start"))
        wait_for(parent, packet_type("return", 72))
        send(parent, {
            "type": "event",
            "topic": "msys.lifecycle.transition",
            "payload": {
                "component": "org.example.beta:main",
                "phase": "failed",
            },
        })
        if not wait_window_hidden("MSYS Launch Transition"):
            raise RuntimeError("failed lifecycle left launch transition visible")
        if not wait_window_viewable("MSYS Notifications"):
            raise RuntimeError("failed lifecycle did not expose bounded feedback")
        reply(parent, late_success, {
            "component": "org.example.beta:main",
            "state": "ready",
            "activation": {"ok": True},
        })
        if not window_is_viewable("MSYS Notifications"):
            raise RuntimeError("late start success suppressed terminal feedback")
        send(parent, {"type": "call", "id": 73, "method": "hide", "payload": {}})
        wait_for(parent, packet_type("return", 73))

        # Keep a real managed application and override-redirect input method in
        # the server tree.  A map-state-only assertion cannot detect Overview
        # being viewable underneath either surface.
        stack_fixture = X11StackFixture()
        application_window = stack_fixture.create(
            "MSYS Probe Application", "application"
        )
        input_method_window = stack_fixture.create(
            "MSYS Probe Input Method", "input-method", override_redirect=True
        )
        if not wait_window_viewable("MSYS Probe Application"):
            raise RuntimeError("stacking fixture application did not map")
        if not wait_window_viewable("MSYS Probe Input Method"):
            raise RuntimeError("stacking fixture input method did not map")

        send(parent, {"type": "call", "id": 43, "method": "show", "payload": {}})
        windows_request = wait_for(
            parent,
            outbound_call("role:window-manager", "list_windows"),
        )
        wait_for(parent, packet_type("return", 43))
        if window_is_viewable("MSYS Recents"):
            raise RuntimeError(
                "Overview mapped before asynchronous window snapshots completed"
            )
        window_snapshot = {
            "schema": "msys.window-list.v1",
            "windows": [
                {
                    "component": "org.example.alpha:main",
                    "id": "msys.x11-window.v1:100:1",
                    "title": "Alpha document",
                    "identity": "org.example.alpha",
                    "kind": "application",
                    "role": "application",
                    "thumbnail": str(thumbnail),
                },
                {
                    "id": "msys.x11-window.v1:200:1",
                    "title": "External terminal",
                    "identity": "terminal",
                },
            ],
        }
        reply(parent, windows_request, window_snapshot)
        if not wait_window_viewable("MSYS Recents"):
            raise RuntimeError("Overview did not map after window snapshots completed")
        recents_window = window_id("MSYS Recents")
        # A foreground application mapped after the asynchronous Overview
        # presentation must still remain below task-switcher.  Map state alone
        # cannot detect this inversion.
        late_application_window = stack_fixture.create(
            "MSYS Probe Late Application", "application"
        )
        if not wait_window_viewable("MSYS Probe Late Application"):
            raise RuntimeError("late stacking fixture application did not map")
        if not stack_fixture.wait_above(recents_window, application_window):
            raise RuntimeError("Overview mapped underneath the foreground application")
        if not stack_fixture.wait_above(recents_window, late_application_window):
            raise RuntimeError("late application mapped above Overview")
        if not stack_fixture.wait_above(recents_window, input_method_window):
            raise RuntimeError("input method remained above Overview")
        stack_fixture.close()
        stack_fixture = None
        send(parent, {"type": "call", "id": 44, "method": "list_recents", "payload": {}})
        recents = wait_for(parent, packet_type("return", 44))
        tasks = recents.get("payload", {}).get("tasks", [])
        if (
            len(tasks) != 2
            or tasks[0].get("title") != "Alpha document"
            or tasks[0].get("thumbnail") != str(thumbnail)
        ):
            raise RuntimeError(f"recents did not retain real tasks: {recents}")

        # Exercise an authoritative multi-application inventory independently
        # of presentation order.  A stale hidden window for Beta precedes its
        # visible replacement to prove component de-duplication and
        # visible/minimized ordering in the native catalog.
        multi_snapshot = {
            "schema": "msys.window-list.v1",
            "windows": [
                {
                    "component": "org.example.beta:main",
                    "id": "msys.x11-window.v1:200:stale",
                    "title": "Beta stale",
                    "kind": "application",
                    "role": "application",
                    "state": "hidden",
                },
                {
                    "component": "org.example.alpha:main",
                    "id": "msys.x11-window.v1:100:1",
                    "title": "Alpha document",
                    "kind": "application",
                    "role": "application",
                    "state": "visible",
                    "thumbnail": str(thumbnail),
                },
                {
                    "component": "org.example.beta:main",
                    "id": "msys.x11-window.v1:200:1",
                    "title": "Beta document",
                    "kind": "application",
                    "role": "application",
                    "state": "visible",
                },
                {
                    "component": "org.example.gamma:main",
                    "id": "msys.x11-window.v1:300:1",
                    "title": "Gamma document",
                    "kind": "application",
                    "role": "application",
                    "state": "minimized",
                },
            ],
        }
        after_beta_close = {
            "schema": "msys.window-list.v1",
            "windows": [multi_snapshot["windows"][1], multi_snapshot["windows"][3]],
        }
        alpha_only = {
            "schema": "msys.window-list.v1",
            "windows": [multi_snapshot["windows"][1]],
        }

        send(parent, {"type": "call", "id": 60, "method": "refresh_recents", "payload": {}})
        multi_request = wait_for(
            parent, outbound_call("role:window-manager", "list_windows")
        )
        wait_for(parent, packet_type("return", 60))
        reply(parent, multi_request, multi_snapshot)
        send(parent, {"type": "call", "id": 61, "method": "list_recents", "payload": {}})
        multi = wait_for(parent, packet_type("return", 61))
        multi_tasks = multi.get("payload", {}).get("tasks", [])
        if [item.get("component") for item in multi_tasks] != [
            "org.example.alpha:main",
            "org.example.beta:main",
            "org.example.gamma:main",
        ]:
            raise RuntimeError(f"multi-task sort/de-duplication failed: {multi}")
        if multi_tasks[1].get("id") != "msys.x11-window.v1:200:1":
            raise RuntimeError(f"visible duplicate was not retained: {multi}")

        if os.environ.get("MSYS_PROBE_INPUT_MODE", "").strip():
            width, height = window_geometry("MSYS Recents")
            chrome_x, chrome_y, chrome_width, chrome_height = window_frame(
                "MSYS Chrome"
            )
            before_cross_surface_release = window_pixel_digest("MSYS Recents")
            debug_cross_surface_swipe(
                "MSYS Recents",
                width // 2,
                max(40, height - 36),
                chrome_x + chrome_width // 2,
                chrome_y + chrome_height // 2,
            )
            time.sleep(0.08)
            if not window_is_viewable("MSYS Recents"):
                raise RuntimeError("multi-task scroll dismissed Overview")
            if window_pixel_digest("MSYS Recents") == before_cross_surface_release:
                raise RuntimeError(
                    "cross-surface Overview release did not present final scroll"
                )

        # Close the middle card by its stable component.  The stop reply is
        # followed by a fresh window inventory and a two-card reflow.
        send(parent, {
            "type": "call",
            "id": 62,
            "method": "close_task",
            "payload": {"component": "org.example.beta:main"},
        })
        beta_stop = wait_for(parent, outbound_call("msys.core", "stop"))
        wait_for(parent, packet_type("return", 62))
        if beta_stop.get("payload") != {"component": "org.example.beta:main"}:
            raise RuntimeError(f"middle task stop targeted the wrong component: {beta_stop}")
        reply(parent, beta_stop, {
            "component": "org.example.beta:main",
            "state": "stopped",
        })
        beta_refresh = wait_for(
            parent, outbound_call("role:window-manager", "list_windows")
        )
        reply(parent, beta_refresh, after_beta_close)
        send(parent, {"type": "call", "id": 63, "method": "list_recents", "payload": {}})
        after_close = wait_for(parent, packet_type("return", 63))
        if [item.get("component") for item in after_close.get("payload", {}).get("tasks", [])] != [
            "org.example.alpha:main",
            "org.example.gamma:main",
        ]:
            raise RuntimeError(f"middle close did not reflow authoritative tasks: {after_close}")

        # Reactivate Gamma from its reordered card inventory.
        send(parent, {
            "type": "call",
            "id": 64,
            "method": "activate_task",
            "payload": {"component": "org.example.gamma:main"},
        })
        gamma_start = wait_for(parent, outbound_call("msys.core", "start"))
        wait_for(parent, packet_type("return", 64))
        if gamma_start.get("payload") != {"component": "org.example.gamma:main"}:
            raise RuntimeError(f"reordered task activation targeted the wrong component: {gamma_start}")
        reply(parent, gamma_start, {
            "component": "org.example.gamma:main",
            "state": "ready",
            "activation": {"ok": True},
        })
        if window_is_viewable("MSYS Recents"):
            raise RuntimeError("successful task activation left Overview visible")

        # Reopen, then emulate an abnormal managed-task exit.  A terminal
        # lifecycle event must trigger one coalesced inventory refresh while
        # keeping Overview available with the surviving task.
        send(parent, {"type": "call", "id": 65, "method": "show", "payload": {}})
        abnormal_open = wait_for(
            parent, outbound_call("role:window-manager", "list_windows")
        )
        wait_for(parent, packet_type("return", 65))
        reply(parent, abnormal_open, after_beta_close)
        if not wait_window_viewable("MSYS Recents"):
            raise RuntimeError("Overview did not reopen before abnormal-exit refresh")
        failed_event = {
            "type": "event",
            "topic": "msys.lifecycle.transition",
            "payload": {
                "phase": "failed",
                "component": "org.example.gamma:main",
                "generation": 8,
            },
        }
        send(parent, failed_event)
        abnormal_refresh = wait_for(
            parent, outbound_call("role:window-manager", "list_windows")
        )
        # A duplicate terminal event while the first inventory request is in
        # flight becomes one bounded follow-up rather than parallel readers.
        send(parent, failed_event)
        reply(parent, abnormal_refresh, alpha_only)
        coalesced_refresh = wait_for(
            parent, outbound_call("role:window-manager", "list_windows")
        )
        reply(parent, coalesced_refresh, alpha_only)
        send(parent, {"type": "call", "id": 66, "method": "list_recents", "payload": {}})
        after_failure = wait_for(parent, packet_type("return", 66))
        if [item.get("component") for item in after_failure.get("payload", {}).get("tasks", [])] != [
            "org.example.alpha:main"
        ]:
            raise RuntimeError(f"abnormal task exit left a stale card: {after_failure}")

        # Restore the two-task fixture used by the navigation and close-hit
        # probes below.
        send(parent, {"type": "call", "id": 67, "method": "refresh_recents", "payload": {}})
        restore_catalog = wait_for(
            parent, outbound_call("role:window-manager", "list_windows")
        )
        wait_for(parent, packet_type("return", 67))
        reply(parent, restore_catalog, window_snapshot)
        send(parent, {"type": "call", "id": 68, "method": "list_recents", "payload": {}})
        restored = wait_for(parent, packet_type("return", 68))
        if len(restored.get("payload", {}).get("tasks", [])) != 2:
            raise RuntimeError(f"navigation fixture was not restored: {restored}")

        expected_mobile = os.environ.get("MSYS_PROBE_EXPECT_MOBILE", "")
        if expected_mobile in {"1", "portrait", "landscape"}:
            landscape = expected_mobile == "landscape"
            expected_frames = {
                "MSYS Chrome": (0, 0, 438, 42) if landscape else (0, 0, 320, 42),
                "MSYS Launcher": (0, 42, 438, 278) if landscape else (0, 42, 320, 396),
                "MSYS Recents": (0, 42, 438, 278) if landscape else (0, 42, 320, 396),
                "MSYS Navigation": (438, 42, 42, 278) if landscape else (0, 438, 320, 42),
            }
            for title, expected in expected_frames.items():
                actual = window_frame(title)
                if actual != expected:
                    raise RuntimeError(
                        f"mobile frame mismatch for {title}: {actual} != {expected}"
                    )
            assert_window_role(
                "MSYS Launcher", "launcher", "org.msys.shell.native.launcher"
            )
            assert_window_role(
                "MSYS Chrome", "system-chrome", "org.msys.shell.native.chrome"
            )
            assert_window_role(
                "MSYS Navigation",
                "navigation-bar",
                "org.msys.shell.native.navigation-pill",
            )
            assert_window_role(
                "MSYS Recents", "task-switcher", "org.msys.shell.task-switcher"
            )

        hold_ms = int(os.environ.get("MSYS_PROBE_HOLD_MS", "0") or 0)
        if 0 < hold_ms <= 10000:
            time.sleep(hold_ms / 1000)

        hide_request_sent = False
        restore_request: dict | None = None
        if input_mode:
            chrome_width, chrome_height = window_geometry("MSYS Chrome")
            nav_width, nav_height = window_geometry("MSYS Navigation")
            nav_vertical = nav_height > nav_width * 2

            # Back dismisses the local Overview before it reaches an app.
            if input_mode == "buttons":
                debug_input(
                    "--debug-click-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2 if nav_vertical else nav_width // 6),
                    str(nav_height // 6 if nav_vertical else nav_height // 2),
                )
            elif input_mode == "pill":
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width - 6 if nav_vertical else nav_width // 2),
                    str(nav_height // 2 if nav_vertical else nav_height - 6),
                    str(nav_width - 18 if nav_vertical else nav_width // 2),
                    str(nav_height // 2 if nav_vertical else nav_height - 18),
                    "180",
                )
            else:
                raise RuntimeError(f"unknown MSYS_PROBE_INPUT_MODE={input_mode!r}")
            restore_request = wait_for(parent, outbound_call("msys.core", "start"))
            reply(parent, restore_request, {
                "component": "org.example.alpha:main",
                "state": "ready",
                "activation": {"ok": True},
            })
            if window_is_viewable("MSYS Recents"):
                raise RuntimeError("Back left Overview visible")

            # Exercise the visible Exit hit target, not only the IPC hide API.
            send(parent, {"type": "call", "id": 47, "method": "show", "payload": {}})
            exit_windows_request = wait_for(
                parent, outbound_call("role:window-manager", "list_windows")
            )
            wait_for(parent, packet_type("return", 47))
            reply(parent, exit_windows_request, window_snapshot)
            if not wait_window_viewable("MSYS Recents"):
                raise RuntimeError("Overview did not map before its Exit action")
            exit_width, _exit_height = window_geometry("MSYS Recents")
            debug_input(
                "--debug-swipe-identity",
                "org.msys.shell.task-switcher",
                str(exit_width - 30),
                "30",
                str(exit_width // 2),
                "30",
                "180",
            )
            if not window_is_viewable("MSYS Recents"):
                raise RuntimeError("Exit press released outside still hid Overview")
            assert_no_outbound_call(parent, "msys.core", "start")
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.task-switcher",
                str(window_geometry("MSYS Recents")[0] - 30),
                "30",
            )
            restore_request = wait_for(parent, outbound_call("msys.core", "start"))
            reply(parent, restore_request, {
                "component": "org.example.alpha:main",
                "state": "ready",
                "activation": {"ok": True},
            })
            if window_is_viewable("MSYS Recents"):
                raise RuntimeError("Overview Exit left the surface visible")

            debug_input(
                "--debug-swipe-identity",
                "org.msys.shell.native.chrome",
                "18",
                str(chrome_height // 2),
                str(chrome_width // 2),
                str(chrome_height // 2),
                "180",
            )
            assert_no_outbound_call(
                parent, "role:notification-center", "show"
            )
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.native.chrome",
                "18",
                str(chrome_height // 2),
            )
            notification_request = wait_for(
                parent, outbound_call("role:notification-center", "show")
            )
            reply(parent, notification_request, {"ok": True})
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.native.chrome",
                str(chrome_width - 18),
                str(chrome_height // 2),
            )
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and not window_is_viewable(
                "MSYS Quick Controls Surface"
            ):
                time.sleep(0.05)
            if not window_is_viewable("MSYS Quick Controls Surface"):
                raise RuntimeError("right chrome area did not open quick controls")

            audio_state = wait_for(
                parent, outbound_call("role:audio-manager", "get_state")
            )
            if audio_state.get("payload") != {}:
                raise RuntimeError(f"wrong audio state payload: {audio_state}")

            def audio_payload(volume: int, muted: bool) -> dict:
                return {
                    "schema": "msys.audio-state.v1",
                    "backend": "bluealsa",
                    "available": True,
                    "reason": None,
                    "output_name": "Probe Headset",
                    "volume_percent": volume,
                    "muted": muted,
                }

            reply(parent, audio_state, audio_payload(63, False))
            controls_width, controls_height = window_geometry(
                "MSYS Quick Controls Surface"
            )
            mobile_orientation = os.environ.get("MSYS_PROBE_EXPECT_MOBILE", "")
            controls_top = 42 if mobile_orientation else 0
            controls_right = 42 if mobile_orientation == "landscape" else 0
            controls_bottom = 42 if mobile_orientation == "portrait" else 0
            rows_y = controls_top + 58
            row_pitch = min(
                66,
                (controls_height - controls_bottom - 30 - rows_y) // 4,
            )
            row_height = row_pitch - (10 if row_pitch >= 60 else 6)
            audio_y = rows_y + 2 * row_pitch + row_height // 2
            audio_row_x = 14
            audio_row_width = controls_width - controls_right - 28
            audio_zone_x = (
                audio_row_x + audio_row_width // 10,
                audio_row_x + audio_row_width // 2,
                audio_row_x + audio_row_width * 9 // 10,
            )

            debug_overlay_click(
                "MSYS Quick Controls Surface", audio_zone_x[0], audio_y
            )
            volume_down = wait_for(
                parent, outbound_call("role:audio-manager", "set_volume")
            )
            if volume_down.get("payload") != {"percent": 53}:
                raise RuntimeError(f"wrong volume-down payload: {volume_down}")
            reply(parent, volume_down, audio_payload(53, False))

            debug_overlay_click(
                "MSYS Quick Controls Surface", audio_zone_x[1], audio_y
            )
            mute = wait_for(
                parent, outbound_call("role:audio-manager", "set_muted")
            )
            if mute.get("payload") != {"muted": True}:
                raise RuntimeError(f"wrong audio mute payload: {mute}")
            reply(parent, mute, audio_payload(53, True))

            debug_overlay_click(
                "MSYS Quick Controls Surface", audio_zone_x[2], audio_y
            )
            volume_up = wait_for(
                parent, outbound_call("role:audio-manager", "set_volume")
            )
            if volume_up.get("payload") != {"percent": 63}:
                raise RuntimeError(f"wrong volume-up payload: {volume_up}")
            reply(parent, volume_up, audio_payload(63, True))

            debug_input(
                "--debug-click-identity",
                "org.msys.shell.native.chrome",
                str(chrome_width - 18),
                str(chrome_height // 2),
            )
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and window_is_viewable(
                "MSYS Quick Controls Surface"
            ):
                time.sleep(0.05)
            if window_is_viewable("MSYS Quick Controls Surface"):
                raise RuntimeError("second right chrome click did not hide quick controls")

            # The centre control is Home in both navigation presentations; a
            # pill tap is deliberately the same typed action as the middle
            # three-button key.
            if input_mode == "buttons":
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2 if nav_vertical else nav_width // 6),
                    str(nav_height // 6 if nav_vertical else nav_height // 2),
                    str(nav_width // 2),
                    str(nav_height // 2),
                    "180",
                )
                assert_no_outbound_call(
                    parent, "role:window-manager", "navigation_action"
                )
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.native.navigation-pill",
                str(nav_width // 2),
                str(nav_height // 2),
            )
            home_request = wait_for(
                parent, outbound_call("role:window-manager", "navigation_action")
            )
            expected_input = "button" if input_mode == "buttons" else "swipe"
            if home_request.get("payload") != {
                "action": "home",
                "input": expected_input,
            }:
                raise RuntimeError(f"wrong Home delegation: {home_request}")
            reply(parent, home_request, {"ok": True, "action": "home"})

            # Apps from a hidden surface must delegate once to the replaceable
            # window manager.  The harness then emulates its typed callback to
            # this selected task-switcher provider.
            if input_mode == "buttons":
                debug_input(
                    "--debug-click-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2 if nav_vertical else nav_width * 5 // 6),
                    str(nav_height * 5 // 6 if nav_vertical else nav_height // 2),
                )
            else:
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width - 6 if nav_vertical else nav_width // 2),
                    str(nav_height // 2 if nav_vertical else nav_height - 6),
                    "2" if nav_vertical else str(nav_width // 2),
                    str(nav_height // 2 if nav_vertical else 2),
                    "520",
                )
            navigation_request = wait_for(
                parent, outbound_call("role:window-manager", "navigation_action")
            )
            if navigation_request.get("payload") != {
                "action": "apps",
                "input": expected_input,
            }:
                raise RuntimeError(f"wrong Apps delegation: {navigation_request}")
            reply(parent, navigation_request, {"ok": True, "action": "apps"})

            send(parent, {"type": "call", "id": 48, "method": "show", "payload": {}})
            second_windows_request = wait_for(
                parent, outbound_call("role:window-manager", "list_windows")
            )
            wait_for(parent, packet_type("return", 48))
            reply(parent, second_windows_request, window_snapshot)
            if not wait_window_viewable("MSYS Recents"):
                raise RuntimeError("Apps callback did not show Overview")

            # Apps again exits an already-visible Overview locally.
            if input_mode == "buttons":
                debug_input(
                    "--debug-click-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2 if nav_vertical else nav_width * 5 // 6),
                    str(nav_height * 5 // 6 if nav_vertical else nav_height // 2),
                )
            elif input_mode == "pill":
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width - 6 if nav_vertical else nav_width // 2),
                    str(nav_height // 2 if nav_vertical else nav_height - 6),
                    "2" if nav_vertical else str(nav_width // 2),
                    str(nav_height // 2 if nav_vertical else 2),
                    "520",
                )
            else:
                raise RuntimeError(f"unknown MSYS_PROBE_INPUT_MODE={input_mode!r}")
        else:
            send(parent, {"type": "call", "id": 46, "method": "hide", "payload": {}})
            hide_request_sent = True

        restore_request = wait_for(parent, outbound_call("msys.core", "start"))
        if restore_request.get("payload") != {"component": "org.example.alpha:main"}:
            raise RuntimeError(f"overview exit did not restore the top task: {restore_request}")
        if hide_request_sent:
            wait_for(parent, packet_type("return", 46))
        reply(parent, restore_request, {
            "component": "org.example.alpha:main",
            "state": "ready",
            "activation": {"ok": True},
        })

        if input_mode:
            # Reopen and press the close affordance inside the first real card.
            send(parent, {"type": "call", "id": 49, "method": "show", "payload": {}})
            close_windows_request = wait_for(
                parent, outbound_call("role:window-manager", "list_windows")
            )
            wait_for(parent, packet_type("return", 49))
            reply(parent, close_windows_request, window_snapshot)
            if not wait_window_viewable("MSYS Recents"):
                raise RuntimeError("Overview did not map before its close action")
            send(parent, {
                "type": "call",
                "id": 50,
                "method": "list_recents",
                "payload": {},
            })
            close_ready = wait_for(parent, packet_type("return", 50))
            if len(close_ready.get("payload", {}).get("tasks", [])) != 2:
                raise RuntimeError(f"task cards were not ready to close: {close_ready}")
            recents_width, recents_height = window_geometry("MSYS Recents")
            recents_margin = 14
            recents_gap = 14
            first_card_width = (
                recents_width - recents_margin * 2 - recents_gap
            ) // 2
            close_x = recents_margin + first_card_width - 28
            close_y = min(240, recents_height - 24)
            debug_input(
                "--debug-swipe-identity",
                "org.msys.shell.task-switcher",
                str(close_x),
                str(close_y),
                str(close_x),
                str(max(64, close_y - 52)),
                "180",
            )
            if not window_is_viewable("MSYS Recents"):
                raise RuntimeError("cancelled close release dismissed Overview")
            assert_no_outbound_call(parent, "msys.core", "stop")
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.task-switcher",
                str(close_x),
                str(close_y),
            )
        else:
            send(parent, {
                "type": "call",
                "id": 45,
                "method": "close_task",
                "payload": {"component": "org.example.alpha:main"},
            })
        stop_request = wait_for(parent, outbound_call("msys.core", "stop"))
        if stop_request.get("payload") != {"component": "org.example.alpha:main"}:
            raise RuntimeError(f"wrong task stop payload: {stop_request}")
        if not input_mode:
            wait_for(parent, packet_type("return", 45))
        reply(parent, stop_request, {
            "component": "org.example.alpha:main",
            "state": "stopped",
        })
        refresh_request = wait_for(
            parent,
            outbound_call("role:window-manager", "list_windows"),
        )
        reply(parent, refresh_request, {
            "schema": "msys.window-list.v1",
            "windows": [],
        })

        # A loose output cable can recover while the :24 X session remains
        # alive.  That path stays quiet.  Only a real display-session rebuild
        # gets one explicit warning, and replaying the same recovery does not
        # remap the toast.
        quiet_recovery = {
            "schema": "msys.display-output-recovered.v1",
            "fault": "output-transport",
            "provider": "org.msys.openstick.ch347:x11-spi-touch-output",
            "failed_generation": 7,
            "generation": 8,
            "display": ":24",
            "session_preserved": True,
            "applications_reopened": False,
        }
        send(parent, {
            "type": "event",
            "topic": "msys.display.output_recovered",
            "payload": quiet_recovery,
        })
        send(parent, {"type": "call", "id": 51, "method": "status", "payload": {}})
        wait_for(parent, packet_type("return", 51))
        if window_is_viewable("MSYS Notifications"):
            raise RuntimeError("session-preserving output recovery showed a toast")

        rebuilt_recovery = dict(quiet_recovery)
        rebuilt_recovery.update({
            "fault": "display-session-lost",
            "session_preserved": False,
            "generation": 9,
        })
        reopened_recovery = dict(rebuilt_recovery)
        reopened_recovery["applications_reopened"] = True
        send(parent, {
            "type": "event",
            "topic": "msys.display.output_recovered",
            "payload": reopened_recovery,
        })
        send(parent, {"type": "call", "id": 52, "method": "status", "payload": {}})
        wait_for(parent, packet_type("return", 52))
        if window_is_viewable("MSYS Notifications"):
            raise RuntimeError("ambiguous application recovery showed a toast")
        send(parent, {
            "type": "event",
            "topic": "msys.display.output_recovered",
            "payload": rebuilt_recovery,
        })
        if not wait_window_viewable("MSYS Notifications"):
            raise RuntimeError("display-session rebuild did not show its warning")
        send(parent, {"type": "call", "id": 53, "method": "hide", "payload": {}})
        wait_for(parent, packet_type("return", 53))
        if window_is_viewable("MSYS Notifications"):
            raise RuntimeError("display recovery warning did not hide")
        send(parent, {
            "type": "event",
            "topic": "msys.display.output_recovered",
            "payload": rebuilt_recovery,
        })
        send(parent, {"type": "call", "id": 54, "method": "status", "payload": {}})
        wait_for(parent, packet_type("return", 54))
        if window_is_viewable("MSYS Notifications"):
            raise RuntimeError("duplicate display recovery remapped the warning")

        # A notification burst may replace the visible message but must not
        # extend the first toast's finite deadline.  This is an X11 mapping
        # probe, so it catches both timer regressions and a forgotten unmap.
        toast_started = time.monotonic()
        for request_id, message in (
            (55, "first bounded toast"),
            (56, "replacement from the same burst"),
            (57, "last replacement from the same burst"),
        ):
            send(parent, {
                "type": "call",
                "id": request_id,
                "method": "show",
                "payload": {"message": message},
            })
            wait_for(parent, packet_type("return", request_id))
        if not wait_window_viewable("MSYS Notifications"):
            raise RuntimeError("notification toast did not map")
        toast_deadline = toast_started + 3.4
        while time.monotonic() < toast_deadline and window_is_viewable(
            "MSYS Notifications"
        ):
            time.sleep(0.05)
        if window_is_viewable("MSYS Notifications"):
            raise RuntimeError("notification burst extended the bounded toast")

        measured = rss_kib(process.pid)
        send(parent, {"type": "shutdown"})
        process.wait(timeout=5)
        print(json.dumps({
            "schema": "msys.native-shell-probe.v2",
            "hello": hello,
            "async_status": status.get("payload"),
            "package_refresh": refresh_apps_request.get("payload"),
            "apps": listed_apps,
            "started": start_request.get("payload"),
            "tasks": tasks,
            "multi_tasks": multi_tasks,
            "after_middle_close": after_close.get("payload", {}).get("tasks", []),
            "after_abnormal_exit": after_failure.get("payload", {}).get("tasks", []),
            "orientation": os.environ.get("MSYS_PROBE_EXPECT_MOBILE", ""),
            "restored": restore_request.get("payload"),
            "closed": stop_request.get("payload"),
            "rss_kib": measured,
            "returncode": process.returncode,
        }, ensure_ascii=False, indent=2))
        return 0 if process.returncode == 0 else 1
    finally:
        if stack_fixture is not None:
            stack_fixture.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        parent.close()
        thumbnail.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
