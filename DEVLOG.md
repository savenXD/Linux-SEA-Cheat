# DEVLOG: phoneoverlay

## Project Overview
- **Name**: phoneoverlay
- **Platform**: Linux Mint 22.3 (Zena / Ubuntu Noble base), Cinnamon, X11
- **Architecture**:
  - C (gnu11) + GTK3 drawing/widget layer.
  - Window type: `GTK_WINDOW_POPUP` (Override-Redirect, stacked directly by X server, bypassing Muffin WM).
  - Global Input & Hotkey: XInput2 raw events (`XI_RawKeyPress`, `XI_RawKeyRelease`) on root window via dedicated secondary Xlib `Display*` connection + passive `XGrabKey` consuming mask.
  - Zero-focus-stealing design (`gtk_window_set_accept_focus(FALSE)`, `InputHint = False`).

---

## Phase 1 of 4: Environment Recon, Scaffold, Override-Redirect Window, Global Hotkey Toggle
- **Status**: Completed & Verified
- **Git Tag**: `phase1`

### Step 1: Environment Reconnaissance Results
- **Session Type**: `x11` (Confirmed: `XDG_SESSION_TYPE=x11`, `DISPLAY=:0`).
- **OS & Kernel**:
  - OS: Linux Mint 22.3 (Zena) / Ubuntu 24.04 LTS (Noble)
  - Kernel: `6.17.0-40-generic` x86_64
- **Desktop Environment & Window Manager**:
  - Cinnamon: `6.6.7`
  - Muffin: `6.6.3`
  - Supporting WM Check: Window `0x2600006` (`_NET_WM_NAME = "Mutter (Muffin)"`, `_MUFFIN_VERSION = "6.6.3"`)
- **Compositor Status**:
  - Active: Yes, Muffin is compositing screen 0.
  - Selection Owner `_NET_WM_CM_S0`: Window `0x2600011`.
- **Displays & Monitors**:
  - Total Monitors: 1
  - Primary Monitor: `eDP` (1920x1080+0+0, physical size 344mm x 194mm)
- **XInput Support**:
  - xinput client version: `1.6.4`
  - XI server protocol version: `2.4` (fully supports XI 2.2+ raw events: `XI_RawKeyPress`, `XI_RawKeyRelease`)
- **Keybinding Conflict Check**:
  - Checked all GSettings in `org.cinnamon.desktop.keybindings` and `org.cinnamon.muffin`.
  - Found only one custom keybinding: `<Primary><Shift>k` (`coding-timelapse`).
  - Zero conflicts with target combo `Ctrl+Shift+Alt+X` (`<Primary><Shift><Alt>x`).
- **GPU & Graphics Drivers**:
  - Integrated GPU: AMD Renoir [Radeon RX Vega 6] (`radeonsi`, Mesa 25.2.8, OpenGL 4.6 Core Profile, Direct Rendering: Yes)
  - Discrete GPU: NVIDIA Corporation GA107M [GeForce RTX 3050 Mobile] (rev a1)
  - Active X11 renderer: AMD Radeon Graphics
- **Package Audit**:
  - Installed: `build-essential`, `pkg-config`, `libgtk-3-dev`, `libx11-dev`, `libxi-dev`, `libxext-dev`, `libxrandr-dev`, `xdotool`, `x11-utils`, `x11-xserver-utils`, `xinput`, `imagemagick`, `python3-xlib`.
  - Missing: `libxkbfile-dev`, `scrot`.
  - Single apt install command: `sudo apt install -y libxkbfile-dev scrot`
- **WebKitGTK Audit (for Phase 4)**:
  - `libwebkit2gtk-4.1-dev` (version 2.52.6-0ubuntu0.24.04.1) is installed and available.

---

### Step 2 — Scaffold
- `Makefile`: Targets `all`, `clean`, `run`, `test`; compiler flags `-std=gnu11 -Wall -Wextra -O2 -g`; dependencies `gtk+-3.0 x11 xi xext xrandr -lm`.
- `src/xutil.c` & `src/xutil.h`: XSetErrorHandler/XSetIOErrorHandler intercepting non-fatal errors; timestamped logging helper.
- `src/window.c` & `src/window.h`: GTK_WINDOW_POPUP override-redirect window implementation with Cairo rendering and helper API.
- `src/hotkey.c` & `src/hotkey.h`: Dedicated secondary Xlib display connection, XI2 raw events, passive XGrabKey, GLib event source.
- `src/main.c`: Signal handling with `g_unix_signal_add`, `--show` flag, main event loop.
- `tests/acceptance_phase1.sh`: Automated test runner for T1 through T7.

---

### Step 3 — The Window Implementation
- Window Type: `gtk_window_new(GTK_WINDOW_POPUP)`. Sets `override_redirect = True`.
- RGBA Visual: Configured using `gdk_screen_get_rgba_visual` + `gtk_widget_set_visual` + `gtk_widget_set_app_paintable(..., TRUE)`. Confirmed 32-bit depth visual (`Depth: 32, Visual Class: TrueColor`), providing transparent rounded corners under Muffin compositing.
- Geometry: Fixed constants `360x640`. Queried primary monitor geometry via `gdk_display_get_primary_monitor` (`1920x1080+0+0`). Anchored at bottom-left with 12px margin (`x = 12, y = 428`).
- Focus & Hints: `gtk_window_set_accept_focus(FALSE)`, `gtk_window_set_focus_on_map(FALSE)`, `gtk_window_set_skip_taskbar_hint(TRUE)`, `gtk_window_set_skip_pager_hint(TRUE)`, and raw `WM_HINTS` with `InputHint = False`.
- Cairo Content:
  - Outer rounded rectangle body (`radius 28.0`, dark `#171721`, subtle stroke).
  - Top phone notch with camera dot.
  - Centered clock (`HH:MM:SS`) using Cairo text extents.
  - 1-second refresh timer `g_timeout_add_seconds(1, on_clock_tick, area)` (only queues redraws when visible).
