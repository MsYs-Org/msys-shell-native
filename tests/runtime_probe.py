#!/usr/bin/env python3
"""Supervised socketpair/reply-correlation/RSS probe for the native shell."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import re
import time
from pathlib import Path
from typing import Callable


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

        send(parent, {
            "type": "call",
            "id": 42,
            "method": "activate_app",
            "payload": {"component": "org.example.alpha:main"},
        })
        start_request = wait_for(parent, outbound_call("msys.core", "start"))
        if start_request.get("payload") != {"component": "org.example.alpha:main"}:
            raise RuntimeError(f"wrong app start payload: {start_request}")
        wait_for(parent, packet_type("return", 42))
        reply(parent, start_request, {
            "component": "org.example.alpha:main",
            "state": "ready",
            "activation": {"ok": True},
        })

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
        send(parent, {"type": "call", "id": 44, "method": "list_recents", "payload": {}})
        recents = wait_for(parent, packet_type("return", 44))
        tasks = recents.get("payload", {}).get("tasks", [])
        if (
            len(tasks) != 2
            or tasks[0].get("title") != "Alpha document"
            or tasks[0].get("thumbnail") != str(thumbnail)
        ):
            raise RuntimeError(f"recents did not retain real tasks: {recents}")

        if os.environ.get("MSYS_PROBE_EXPECT_MOBILE") == "1":
            expected_frames = {
                "MSYS Chrome": (0, 0, 320, 42),
                "MSYS Launcher": (0, 42, 320, 396),
                "MSYS Recents": (0, 42, 320, 396),
                "MSYS Navigation": (0, 438, 320, 42),
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

        input_mode = os.environ.get("MSYS_PROBE_INPUT_MODE", "").strip()
        hide_request_sent = False
        restore_request: dict | None = None
        if input_mode:
            chrome_width, chrome_height = window_geometry("MSYS Chrome")
            nav_width, nav_height = window_geometry("MSYS Navigation")

            # Back dismisses the local Overview before it reaches an app.
            if input_mode == "buttons":
                debug_input(
                    "--debug-click-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 6),
                    str(nav_height // 2),
                )
            elif input_mode == "pill":
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2),
                    str(nav_height - 6),
                    str(nav_width // 2),
                    str(nav_height - 18),
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
                    str(nav_width * 5 // 6),
                    str(nav_height // 2),
                )
            else:
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2),
                    str(nav_height - 6),
                    str(nav_width // 2),
                    "2",
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
                    str(nav_width * 5 // 6),
                    str(nav_height // 2),
                )
            elif input_mode == "pill":
                debug_input(
                    "--debug-swipe-identity",
                    "org.msys.shell.native.navigation-pill",
                    str(nav_width // 2),
                    str(nav_height - 6),
                    str(nav_width // 2),
                    "2",
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
            debug_input(
                "--debug-click-identity",
                "org.msys.shell.task-switcher",
                "280",
                "240",
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
            "restored": restore_request.get("payload"),
            "closed": stop_request.get("payload"),
            "rss_kib": measured,
            "returncode": process.returncode,
        }, ensure_ascii=False, indent=2))
        return 0 if process.returncode == 0 else 1
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        parent.close()
        thumbnail.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
