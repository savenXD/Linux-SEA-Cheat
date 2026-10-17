# PhoneOverlay

A sleek, always-visible, phone-sized mobile web browser overlay designed specifically for Linux Mint (Cinnamon, X11).

```
   +------------------------------------+
   | 14:32  📶 100% ⚡  [MIRROR] ● OVERLAY|  <- Status Bar (Clock & Mode Pills)
   | [ 🔍 https://duckduckgo.com      ] |  <- URL / Search Entry & Progress Bar
   |------------------------------------|
   |                                    |
   |                                    |
   |        WebKitGTK 4.1 View          |  <- Embedded Persistent Mobile Browser
   |       (Android Mobile UA)          |
   |                                    |
   |                                    |
   |------------------------------------|
   |   ←       →       ⟳       ⌂        |  <- Navigation Bar
   |               ======               |  <- Home Gesture Indicator
   +------------------------------------+
```

---

## Key Features

1. **Topmost Dominance Over Everything**:
   - Built on an override-redirect X11 surface (`GTK_WINDOW_POPUP`) that bypasses window manager stacking rules.
   - Restack watcher using an event-driven `SubstructureNotify` connection continuously verifies that the overlay remains the absolute topmost window in the X11 tree, floating cleanly above fullscreen video players, `_NET_WM_STATE_ABOVE` windows, and hostile stacking adversaries.

2. **Strict Focus Invariance**:
   - Interacting with or clicking inside the overlay **never steals X11 input focus** or alters `_NET_ACTIVE_WINDOW`.
   - Your active desktop window (code editor, terminal, browser) remains active and focused at all times.

3. **Dual Input Engine**:
   - **MIRROR Mode (Default)**: Raw keyboard events (`XI_RawKeyPress`/`XI_RawKeyRelease`) are captured at the XInput2 level and synthesized into the overlay's active widget while the underlying focused application continues receiving all keystrokes unmodified.
   - **EXCLUSIVE Mode**: Temporarily acquires an exclusive keyboard grab (`XGrabKeyboard`) when private typing into the overlay is needed.
   - **GHOST Mode**: Makes the overlay 100% click-through using the X11 Shape Extension (`ShapeInput`), allowing mouse clicks to pass directly through to whatever is underneath.
   - **Foreign Grab Resilience**: Includes a 35ms pointer-grab fallback timer to synthesize clicks even when hostile apps attempt pointer grabs.

4. **Embedded WebKitGTK 4.1 Mobile Browser**:
   - Persistent session storage in `~/.local/share/phoneoverlay/` (cookies, cache, local storage persist across restarts).
   - Mobile User-Agent (`Pixel 8 / Android 14 Chrome`) ensures websites serve mobile layouts.
   - Smart URL bar: automatically detects URLs vs search queries (defaults to DuckDuckGo search template).
   - Audio/video auto-mute when hidden to conserve system resources.

5. **Dynamic Configuration & Single-Instance IPC**:
   - Managed via `~/.config/phoneoverlay/config.ini`.
   - Supports `SIGHUP` dynamic reload: repositioning, opacity changes, and hotkey rebinding occur instantly without restarting.
   - Single-instance Unix domain socket (`~/.local/share/phoneoverlay/ipc.sock`) enables direct command-line control.

6. **Supervisor & Autostart Integration**:
   - Lightweight crash supervisor (`phoneoverlay-supervisor`) logs abnormal terminations to `crash.log` and restarts the process automatically.
   - XDG autostart desktop entry (`~/.config/autostart/phoneoverlay.desktop`) launches seamlessly upon Cinnamon desktop login.

---

## Global Hotkeys

| Hotkey | Action |
| :--- | :--- |
| **`Ctrl` + `Shift` + `Alt` + `X`** | Toggle Overlay Visibility (Show / Hide) |
| **`Ctrl` + `Shift` + `Alt` + `M`** | Cycle Input Mode (`MIRROR` ⟷ `EXCLUSIVE`) |
| **`Ctrl` + `Shift` + `Alt` + `G`** | Toggle `GHOST` Mode (Solid ⟷ Click-Through) |
| **`Ctrl` + `Shift` + `Alt` + `S`** | Take Screenshot ("Snap") to Clipboard & Disk |
| **`Ctrl` + `Shift` + `Alt` + `W`** | Ghost Writer (Type into Active Focused Window) |

