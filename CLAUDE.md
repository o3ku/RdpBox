# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This repo uses the **CodeStable** workflow (see `codestable/`). Both this file and `AGENTS.md` are project hard-constraint entry points and apply together; **CLAUDE.md takes precedence on conflicts**. `AGENTS.md` covers contribution / commit / security conventions and is still authoritative for those topics.

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

RdpBox is a Windows-only, multi-tab RDP session manager built with Qt 5 and FreeRDP 3.x. It embeds the FreeRDP client in-process rather than launching `wfreerdp.exe` as a subprocess.

The **Qt UI is the default and only shipped target** (`add_executable(RdpBox ...)` over `RDPBOX_QT_SOURCES`). The original **MFC/Win32 UI is kept as an optional legacy target** (`RdpBoxLegacy`, built only with `-DRDPBOX_BUILD_LEGACY=ON`, `OFF` by default) and is not part of normal builds. Both front ends share the same `common/`, `profiles/`, `rdp/` (FreeRDP integration), and behavior-helper layers; only the top-level window/session-host classes differ.

### Data Flow (Qt — default)

```text
QtMainWindow (frameless window; QTabBar in caption + QStackedWidget of session pages)
  ├─ ProfileRepository (owned directly; JSON profile persistence)
  └─ QtRdpSessionWidget (one per tab/stack page; renders frames, handles input/focus)
       └─ FreeRdpProcess (manages FreeRDP context on a worker thread)
            └─ RdpClipboardBridge → WindowsClipboardBackend (clipboard redirection)
```

`QtMainWindow` owns the `ProfileRepository` and drives session tabs itself (no `SessionManager`); the connection list is a modal `QtConnectionListDialog` opened via Ctrl+P or the logo menu. See `src/qt/`.

### Data Flow (MFC — legacy, `RDPBOX_BUILD_LEGACY=ON` only)

```text
MainWindow (CFrameWnd + tab host)
  └─ SessionManager (owns sessions, maps sessionId ↔ CRdpSessionView)
       └─ CRdpSessionView (renders frames, handles input/focus)
            └─ FreeRdpProcess (manages FreeRDP context on a worker thread)
                 └─ RdpClipboardBridge → WindowsClipboardBackend (clipboard redirection)
```

`SessionManager`, `MainWindow*.cpp`, and `CRdpSessionView` belong to the legacy MFC target only. The Qt target does not compile them.

### Key Modules

- `src/qt/` - **Default Qt front end**: `QtMainWindow` (frameless window, caption `QTabBar` + `QStackedWidget`, owns `ProfileRepository`, hosts the modal `QtConnectionListDialog`), `QtRdpSessionWidget` (per-session render/input widget wrapping a `FreeRdpProcess`), `QtProfileDialog`, and `QtMain.cpp` (entry point: COM/Winsock init, single-instance mutex, style). `QtMainWindow.cpp` is split by concern within the file.
- `src/rdp/` - Core RDP integration (shared by both front ends):
  - `FreeRdpProcess` runs the FreeRDP connection on a `std::thread` and publishes frame/cursor/state updates back to the UI thread.
  - `CRdpSessionView` (legacy MFC only) renders the framebuffer, forwards mouse/keyboard input, and handles resize/focus/reconnect behavior; the Qt equivalent is `QtRdpSessionWidget`.
  - `RdpClipboardBridge` delegates to `PlatformClipboardBackend`; `WindowsClipboardBackend` owns the Windows clipboard implementation.
  - Prefer adding new RDP behavior in focused helpers here rather than growing `FreeRdpProcess`, `QtRdpSessionWidget`, or `CRdpSessionView` further.
