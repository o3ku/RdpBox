# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

This project uses CMake with Ninja and vcpkg on Windows. MSVC toolchain is required.

```bash
# Configure (debug)
cmake --preset msvc-debug

# Build
cmake --build --preset msvc-debug

# Release build
cmake --preset msvc-release && cmake --build --preset msvc-release

# Configure with tests
cmake --preset msvc-debug-test

# Build tests
cmake --build --preset msvc-debug-test

# Run tests
ctest --preset msvc-debug-test --output-on-failure
```

The vcpkg toolchain is at `D:/local/vcpkg-latest` with triplet `x64-windows-static-md`. Build outputs go to `build/<preset-name>/`.

## Architecture

RdpBox is a Windows-only, multi-tab RDP session manager built with Qt (5/6 dual-support) and FreeRDP 3.x. It embeds the FreeRDP client as an in-process library (not a subprocess) for direct framebuffer rendering.

### Data Flow

```
MainWindow (QTabWidget tabs)
  └─ SessionManager (owns sessions, maps sessionId ↔ RdpSessionWidget)
       └─ RdpSessionWidget (QWidget, renders frames, handles input)
            └─ FreeRdpProcess (manages FreeRDP context on a worker thread)
                 └─ RdpClipboardBridge → WindowsClipboardBackend (OLE clipboard redirection)
```

### Key Modules

- **`src/rdp/`** — Core RDP integration:
  - `FreeRdpProcess` runs the FreeRDP connection on a `std::thread`, communicates frame/cursor/state updates to the Qt thread via `QMetaObject::invokeMethod` with `QueuedConnection`. All shared state is protected by a `QMutex`.
  - `RdpSessionWidget` renders FreeRDP frames via `QPainter::drawImage` in `paintEvent()`, forwards mouse/keyboard input, and manages a global `WH_KEYBOARD_LL` hook for capturing Win/Alt keys when the session has focus.
  - Clipboard: `RdpClipboardBridge` delegates to `PlatformClipboardBackend` (interface). `WindowsClipboardBackend` is the Windows implementation — a large (~1700 line) C++ reimplementation of FreeRDP's `wf_cliprdr` that runs a dedicated OLE clipboard thread with a hidden message-only window. There is also a `WindowsClipboardBackendNative.c` which is the original C implementation from FreeRDP (not currently compiled).
- **`src/session/`** — `SessionManager` maps UUID session IDs to `RdpSessionWidget` instances in the tab widget. Handles open/close/reconnect lifecycle.
- **`src/profiles/`** — `Profile` struct and `ProfileRepository` for JSON-based profile persistence in `%AppData%/RdpBox/profiles.json`.
- **`src/ui/`** — `ProfileEditDialog` (create/edit connection profile) and `ConnectionListDialog` (browse/search/select profile).

### FreeRDP Integration Details

`FreeRdpProcess` registers custom callbacks (`RDP_CLIENT_ENTRY_POINTS`) with the FreeRDP 3 API:
- Frame rendering: `gdi_init(PIXEL_FORMAT_BGRX32)` → custom `BeginPaint`/`EndPaint` copies the framebuffer to a `QImage` and delivers it to the Qt thread.
- Cursor mapping: Remote cursors are compared against local system cursor masks using a distance metric; matching cursors are replaced with native Qt cursors for better rendering quality.
- Dynamic resolution: Uses the `DispClientContext` channel to send `SendMonitorLayout` on resize (debounced 300ms). Resize triggers a full reconnect.
- Keyboard: Scan codes are translated from Win32 `WM_KEY*` messages to RDP scancodes, with special handling for NumLock and Right Shift extended codes.

### Test Setup

Tests use Qt Test framework. The test executable (`RdpBoxTests`) directly compiles specific source files from `src/` (currently `RdpSessionWidget.cpp` and `FreeRdpProcess.cpp`) rather than linking the full app. Tests use a `TestableRdpSessionWidget` subclass that overrides virtual methods.

## Coding Style

- C++20, 4-space indentation, braces on next line for functions
- PascalCase classes, lowerCamelCase methods, `m_` prefix for members
- Qt signal/slot patterns, `Q_OBJECT` macro in all QObject subclasses
- Conventional Commits (`feat:`, `fix:`)
- Windows headers must be guarded with `#define WIN32_LEAN_AND_MEAN` and `#define NOMINMAX` before inclusion