*Hotkeys are fully configurable in `~/.config/phoneoverlay/config.ini`.*

---

## CLI Usage

PhoneOverlay features single-instance control via Unix domain socket IPC:

```bash
# Show or hide overlay
phoneoverlay --show
phoneoverlay --hide
phoneoverlay --toggle

# Query status
phoneoverlay --status
# Output: MODE=MIRROR TARGET=NONE GHOST=SOLID GRAB=NONE VISIBLE=TRUE

# Change typing mode
phoneoverlay --mode mirror
phoneoverlay --mode exclusive

# Control ghost mode
phoneoverlay --ghost on
phoneoverlay --ghost off

# Navigate to URL or search term
phoneoverlay --load "https://news.ycombinator.com"

# Reload configuration without restart
phoneoverlay --reload-config
```

---

## Configuration (`~/.config/phoneoverlay/config.ini`)

On initial launch, a default configuration file is automatically created at `~/.config/phoneoverlay/config.ini`:

```ini
[General]
width = 360
height = 640
margin = 12
# Options: bottom-left, bottom-right, top-left, top-right
corner = bottom-left
# Options: primary or monitor index (0, 1, ...)
monitor = primary
opacity = 0.98
start_visible = false
# Options: mirror, exclusive
default_key_mode = mirror

[Browser]
home_url = https://html5test.com
search_url_template = https://duckduckgo.com/?q=%s
user_agent = Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Mobile Safari/537.36

[Hotkeys]
# Key letters to combine with Ctrl+Shift+Alt
toggle_key = x
mode_key = m
ghost_key = g
```

To reload changes immediately, run `kill -HUP $(pgrep -f bin/phoneoverlay)` or `phoneoverlay --reload-config`.

---

## Installation & Building

### Prerequisites

On Linux Mint / Ubuntu / Debian:
```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev libwebkit2gtk-4.1-dev \
                 libx11-dev libxi-dev libxext-dev libxrandr-dev libxtst-dev xdotool x11-utils
```

### Build

```bash
make clean
make
```

### Install

Installs binary to `~/.local/bin/phoneoverlay`, supervisor to `~/.local/bin/phoneoverlay-supervisor`, and autostart to `~/.config/autostart/phoneoverlay.desktop`:

```bash
make install
```

### Uninstall

```bash
make uninstall
```

---

## Automated Test Suite

PhoneOverlay includes a comprehensive automated test suite covering all architectural requirements:

```bash
# Run all test suites (Phases 1-4)
make test

# Run individual test suites
make test1   # Phase 1: Window properties, override-redirect, initial geometry
make test2   # Phase 2: Stacking dominance over hostile adversaries
make test3   # Phase 3: Input model, focus invariance, mirror/exclusive typing
make test4   # Phase 4: WebKitGTK browser, cookies, navigation, IPC, supervisor
```

---

## Architectural Notes & Honest Limitations

1. **X11 vs Wayland**:
   - PhoneOverlay is explicitly architected for the X11 windowing system (the default for Linux Mint Cinnamon). It leverages X11 override-redirect windows, raw XInput2 event streams, passive grabs, and XShape masks. These low-level X11 primitives do not exist in Wayland without compositor-specific private protocols.
2. **Input Method Editors (IME)**:
   - In MIRROR mode, keystrokes are translated via XKB state from raw hardware keycodes. Complex multi-stage input methods (such as Japanese or Chinese IMEs) that rely on external popup candidate windows and active X11 focus may require EXCLUSIVE mode for text composition.
3. **Compositor Support**:
   - Rounded phone corners and anti-aliased transparency require an active compositor (enabled by default in Cinnamon Muffin). On non-composited servers, PhoneOverlay falls back to an opaque window frame.
