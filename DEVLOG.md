# DEVLOG: phoneoverlay

## Project Overview
- **Name**: phoneoverlay
- **Platform**: Linux Mint 22.3 (Zena / Ubuntu Noble base), Cinnamon, X11
- **Architecture**:
  - C11 + GTK3 drawing/widget layer.
  - Window type: `GTK_WINDOW_POPUP` (Override-Redirect, managed directly by X server, bypassing Muffin WM).
  - Dominance: Raw Xlib (`XRaiseWindow`) reacting to `SubstructureNotify` (`ConfigureNotify`/`CirculateNotify`) on Root Window.
  - Global Input & Hotkey: XInput2 raw events (`XI_RawKeyPress`, `XI_RawButtonPress`) selected on Root Window via a dedicated secondary Xlib `Display*` connection.
  - Browser: WebKitGTK (webkit2gtk-4.1) phone-styled UI.
  - Zero-focus-stealing design (`gtk_window_set_accept_focus(FALSE)`).

## Phase 1: Foundation, Override-Redirect Dominance & Positioning
- **Status**: In Progress
- **Dependencies Identified**:
  - `libgtk-3-dev`
  - `libwebkit2gtk-4.1-dev`
  - `libxi-dev`
  - `libxrandr-dev`
  - `libxext-dev`
  - `libxfixes-dev`
- **Initial Setup**:
  - Initialized git repository.
  - Symlinked `~/phoneoverlay` -> `/home/saven/Codding Projects/god-window`.
