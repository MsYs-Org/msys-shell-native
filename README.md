# MSYS Native Shell

Current source version: `0.6.15`.

Version 0.6.15 makes the parity-complete LVGL shell the supervised default and
keeps Xlib as an explicit manual fallback, preventing two full Shell processes
from occupying the same system-UI roles.

Version 0.6.6 explicitly applies the vertical flex flow to the named
`launcher_root` view inside the instantiated XML component. The document API
can expose a component wrapper above that view; applying layout to the wrapper
does not arrange the header/grid/status children and had placed the grid at the
right edge. The Xvfb probe renders real test tiles and checks geometry/pixels.
Pixel verification uses a repository-built Xlib `XGetImage` helper and does not
require `xwd`, ImageMagick, Python, or any host package installation.

Version 0.6.5 makes the Launcher work area independent of LVGL's initial
content-size pass: C resolves the remaining height from the live root, header,
status and row gaps before creating tiles. One bounded diagnostic line after an
authoritative app render reports root/grid/tile geometry without invalidating
or drawing any object.

Version 0.6.4 fixes the LVGL Launcher flex geometry after asynchronous app
discovery. The authoritative app list and persisted page layout were already
present, but a content-sized grid had zero height before its children were
created, so LVGL clipped every tile. The grid now has a non-zero flex basis and
the compact page controls have a bounded row container for 320-pixel screens.

Version 0.6.1 connects the LVGL preview Launcher to the same persisted
`msys.shell-preferences.v1` and `launcher-layout.v1` contracts as the Xlib
provider. It adds preference-sized multi-page grids, package icons, PPM
wallpaper/color fallback, long-press editing, drag reorder/group/cross-page
move, and ordinary/large-folder persistence while retaining Xlib as default.

Version 0.6.0 adds the second LVGL parity slice without selecting it by
default. Notification history, notification-presenter Toasts, and Quick
Controls are now real light LVGL overlays backed by the existing bounded C
notification ring and mIPC contracts. Wi-Fi reads HAL inventory/state and
never invents connectivity; Wi-Fi, Bluetooth, and system rows open the typed
Settings panels. Navigation supports the profile-selected three buttons or a
pill whose short release sends Home and whose held 28-pixel upward gesture
opens Overview. Press feedback remains object-local and uses the shared LVGL
animation policy.

Version 0.5.0 adds the first production-shaped LVGL 9.3/XML Shell slice beside
the established Xlib implementation. One C process owns four independent X11
surfaces for Launcher, system chrome, navigation, and Overview. Their light
layouts are package-owned XML documents while catalog, mIPC, window identity,
PPM screenshot scaling, task resource data, and navigation behavior remain C.
The 320x480 mobile geometry is fixed to 42-pixel bars and the exact
`0,42,320,396` workarea. Overview stays unmapped until `list_windows` returns,
then presents a two-column grid with real PPM thumbnails and PSS/RSS values.
The shared runtime retains exact LVGL invalid rectangles and zero idle flush.

`desktop-shell-lvgl` is now the supervised default provider after message
history, Quick Controls, launcher folders/editing, pill gestures, preference
RPCs, real application tiles, and idle zero-flush reached parity. The Xlib
`desktop-shell` remains a manual fallback and is not kept resident beside it.

Version 0.4.0 replaces the mobile Launcher's unbounded vertical application
list with fixed-capacity horizontal pages. `grid_columns`, `grid_rows`, and
`icon_size` may be automatic or explicitly bounded through the existing
preference RPC. A page indicator changes only with the selected page; a
stationary Home surface has no Launcher timer or damage.

Long-press enters edit mode with bounded cell feedback and an application
detail sheet. Dragging to a cell reorders it, a centred drop creates or fills a
folder, and holding at an edge moves it to the adjacent or a new page. Empty
pages are compacted. Order and folders are stored in a fixed-size,
percent-escaped state file which is fsync'd and atomically renamed. Folders
show up to four member icons on Home and open a bounded member grid; their
UTF-8 names are editable through `rename_launcher_folder`. Details and
Uninstall launch `org.msys.settings:software-center`, where destructive
confirmation and the real install-agent contract live. Catalog quick actions
are capped at three and forwarded only as a real `msys.core.activate` intent;
undeclared actions never appear.

`wallpaper_path` accepts a bounded absolute P6 PPM path. It is decoded and
antialiased once into the existing in-process XImage cache for the current
surface size; `wallpaper_color` remains the fallback. `acrylic=true` is a
low-memory contract for a pre-blurred wallpaper plus static rounded surface
tint, never live blur. Application images receive rounded-corner treatment
without a compositor or persistent mask allocation.

Version 0.3.26 keeps normal Recents limited to real application windows while
its separate background-process page requests the bounded `all-msys` process
scope, including ready hidden role surfaces such as the touch input method.
Rows expose component state, lifecycle, runtime, and RSS; the existing opt-in
checkbox remains the only way to add non-MSYS procfs entries. Overview also
samples CPU deltas and `MemAvailable` directly from procfs once per second
while it is mapped. Its compact CPU/MEM summary repaints only the small header
text rectangle and adds no process, service, allocation loop, or idle damage.

Version 0.3.23 keeps the explicit `notification-center` role as a normal
managed X11 overlay. It no longer marks that surface override-redirect, so the
native window policy can inventory, layer, dismiss, and apply workarea geometry
to it without trusting arbitrary override-redirect windows. Quick Controls and
the compact launch mask retain their existing private overlay behavior.

