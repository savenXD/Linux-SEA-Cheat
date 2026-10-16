# DEVLOG: phoneoverlay

## Project Overview
- **Name**: phoneoverlay
- **Platform**: Linux Mint 22.3 (Zena / Ubuntu Noble base), Cinnamon, X11
- **Architecture**:
  - C11 + GTK3 drawing/widget layer.
  - Window type: `GTK_WINDOW_POPUP` (Override-Redirect, managed directly by X server, bypassing Muffin WM).
  - Dominance: Raw Xlib (`XRaiseWindow`) reacting to `SubstructureNotify` (`ConfigureNotify`/`CirculateNotify`/`MapNotify`) on Root Window + 500ms heartbeat.
  - Global Input & Hotkey: XInput2 raw events (`XI_RawKeyPress`, `XI_RawButtonPress`) selected on Root Window via a dedicated secondary Xlib `Display*` connection.
  - Browser: WebKitGTK (webkit2gtk-4.1) phone-styled UI.
  - Zero-focus-stealing design (`gtk_window_set_accept_focus(FALSE)`, `InputHint = False`).

---

## Phase 1: Foundation, Override-Redirect Dominance & Positioning
- **Status**: Completed & Verified
- **Tag**: `phase1`

### What Was Built
1. **Core Overlay Structure**:
   - `include/overlay.h`: Core structures, definitions, and module declarations.
   - `src/main.c`: Application entry point, signal handlers (`SIGINT`, `SIGTERM`, `SIGHUP`), unbuffered logging, and clean shutdown lifecycle.
   - `src/x11_dom.c`: Dedicated Xlib connection (`sub_dpy`), XRandR primary display geometry detection, non-fatal X error handlers (`XSetErrorHandler`, `XSetIOErrorHandler`), and GLib custom `GSource` event loop integration.
   - `src/ui.c`: GTK phone-style window (`GTK_WINDOW_POPUP`), zero-focus configuration (`gtk_window_set_accept_focus(FALSE)`, `InputHint = False`), CSS theme, and test interactive widgets.
2. **Build & Test Infrastructure**:
   - `Makefile`: Strict C11 build with `-Wall -Wextra -Werror=vla -pedantic -O2` with pkg-config integration.
   - `tests/check_stacking.c`: Accurate XQueryTree stacking comparator that traverses window hierarchies to root children.
   - `tests/competitor_window.c`: Background window simulator designed to challenge overlay stacking.
   - `tests/test_phase1.sh`: Automated acceptance test runner.

### Decisions & Rationale
- **Dedicated Xlib Connection**: Opening a second `Display*` connection dedicated to root-level events and XInput2 ensures that GTK's internal Xlib state machine is never polluted or locked by our low-level event interception.
- **Custom `GSource` for X11 FD**: Polling `ConnectionNumber(dpy)` alone in `g_io_add_watch` can miss events if Xlib buffers them in user space. A custom `GSource` implementing `.prepare` and `.check` with `XPending(dpy) > 0` guarantees zero dropped events and zero polling spin.
- **Dual Dominance (Event-Driven + Heartbeat)**: Reacting immediately to `SubstructureNotify` (`ConfigureNotify`, `MapNotify`, `CirculateNotify`) gives sub-millisecond re-raising when other windows raise or fullscreen. A 500ms `XQueryTree` heartbeat acts as defense-in-depth against edge cases.
- **WM Reparenting Resolution**: Normal X11 client windows are reparented into Muffin window frames; resolving ancestors to root children was implemented in `check_stacking.c` to accurately compare stacking order on the screen.

### Exact Test Commands & Results
Command: `make test_phase1`
```
=== Building Test Helpers ===
=== Starting PhoneOverlay ===
Overlay XID: 0x7a00003 (PID: 212320)
--------------------------------------------------------
TEST 1: Override-Redirect & Geometry Verification
--------------------------------------------------------
  Absolute upper-left X:  0
  Absolute upper-left Y:  440
  Width: 360
  Height: 640
  Override Redirect State: yes
PASS: Window has Override Redirect State: yes
PASS: Window size is 360x640
PASS: Window position is at (0, 440) - Bottom-Left of 1920x1080 display
--------------------------------------------------------
TEST 2: Zero-Focus-Stealing Verification on Interaction
--------------------------------------------------------
Active Window before click: 109051910
Clicking test button at (180, 670)...
Active Window after click: 109051910
PASS: Active window ID was NOT changed by clicking the overlay
PASS: Click was received and processed by overlay widget
--------------------------------------------------------
TEST 3: Stacking Dominance Against Competitor Window
--------------------------------------------------------
Competitor XID: 0x7e00001
Check Stacking: top_xid 0x7a00003 (root child 0x7a00003)=98, bottom_xid 0x7e00001 (root child 0x2601a20)=88, total=99
PASS: 0x7a00003 is strictly above 0x7e00001 in stacking order (98 > 88).
--------------------------------------------------------
TEST 4: Clean Shutdown on SIGTERM
--------------------------------------------------------
Exit code: 0
PASS: Clean exit code 0
PASS: Clean teardown sequence completed
========================================================
ALL PHASE 1 ACCEPTANCE TESTS PASSED!
========================================================
```
Dominance against override-redirect competitor:
`PASS: 0x7a00003 is strictly above 0x7e00001 in stacking order (98 > 97, total 99).`

### Known Limitations
- Overlay visibility is currently constant from startup (show/hide toggle via global hotkey `Ctrl+Shift+Alt+X` is the objective of Phase 2).
- Overlay displays diagnostic phone UI; full WebKitGTK browser integration is Phase 4.