- `src/session/` - **Legacy MFC only**: `SessionManager` maps UUID session IDs to `CRdpSessionView` instances and handles open/close/reconnect lifecycle. The Qt target does not use it (`QtMainWindow` drives tabs directly).
- `src/ui/` - UI-behavior helpers shared by both front ends (e.g. `ConnectionListBehavior`, `MainWindowLayoutBehavior`, `MainWindowSessionBehavior`, `MainWindowShortcuts`) plus the MFC-only dialogs `ProfileEditDialog` and `ConnectionListDialog`. Keep decision logic in these testable helpers rather than in the window classes.
- `src/profiles/` - `Profile` and `ProfileRepository` handle JSON-based profile persistence in `%AppData%/RdpBox/profiles.json`.
- `src/common/` - Shared native types: `PointI`, `SizeI`, `FrameBuffer`, `CursorInfo` (see `NativeTypes.h`); also `PasswordProtection` (DPAPI/portable) and `AppPaths`.

### File Layout Conventions

Several large classes are intentionally split across multiple `.cpp` files in the same directory — when editing them, find the right partial rather than collapsing them back together:
- `WindowsClipboardBackend.{h,cpp}` + `WindowsClipboardBackendDataObject.cpp` (`IDataObject` impl) + `WindowsClipboardBackendWindow.cpp` (hidden message window). (Shared by both front ends.)
- Legacy MFC only: `MainWindow.{h,cpp}` + `MainWindowChrome.cpp` (top strip / fullscreen chrome) + `MainWindowSessions.cpp` (tab + session orchestration); `RdpSessionView.{h,cpp}` + `RdpSessionViewInput.cpp` (mouse/keyboard) + `RdpSessionViewMessages.cpp` (Win32 message pump) + `RdpSessionViewRendering.cpp` (paint). These belong to the `RdpBoxLegacy` target and are not compiled by the default Qt build.

Profile persistence is implemented with `nlohmann_json` (in `src/common/AppPaths.cpp` and `src/profiles/ProfileRepository.cpp`). `cJSON` is used **only** by `tests/profiles/ProfileRepositoryTests.cpp` as an independent JSON parser to assert against the output `ProfileRepository` writes — the dual-library setup is intentional cross-validation. Don't remove `cJSON` from `tests/CMakeLists.txt` or from the `find_package(cJSON)` in `src/CMakeLists.txt` (the latter is needed because `tests/CMakeLists.txt` is included via `add_subdirectory` from `src/`). The release build also runs `upx -9` on `RdpBox.exe` post-build if `upx` is on PATH.

### FreeRDP Integration Details

`FreeRdpProcess` registers custom callbacks with the FreeRDP 3 API:
- Frame rendering uses `gdi_init(PIXEL_FORMAT_BGRX32)` with custom `BeginPaint`/`EndPaint` handling.
- Cursor mapping compares remote cursors against local cursor masks and prefers native cursors when possible.
- Dynamic resolution uses `DispClientContext` and `SendMonitorLayout` on resize; resize may trigger a reconnect.
- Keyboard handling translates Win32 `WM_KEY*` messages to RDP scancodes, including the special cases already handled in the implementation.

### Test Setup

Tests use standalone native executables under `tests/`, organized by module (`tests/rdp/`, `tests/profiles/`, `tests/ui/`). Each test is its own `add_executable` + `add_test` pair — there is no aggregated test binary. Current targets:
- `RdpResizeBurstTrackerTests`
- `RdpMouseMoveCoalescerTests`
- `RdpModifierSyncTrackerTests`
- `RdpCursorClassifierTests`
- `ProfileRepositoryTests`
- `WindowFrameMetricsTests`
- `MainWindowActivationTests`

When adding a new test, register it in `tests/CMakeLists.txt` with both `add_executable` and `add_test(...)`, and pull in the specific source files under test rather than linking against `RdpBox` (the test exes intentionally compile only the files they need so they don't drag in MFC/FreeRDP).

## Coding Style

- C++20, 4-space indentation, braces on the next line for functions
- PascalCase classes, lowerCamelCase methods, `m_` prefix for members
- MFC message maps and Win32 APIs
- Conventional Commits (`feat:`, `fix:`)
- Windows headers must be guarded with `#define WIN32_LEAN_AND_MEAN` and `#define NOMINMAX` before inclusion