Version 0.3.22 gives `HH:MM:SS` eight fixed character slots and updates only
numeric slots whose glyph changed. A small reusable Pixmap is completed before
one bounded copy per changed digit, so the capture path cannot observe a blank
clear/draw intermediate; colon slots are never copied by the periodic path.
Minute and hour slots therefore add damage only on their real carry. The
existing display sink and stable dirty policy are unchanged.

Version 0.3.21 adds the native `notification-center` role to the existing C
Shell process. It keeps at most 24 normalized notifications in a fixed-size
ring, subscribes to both notification topics, and implements typed
`show`/`hide`/`toggle`/`list`/`clear` calls. The full-screen X11 overlay has
event-driven touch scrolling plus Clear and Close actions; it adds no process,
poll, toolkit, continuous animation, or unbounded allocation. Live session
language events redraw every Shell surface in place and refresh localized app
names. Timezone events call `tzset()` and damage only the clock region.

Version 0.3.20 adds a read-only background-process page inside Overview. Its
header action stays separate from Exit, defaults to supervised non-GUI MSYS
processes, and optionally includes non-MSYS system processes through one
explicit checkbox refresh. It never polls or exposes process termination. The
compact list has independent scrolling and reuses the 80ms atomic live-drag
scheduler.

Version 0.3.19 makes Launcher and Recents content follow vertical touch drags
again. The first changed position is immediate, later viewport frames are
coalesced to one atomic presentation per 80ms, and release only submits a final
position which was not already visible. Clamped drags remain viewport no-ops.

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

Both renderers are lean adaptive X11 shells implemented as one C process with
the dependency-free JSON mIPC C SDK. The new frontend uses LVGL/XML on Xlib;
the compatibility frontend draws through Xlib directly. This repository
supplies the shell package but does not modify or select any system profile;
profile ownership stays with the integrating MSYS configuration.

## Implemented boundary

One supervised component creates several independently identified X11 windows:

The launcher remains backed by the authoritative Core application inventory.

- Material-like, horizontally paged icon grid backed by the authoritative Core
  application inventory, with folders, atomic ordering, rounded package PPM
  icons, and bounded mobile grid preferences;
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
- full-screen native notification history with bounded scrolling, Clear, and Close;
- finite, damage-clipped interaction pulses for launcher cells, Recents cards,
  and the navigation pill (there is no continuous animation timer).

Quick Controls uses dependency-free Xlib line icons for Wi-Fi, Bluetooth,
Bluetooth audio, and Settings. Opening the panel asynchronously reads the
replaceable `audio-manager` role. The audio row shows the selected headset,
live volume/mute state, or a truthful unavailable reason. Its roughly 1/5,
3/5, and 1/5 left/centre/right zones perform -10, mute/unmute, and +10 while
leaving enough width for a headset name or degraded reason. PCM never crosses
mIPC.

The Xlib fallback component advertises only the roles it actually serves: `launcher`,
`system-chrome`, `navigation-bar`, `task-switcher`,
`notification-presenter`, and `notification-center`. Each has fallback
priority 89. The supervised LVGL component owns the same six roles at priority
100, so role calls cannot wake the fallback while LVGL is healthy. An explicit
role selection can still activate Xlib for recovery.

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
finite shell animations to their first frame. Overview content follows drag
motion through atomic, rate-limited viewport frames. Launcher page swipes keep
the current cached page stable during motion and atomically commit one adjacent
page on release; edit-mode hover changes only the old/new cells. A clamped drag
performs no viewport damage. This avoids clear/content
intermediate frames and unbounded MotionNotify repaint bursts without changing
the CH347 dirty-box implementation.

`get_preferences` and its `status` alias return the versioned launcher
preference state. `set_preferences` strictly merges one or more bounded fields,
and `reset_preferences` restores defaults. Successful mutations advance a
monotonic revision, atomically replace `launcher-preferences.json` below
`MSYS_COMPONENT_STATE_DIR` (or the package-owned `MSYS_APP_STATE_DIR`), update
launcher colour/layout/icon presentation immediately, and publish
`msys.shell.preferences.changed`. Invalid or unpersistable changes never alter
the in-memory state. Calls forwarded with `logical_target=role:task-switcher`
open Recents, `role:notification-presenter` presents a toast, and
`role:notification-center` controls the bounded history overlay. The provider
component target remains unchanged for older callers.
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
package-specific title. `x-msys-role-windows` additionally binds the launcher,
navigation, and notification-center role contracts to their separate,
explicit X11 surfaces.

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

- Notification history is intentionally memory-only and bounded; persistence
  can be added behind the same replaceable role without changing callers.
- Quick controls route Wi-Fi, Bluetooth, and system rows into the Settings app's
  typed panels. The audio row calls the selected `audio-manager`; hardware
  policy and credentials remain in providers/Settings, not in this shell.
- Window-manager screenshots are optional. Missing, stale, or invalid bounded
  P6 PPM files produce a deterministic application-glyph card instead.
- The temporary pointer grab lasts only for an active pill gesture; there is no
  persistent grab.

## Build and test

The build reuses the sibling SDK and LVGL runtime static libraries plus the
target's existing X11 development files. A normal build emits both the default
fallback and the manual LVGL provider; neither build step fetches packages:

```sh
make -C ../msys-sdk build/libmsys-mipc.a
make i18n  # only after editing the shared JSON catalog
make all test
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

The focused LVGL/Xvfb probe parses all four package XML documents, checks role
identity and the strict `0,42,320,396` workarea, then verifies that an idle
Launcher does not submit another invalid rectangle:

```sh
make lvgl-probe
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