- Helper Functions: `overlay_show()`, `overlay_hide()`, `overlay_toggle()`, `overlay_get_xid()`, `overlay_is_visible()`. `overlay_show()` uses `XMapRaised(xdpy, xid)` to land on top.

---

### Step 4 — Global Hotkey Implementation
- Display Connection: Dedicated secondary `Display*` connection opened via `XOpenDisplay(NULL)` (separate from GDK's connection).
- XInput2 Extension: Verified version 2.2+ on server (`XIQueryVersion >= 2.2`).
- Event Selection: Selected `XI_RawKeyPress` and `XI_RawKeyRelease` on the root window for `XIAllMasterDevices`.
- GLib Integration: Integrated socket FD (`ConnectionNumber(s_hotkey_dpy)`) using a custom `GSource` (`prepare`, `check`, `dispatch`) draining `XPending` events without busy-spinning.
- Trigger Rule:
  - On `XI_RawKeyPress`: checks if keycode matches `XKeysymToKeycode(dpy, XK_x)` (recomputed dynamically).
  - Checks physical modifier key states using `XQueryKeymap`: requires Control (`XK_Control_L` or `XK_Control_R`), Shift (`XK_Shift_L` or `XK_Shift_R`), and Alt (`XK_Alt_L`, `XK_Alt_R`, `XK_Meta_L`, `XK_Meta_R`) all physically down.
  - Auto-repeat suppression: ignores repeats until `x` is released (`XI_RawKeyRelease`).
- Passive `XGrabKey`:
  - Registered `XGrabKey` on root window for `Ctrl+Shift+Alt+X` across 4 lock mask variations (None, NumLock, CapsLock, NumLock+CapsLock).
  - Intercepts and consumes the hotkey so background applications do not receive it.
  - Catches `BadAccess` in `xutil` error handler if already claimed.
  - Deduplication: 150 ms monotonic time window suppresses duplicate triggers from both paths.

---

### Step 5 — Lifecycle
- Handled `SIGINT` and `SIGTERM` via `g_unix_signal_add`: calls `overlay_hide()`, `hotkey_cleanup()` (which calls `XUngrabKey`), `window_cleanup()`, and exits cleanly with 0.
- Default start state is hidden. Passing `--show` starts the overlay visible.

---

### Acceptance Tests (T1 - T7) Execution & Results
Command: `make test` (which executes `tests/acceptance_phase1.sh`)

| Test | Objective | Result | Output / Real Evidence |
| :--- | :--- | :---: | :--- |
| **T1** | Warning-free build with `make` | **PASS** | `cc -std=gnu11 -Wall -Wextra -O2 -g ...` completed with 0 warnings, 0 errors. |
| **T2** | `xwininfo -id` reports Override Redirect State: yes | **PASS** | `Override Redirect State: yes`, `Depth: 32` |
| **T3** | Hotkey `xdotool key ctrl+shift+alt+x` toggles 10x | **PASS** | 10/10 toggles succeeded alternating between `IsViewable` and `IsUnMapped`. 0 failures. Deduplication logged. |
| **T4** | Zero focus stealing during interaction (tested with `xed`) | **PASS** | `xed` focused. Focus before: `127927077`, after overlay click: `127927077`. Active window before: `0x7a00325`, after: `0x7a00325`. Typing `_FOCUS_PASSED_100%` saved in file. |
| **T5** | Geometry matches bottom-left of primary monitor | **PASS** | `Absolute upper-left X: 12`, `Y: 428` (`360x640+12-12` on 1920x1080 display). |
| **T6** | Clean exit on `SIGTERM` and no leftover grabs | **PASS** | Exit code `0`. `Released all passive XGrabKey grabs`. `xinput list` and keyboard typing intact. |
| **T7** | Idle CPU while hidden is ~0% | **PASS** | Sampled with `top -b -n 5 -d 1 -p <PID>`: `0.0 0.0 0.0 0.0 0.0` (0.0% CPU). |

---

### Decisions & Rationale
1. **Dual Hotkey Triggering & Deduplication**: Combining passive `XGrabKey` with `XInput2` raw event listener provides both key consumption (so background apps don't type 'x') and resilience (if grabs are blocked or held by another tool). A 150ms monotonic timestamp deduplication window prevents double-toggling.
2. **RGBA Visual & Muffin Compositor**: Because Muffin is actively compositing screen 0 (`_NET_WM_CM_S0`), acquiring an RGBA visual with 32-bit depth enables genuine alpha channel transparency around the 28px rounded corners.
3. **Suppression of Clock Redraws When Hidden**: Skipping `gtk_widget_queue_draw` during the 1-second clock tick when `s_visible == FALSE` eliminates unnecessary wakeups and renders idle CPU usage strictly 0.0%.

---

### Known Limitations
- Window content is currently a Cairo phone mock with live clock (WebKitGTK mobile browser will be integrated in Phase 4).
- Input is not yet captured or forwarded into overlay widgets (Mirror Mode and Exclusive Mode are scheduled for Phase 2 / Phase 3).
