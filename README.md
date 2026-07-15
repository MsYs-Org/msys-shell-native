# MSYS Native Shell

Current source version: `0.3.18`.

Version 0.3.18 removes content-viewport painting from Launcher and Recents
drag motion. Motion updates only the 12-pixel scroll-indicator strip; release
atomically commits at most one final viewport. A clamped/no-scroll drag only
clears its pressed cell or card once and performs no viewport redraw at all.
The same bounded X11 presentation batch keeps the existing 112×42 per-second
clock damage from exposing a clear-before-text intermediate frame.

Version 0.3.17 adds a compact HAL-backed Wi-Fi strength icon immediately to
the left of Quick Controls. It reacts to HAL change events, uses a bounded
low-frequency fallback refresh, and damages only its small top-bar rectangle.

Version 0.3.16 gives the compact two-column Recents subtitle to the complete
PSS/RSS value. Runtime state remains available through the task RPC instead of
truncating the memory value on a 320-pixel display.

Version 0.3.15 resolves every Recents pointer release from root coordinates.
This keeps the visible Exit control reliable with release-only touch drivers
whose grabbed event can name the Recents window while carrying stale local
coordinates.

This is a lean adaptive X11 shell, implemented as one C process using Xlib
and the dependency-free JSON mIPC C SDK. This repository supplies the shell
package but does not modify or select any system profile; profile ownership
stays with the integrating MSYS configuration.

## Implemented boundary

One supervised component creates several independently identified X11 windows:

- Material-like icon grid backed by the authoritative Core application inventory,
  with scrolling and package PPM icon declarations;
- local-time system chrome with seconds, a notification-center entry on the
  left, and an adaptive quick-controls entry on the right; a text-free Wi-Fi
  glyph beside quick controls follows `role:hal-manager` state and
  `msys.hal.changed`, with a bounded ten-second asynchronous fallback and
  icon-only X11 damage;
- gesture pill, or three-button navigation with
  `MSYS_NATIVE_NAV_MODE=buttons`;
- responsive Recents backed by the selected window manager's real application
  inventory, a two-card mobile screenshot row, bounded PPM screenshots with
  low-memory antialiased scaling, card activation, close buttons, horizontal
  dismiss, and vertical scrolling;
- bounded notification toast whose deadline cannot be extended by an event burst;
- finite, damage-clipped interaction pulses for launcher cells, Recents cards,
  and the navigation pill (there is no continuous animation timer).

Quick Controls uses dependency-free Xlib line icons for Wi-Fi, Bluetooth,
Bluetooth audio, and Settings. Opening the panel asynchronously reads the
replaceable `audio-manager` role. The audio row shows the selected headset,
live volume/mute state, or a truthful unavailable reason. Its roughly 1/5,
3/5, and 1/5 left/centre/right zones perform -10, mute/unmute, and +10 while
leaving enough width for a headset name or degraded reason. PCM never crosses
mIPC.

The manifest advertises only the roles actually served: `launcher`,
`system-chrome`, `navigation-bar`, `task-switcher`, and
`notification-presenter`. Each has priority 90. An integrating profile should
select the single `org.msys.shell.native:desktop-shell` component for the roles
it wants this process to own and supervise that component once.

Navigation mode remains profile-owned: the regular mobile/desktop profiles
export `MSYS_NATIVE_NAV_MODE=buttons`, while `mobile-spi-pill` exports `pill`.
The manifest does not override that environment; a standalone launch defaults
to the pill.

The component performs the normal inherited-channel `hello` / `welcome` /
`ready` handshake. It asynchronously calls `msys.core.list_apps`, refreshes on
`msys.install.package_changed`, and starts only components present in its
bounded authoritative cache. Launcher `list`/`list_apps` exposes that cache.
Recents asynchronously calls `role:window-manager.list_windows`; managed tasks
are activated/stopped through Core, while external X11 windows are focused or
closed through the window-manager role. Request IDs are correlated by the
single event-loop reader, so inbound calls remain responsive while downstream
requests are pending. The Recents surface remains unmapped until the initial
window-list reply, error, or timeout, leaving the foreground application clear
for a real cold-start thumbnail capture. A second Apps action can cancel that
pending presentation, and a late reply does not remap it. Timeouts and late
replies are bounded and safe.

The top-to-bottom X11 inventory is de-duplicated by managed component (or
stable window ID for external windows). Visible tasks precede minimized and
hidden tasks while window-manager order remains stable inside each state. A
managed task card resolves its component through the localized app catalog;
unmanaged external windows retain their X11 title fallback. A
terminal `closed`/`failed` lifecycle event refreshes a visible Overview through
one coalesced request. Closing a card follows the same path, clamps the scroll
offset after reflow, and clears the union of old/new card pixels inside the
task viewport rather than damaging the system bars.

Launching an app maps one compact internal `animation-mask` surface before the
asynchronous Core call. It reuses the launcher's cached icon and localized app
name, renders at most four 90ms pulse frames, then remains static until Core
reports ready/failure or the bounded request expires. It is not a replaceable
`transition-presenter` role provider. `MSYS_NATIVE_REDUCED_MOTION=1` collapses
finite shell animations to their first frame. Launcher and Overview drags are
release-only for content: motion records the latest logical position and
damages only the narrow scroll-indicator strip. Release atomically damages the
final viewport at most once; a clamped drag performs no viewport damage. This
avoids clear/content intermediate frames and repeated large SPI bounding boxes
during touch movement without changing the CH347 dirty-box implementation.

