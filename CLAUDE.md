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
cmake --preset msvc-release
cmake --build --preset msvc-release

# Configure with tests
cmake --preset msvc-debug-test

# Build tests
cmake --build --preset msvc-debug-test

# Run all tests
ctest --preset msvc-debug-test --output-on-failure

# Run a single test by name
ctest --preset msvc-debug-test -R RdpCursorClassifierTests --output-on-failure
```

The vcpkg toolchain is resolved from the `VCPKG_ROOT` environment variable with triplet `x64-windows-static-md`. Build outputs go to `build/<preset-name>/`. The presets pin `CMAKE_CXX_COMPILER=cl`, so configure from a Visual Studio Developer command prompt (or any shell where `cl` is on PATH); using clang/lld will fail to link FreeRDP because vcpkg builds it with `/GL`.

## Architecture

RdpBox is a Windows-only, multi-tab RDP session manager built with MFC/Win32 and FreeRDP 3.x. It embeds the FreeRDP client in-process rather than launching `wfreerdp.exe` as a subprocess.

### Data Flow

```text
MainWindow (CFrameWnd + tab host)
  └─ SessionManager (owns sessions, maps sessionId ↔ CRdpSessionView)
       └─ CRdpSessionView (renders frames, handles input/focus)
            └─ FreeRdpProcess (manages FreeRDP context on a worker thread)
                 └─ RdpClipboardBridge → WindowsClipboardBackend (clipboard redirection)
```

### Key Modules

- `src/rdp/` - Core RDP integration:
  - `FreeRdpProcess` runs the FreeRDP connection on a `std::thread` and publishes frame/cursor/state updates back to the UI thread.
  - `CRdpSessionView` renders the framebuffer, forwards mouse/keyboard input, and handles resize/focus/reconnect behavior.
  - `RdpClipboardBridge` delegates to `PlatformClipboardBackend`; `WindowsClipboardBackend` owns the Windows clipboard implementation.
  - Prefer adding new RDP behavior in focused helpers here rather than growing `FreeRdpProcess` or `CRdpSessionView` further.
- `src/session/` - `SessionManager` maps UUID session IDs to `CRdpSessionView` instances and handles open/close/reconnect lifecycle.
- `src/profiles/` - `Profile` and `ProfileRepository` handle JSON-based profile persistence in `%AppData%/RdpBox/profiles.json`.
- `src/ui/` - `ProfileEditDialog` and `ConnectionListDialog`.
- `src/common/` - Shared native types: `PointI`, `SizeI`, `FrameBuffer`, `CursorInfo` (see `NativeTypes.h`).

### FreeRDP Integration Details

`FreeRdpProcess` registers custom callbacks with the FreeRDP 3 API:
- Frame rendering uses `gdi_init(PIXEL_FORMAT_BGRX32)` with custom `BeginPaint`/`EndPaint` handling.
- Cursor mapping compares remote cursors against local cursor masks and prefers native cursors when possible.
- Dynamic resolution uses `DispClientContext` and `SendMonitorLayout` on resize; resize may trigger a reconnect.
- Keyboard handling translates Win32 `WM_KEY*` messages to RDP scancodes, including the special cases already handled in the implementation.

### Test Setup

Tests use standalone native executables under `tests/`. The current targets are:
- `RdpResizeBurstTrackerTests`
- `RdpMouseMoveCoalescerTests`
- `RdpModifierSyncTrackerTests`
- `RdpCursorClassifierTests`
- `ProfileRepositoryTests`

## Coding Style

- C++20, 4-space indentation, braces on the next line for functions
- PascalCase classes, lowerCamelCase methods, `m_` prefix for members
- MFC message maps and Win32 APIs
- Conventional Commits (`feat:`, `fix:`)
- Windows headers must be guarded with `#define WIN32_LEAN_AND_MEAN` and `#define NOMINMAX` before inclusion
