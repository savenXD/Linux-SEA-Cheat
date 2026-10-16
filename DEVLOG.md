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
- Input is not yet captured or forwarded into overlay widgets (Mirror Mode and Exclusive Mode are scheduled for Phase 3).

---

## Phase 2 of 4: Dominance Engine & Adversary Stacking Resistance
- **Status**: Completed & Verified
- **Git Tag**: `phase2`

### Step 1: Muffin Unredirect & Compositor Investigation
- **Settings Check**:
  - `gsettings get org.cinnamon.muffin unredirect-fullscreen-windows` reports: `false`.
  - Muffin maintains compositing even when windows enter fullscreen state (`_NET_WM_STATE_FULLSCREEN`).
  - Screen 0 compositor selection owner `_NET_WM_CM_S0` remains active (Window `0x2600011`).
- **Atoms Supported**:
  - Root property `_NET_SUPPORTED` includes `_NET_WM_BYPASS_COMPOSITOR`.
  - Override-redirect windows (`GTK_WINDOW_POPUP`) are not managed by Muffin; they are composited by the X Composite overlay layer in natural X11 stacking order (`XQueryTree`).
  - Because fullscreen unredirection is disabled in Muffin, fullscreen applications (e.g. VLC, OpenGL games, video players) do not bypass the compositor. The overlay window stacked at the top of the root window tree remains visible over fullscreen applications.

---

### Step 2: Restack Watcher Architecture (`src/raise.c` & `src/raise.h`)
- **Dedicated Display Connection**:
  - Opened dedicated secondary `Display*` connection (`s_raise_dpy = XOpenDisplay(NULL)`) separate from GTK and hotkey connections.