`get_preferences` and its `status` alias return the versioned launcher
preference state. `set_preferences` strictly merges one or more bounded fields,
and `reset_preferences` restores defaults. Successful mutations advance a
monotonic revision, atomically replace `launcher-preferences.json` below
`MSYS_COMPONENT_STATE_DIR` (or the package-owned `MSYS_APP_STATE_DIR`), update
launcher colour/layout/icon presentation immediately, and publish
`msys.shell.preferences.changed`. Invalid or unpersistable changes never alter
the in-memory state. `show({})`
opens Recents, `show({message})` opens a toast, and `hide({})` closes both.
Unknown methods return `NO_METHOD`; chooser methods return `NOT_IMPLEMENTED`
rather than false success.

The shell also observes the bounded `msys.display.output_recovered` event.
Serial/output reconnects are handled inside the display provider while the
active X display remains alive, so they stay quiet. Only an event proving that
the X display session was lost, rebuilt, and did not automatically reopen
applications produces one localized warning toast. The provider and old/new
generation tuple suppresses duplicate delivery; the shell never starts
applications as part of this notification path.

The legacy component-level manifest identity names the actual launcher X11 window
(`org.msys.shell.native.launcher` / `msys-shell-native`). This lets Core's
generic Home/launcher activation find the window without depending on a
package-specific title. `x-msys-role-windows` additionally binds the launcher
and navigation role contracts to their separate, explicit X11 surfaces.

Shell layout is resolved independently from the component implementation.
`MSYS_LAYOUT_PROFILE=embedded|mobile|desktop` (with `kiosk` mapped to embedded)
selects density and card/grid metrics; the real navigation window aspect picks
bottom-horizontal or right-vertical controls after rotation. The persisted
launcher layout preference may override the profile without coupling it to an
SPI or HDMI provider.

The pill follows inward motion, changes colour, sends typed Back for a short
swipe, Home for a tap, and Apps after at least 28 pixels held for 420 ms. Apps
is delegated to `role:window-manager.navigation_action`, which in turn calls
the selected `role:task-switcher`; this single process remains non-blocking
while handling the reentrant `show` call. Apps toggles an already-visible
Overview off, and Back always dismisses the local Overview/quick-controls layer
before reaching an application. Three-button mode maps the left, middle, and
right thirds (or top, middle, bottom on a right-edge bar) to Back, Home, and
Apps, with press/reply feedback.

Text is rendered as UTF-8 through an optional runtime-loaded Xft/Noto CJK
backend when the small target Xft runtime is available.  It asks Fontconfig for
a Simplified-Chinese font and verifies that the selected face contains the UI's
core Chinese glyphs before accepting it; it falls back to an Xlib FontSet
without adding a toolkit or build-time font dependency. Navigation and chrome
text use their actual X11 surface dimensions and true glyph extents, so a
policy manager may resize the bars after mapping without shifting controls.
The clock redraws only its bounded centre rectangle when its second changes.
Expose handling uses the actual damaged rectangle, move-only Configure events
do not redraw, and gesture/overview animation damage is confined to the changed
bar/card/header region. This avoids idle or accidental full-screen dirty frames
on SPI display transports. No compositor, Openbox,
systemd, D-Bus, toolkit, or package manager is required.

## Deliberate boundaries

- Notification history remains owned by the replaceable `notification-center`
  role; the left chrome affordance calls it rather than duplicating storage.
- Quick controls route Wi-Fi, Bluetooth, and system rows into the Settings app's
  typed panels. The audio row calls the selected `audio-manager`; hardware
  policy and credentials remain in providers/Settings, not in this shell.
- Window-manager screenshots are optional. Missing, stale, or invalid bounded
  P6 PPM files produce a deterministic application-glyph card instead.
- The temporary pointer grab lasts only for an active pill gesture; there is no
  persistent grab.

## Build and test

The build reuses the sibling SDK static library and the target's existing X11
development files:

```sh
make -C ../msys-sdk build/libmsys-mipc.a
make i18n  # only after editing the shared JSON catalog
make all test
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Run the supervised protocol/RSS probe against a temporary test X server:

```sh
DISPLAY=:99 python3 tests/runtime_probe.py bin/msys-shell-native
```

The probe creates a private `SOCK_SEQPACKET` control channel, verifies
hello/ready, non-blocking request correlation, package-change refresh, real app
activation, task listing and task close, reads `/proc/PID/status` `VmRSS`, and
then sends a clean shutdown. It does not install the component. Core's
generated development fallback rewrites `@package/bin/msys-shell-native` to
`/opt/msys-dev/msys-shell-native/bin/msys-shell-native`; installed manifests
continue to resolve `@package` inside their immutable package root.

Run the complete mobile contract with the Shell and native policy together on
a private 320x480 Xvfb:

```sh
make integration-test
```

This checks the 320x396 application/Overview workarea, explicit X11 role
properties, notification and quick-control entry points, three-button and pill
Back/Home/Apps paths, Overview Exit, real thumbnail transport, and task-card close.