- **Event Selection**:
  - Subscribed to `SubstructureNotifyMask` on the root window (shared, non-conflicting with WM's `SubstructureRedirectMask`).
  - Subscribed to `RRScreenChangeNotifyMask` via XRandR extension to dynamically reposition and re-assert geometry on display resolution or orientation changes.
- **GLib Event Loop Integration**:
  - Implemented custom `GSource` polling `ConnectionNumber(s_raise_dpy)`.
  - Drains all buffered X events when the socket FD becomes readable.
  - If any dirty event (`ConfigureNotify`, `CirculateNotify`, `MapNotify`, `CreateNotify`, `ReparentNotify`, `RRScreenChangeNotify`) is detected, invokes `check_and_raise()`.
- **Safety Watchdog & Active-Only Lifecycle**:
  - Added a 100ms safety watchdog timer (`on_watchdog_tick`) in case any X event is dropped.
  - The watcher runs **strictly when the overlay is visible**:
    - `overlay_show()` calls `raise_start()`, which attaches the FD source, drains stale events, and starts the watchdog.
    - `overlay_hide()` calls `raise_stop()`, which destroys the GSource and detaches all timers, resulting in strictly **0.0% CPU** and zero event processing while hidden.

---

### Step 3: Self-Defense Engine & Loop Guard
- **Token Bucket Rate Limiting**:
  - Protects against restack ping-pong storms between competing windows.
  - Threshold parameters:
    - Sustained Rate: `TOKEN_RATE = 300.0` raises/second.
    - Burst Allowance: `TOKEN_BURST = 60.0` raises.
    - Cooldown Period: `COOLDOWN_MS = 50` ms (`COOLDOWN_USEC = 50000` us).
- **Loop Guard Engagement**:
  - If rate limit / burst is exceeded, the loop guard trips, enters 50ms cooldown, and schedules `on_cooldown_expired`.
  - Logs a warning with complete metadata of the offending topmost window:
    - Window XID
    - Window title (`_NET_WM_NAME` / `WM_NAME`)
    - Window class (`WM_CLASS` res_name and res_class)
    - Override-redirect flag (`attr.override_redirect`)
    - Geometry (`attr.width`, `attr.height`, `attr.x`, `attr.y`)
  - Warning logs are throttled to at most once per second to avoid console spam.
  - Offending window metadata format example:
    ```
    [LOOP_GUARD ERROR] Raise limit exceeded (burst/rate saturated). Entering 50ms cooldown.
    [LOOP_GUARD] Offending window: XID=0x5600001, Name='Adversary 2: Override-Redirect Aggressive Raise', WM_CLASS='none'/'none', OverrideRedirect=YES, Geom=1920x1080+0+0
    ```

---

### Step 4: Adversary Test Harness
Created 4 hostile test programs and an automated stacking verification tool:
1. `tests/adversary/adv1_ewmh.c`:
   - Managed fullscreen window setting `_NET_WM_STATE_FULLSCREEN`, `_NET_WM_STATE_ABOVE`, `_NET_WM_STATE_STICKY`, and continuously asserting `_NET_ACTIVE_WINDOW`.
2. `tests/adversary/adv2_override.c`:
   - Hostile override-redirect window that executes an initial high-frequency storm burst (120 raises at 0.5ms intervals) to challenge the loop guard, followed by an aggressive raise every 20ms and on every root `ConfigureNotify`. Grabs keyboard and pointer. Clean signal handling and alarm ensures grabs are ungrabbed on exit.
3. `tests/adversary/adv3_focuslock.c`:
   - Focus-stealing window calling `XSetInputFocus` every 50ms and calling `XLowerWindow` on all top-level application windows (`_NET_CLIENT_LIST`).
4. `tests/adversary/adv4_video.sh`:
   - Fullscreen 3D OpenGL application (`glxgears -fullscreen`) rendering continuously at display refresh rate.
5. `tests/stackcheck.c`:
   - Precision X11 stacking inspector. Supports one-shot mode (`./tests/stackcheck <xid>`) and continuous monitor mode (`./tests/stackcheck --monitor <xid> <duration> <interval_ms> [pid] [min_percent] [max_cpu]`) sampling `/proc/<pid>/stat` for CPU usage.

---

### Acceptance Tests (T1 - T4) Execution & Results
Command: `make test2` (which executes `tests/run_adversaries.sh`)

| Test | Adversary / Condition | Duration | Total Samples | Topmost Samples | Topmost % | Overlay CPU | Loop Guard Status | Result |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **T1.1** | `adv1_ewmh` (Fullscreen + Above) | 30.02s | 598 | 598 | **100.00%** | 0.20% | Idle (Normal raise) | **PASS** |
| **T1.2** | `adv3_focuslock` (Focuslock + Lower) | 30.01s | 598 | 598 | **100.00%** | 0.17% | Idle (Normal raise) | **PASS** |
| **T1.3** | `adv4_video` (`glxgears -fullscreen`) | 30.05s | 599 | 599 | **100.00%** | 0.10% | Idle (Normal raise) | **PASS** |
| **T2** | `adv2_override` (Hostile Override Storm) | 30.05s | 599 | 596 | **99.50%** | 1.03% | **Engaged & Throttled** | **PASS** |
| **T3** | Hidden Mode (Zero CPU, Inactive Watcher) | 5.00s | 5 | N/A (Hidden) | N/A | **0.00%** | Detached / Inactive | **PASS** |
| **T4** | Phase 1 Regression Suite (T1 - T7) | ~15s | Full Suite | All passed | 100% | 0.00% | Zero regressions | **PASS** |

---

### Step 5: Visual Check Instructions (T5)
To manually verify stacking dominance over real-world applications:
1. Start the overlay in visible mode:
   ```bash
   ~/phoneoverlay/bin/phoneoverlay --show
   ```
2. Open a video in fullscreen:
   - Option A: Launch VLC with any video file in fullscreen: `vlc --fullscreen <video.mp4>`.
   - Option B: Launch Chromium/Firefox, open YouTube, and press `F` for fullscreen playback.
3. Observe that the phoneoverlay window remains anchored and visible at the bottom-left corner over the video.
4. Toggle visibility off and on using hotkey <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>X</kbd>.

---

## Phase 3: The Input Model & Focus Invariance

### Objectives Achieved
1. **Focus Invariance Guarantee**:
   - The overlay never steals X input focus or mutates the desktop window manager's `_NET_ACTIVE_WINDOW` state.
   - Using `GTK_WINDOW_POPUP` (`override_redirect = True`) coupled with `gtk_window_set_accept_focus(FALSE)` and `gtk_window_set_focus_on_map(FALSE)` ensures that clicks and internal widget focus within the overlay never trigger `XSetInputFocus` to the X server. The desktop application (e.g. `xed`, `gnome-terminal`, browser) retains active input focus uninterrupted.
2. **State Machine (`src/input.h` / `src/input.c`)**:
   - **Typing Target**: `TYPING_TARGET_NONE` (default) vs `TYPING_TARGET_OVERLAY`.
     - Clicking inside overlay sets `TYPING_TARGET_OVERLAY` and focuses internal test entry.
     - Clicking outside overlay sets `TYPING_TARGET_NONE` and releases grabs.
     - Hiding overlay automatically resets target to `NONE`.
   - **Key Modes**:
     - `MIRROR` (default): Keystrokes are received naturally by the focused desktop application, while XI2 raw key events are intercepted, translated via `gdk_keymap_translate_keyboard_state` / `XQueryPointer` mask, and injected into the overlay's `GtkEntry` via `gtk_widget_event`. Both receive typing simultaneously.
     - `EXCLUSIVE`: Takes an active keyboard grab (`XGrabKeyboard` with `owner_events=False`) on the overlay window so keystrokes go strictly to the overlay entry. If grab fails (`AlreadyGrabbed`), falls back immediately to `MIRROR` with a `[!GRAB]` alert badge in the status bar.
     - Mode toggled globally via <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>M</kbd>.
   - **Ghost Mode**:
     - Click-through display-only mode implemented via X11 Shape extension:
       - Active: `XShapeCombineRectangles(xdpy, s_xid, ShapeInput, 0, 0, NULL, 0, ShapeSet, YXBanded)`.
       - Inactive: `XShapeCombineMask(xdpy, s_xid, ShapeInput, 0, 0, None, ShapeSet)`.
     - Toggled globally via <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>G</kbd>.
   - **Pointer-Grab Fallback**:
     - Monitored via `XI_RawButtonPress` / `XI_RawButtonRelease` on root window.
     - When button press coordinates fall inside overlay geometry, schedules a 35ms fallback timer.
     - If native button click arrives on overlay widget within 35ms, timer is cancelled (zero duplicate clicks).
     - If native button click does not arrive (because an adversary holds an active pointer grab via `XGrabPointer`), fallback timer synthesizes `GdkEventButton` press/release at widget coordinates.
   - **Safety Timers & Panic Teardown**:
     - 60-second idle timer automatically drops typing target to `NONE` and releases exclusive keyboard grab.
     - Panic ungrab: whenever overlay hides, exits, or receives hotkey toggle, all grabs are released first so the user's keyboard can never be trapped.
     - Standard X11 server behavior verified under SIGKILL (-9): server automatically drops client-held grabs when socket connection closes.

---

### Acceptance Tests (T1 - T7) Execution & Results
Command: `make test3` (which executes `tests/run_phase3_tests.sh`)

| Test | Feature / Adversary Condition | Verification Method | Result | Notes |
| :--- | :--- | :--- | :---: | :--- |
| **T1** | Focus Invariance | Clicks inside overlay with `xed`, `gnome-terminal`, `chrome` focused | **PASS** | `_NET_ACTIVE_WINDOW` and `xdotool getwindowfocus` 100% unchanged |
| **T2** | MIRROR Mode Typing | Type "hello" with target=OVERLAY | **PASS** | "hello" received by both `xed` and overlay entry simultaneously |
| **T3** | EXCLUSIVE Mode Typing | Type "world" in EXCL, click outside, type "again" | **PASS** | "world" went only to overlay entry; "again" went only to `xed` |
| **T4** | GHOST Mode Pass-Through | Underlying click-counter window under overlay coordinates | **PASS** | SOLID: 0 clicks passed; GHOST: click passed through; RESTORE: solid restored |
| **T5** | Hostile Environment | Against `adv2_override` (pointer & keyboard grabs held) | **PASS** | Hotkey toggled, pointer fallback engaged, `[!GRAB]` displayed, typing received |
| **T6** | Panic Ungrab & SIGKILL | `kill -9` while holding EXCLUSIVE grab | **PASS** | Desktop keyboard immediately operational; zero trapped grabs |
| **T7** | Regression Check | Phase 1 (T1-T7) + Phase 2 Stacking Suites | **PASS** | Zero regressions across all stacking adversaries (100% pass) |

---

### User Quick-Start & Verification Instructions
1. Start the overlay in visible mode:
   ```bash
   ~/phoneoverlay/bin/phoneoverlay --show
   ```
2. Observe the status bar badges at the top of the overlay:
   - Mode Badge: `[MIRROR]` (blue) or `[EXCL]` (orange) or `[GHOST]` (purple).
   - Target Badge: `○ TYPING: NONE` (gray) vs `● TYPING: OVERLAY` (green).
   - Grab Warning: `[!GRAB]` (red) if exclusive grab failed.
3. Interactive Keybindings:
   - <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>X</kbd>: Toggle visibility on/off.
   - <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>M</kbd>: Cycle typing mode (MIRROR <-> EXCLUSIVE).
   - <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>G</kbd>: Toggle click-through GHOST mode.
4. Interactive Typing Test:
   - Open a text editor (e.g. `xed`) or terminal.
   - Click inside the overlay's "Type test:" entry. Notice the editor window retains full active focus and cursor.
   - Type on your physical keyboard:
     - In **MIRROR** mode: text appears in both your editor and the overlay entry.
     - In **EXCLUSIVE** mode: text appears only inside the overlay entry.
   - Click anywhere outside the overlay: typing target returns to `NONE` and keyboard input returns solely to your editor.

