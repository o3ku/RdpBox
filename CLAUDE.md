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

The vcpkg toolchain is resolved via `CMAKE_TOOLCHAIN_FILE` in `CMakePresets.json` with triplet `x64-windows-static-md`. Build outputs go to `build/<preset-name>/`. Configure from a Visual Studio Developer command prompt (or any shell where `cl` is on PATH); FreeRDP is built by vcpkg with `/GL`, so linking with clang/lld will fail.

Dependencies (`vcpkg.json`): `qt5-base`, `freerdp`, `nlohmann-json`, `cjson`.

## Architecture

RdpBox is a Windows-only, multi-tab RDP session manager that embeds the FreeRDP 3.x client in-process (rather than launching `wfreerdp.exe` as a subprocess). It is built as a single-file executable under 5 MB.

**The default `RdpBox` target is now the Qt 5 UI** (`src/qt/`). The original MFC/Win32 UI is kept as an optional legacy target `RdpBoxLegacy`, built only when configuring with `-DRDPBOX_BUILD_LEGACY=ON`. `CMAKE_MFC_FLAG` is set only when `BUILD_TEST` or `RDPBOX_BUILD_LEGACY` is on, so a plain Qt build does not require MFC. When making UI changes, target the Qt files; only touch the MFC files if the change is explicitly about the legacy shell.

### Shared-core / thin-UI design

The important structural fact: most RDP and session logic lives in framework-agnostic helpers, and the Qt and MFC front-ends are thin adapters over them. This is what lets the tests compile without pulling in Qt, MFC, or (mostly) FreeRDP.

- `*Behavior` / policy classes (e.g. `ui/MainWindowSessionBehavior`, `ui/ConnectionListBehavior`, `ui/MainWindowUpdateBehavior`, `session/SessionCollectionBehavior`, `session/SessionTabBehavior`, `rdp/RdpSessionViewBehavior`, `rdp/RdpProcessEventBehavior`, `rdp/RdpResolutionRecovery`, `rdp/RdpResizeBurstTracker`, `rdp/RdpMouseMoveCoalescer`, `rdp/RdpModifierSyncTracker`, `rdp/RdpKeyboardInputRouter`, `rdp/RdpCursorClassifier`) hold decision logic with no UI-framework dependency and are unit-tested directly.
- The UI widgets (`QtMainWindow`, `QtRdpSessionWidget`) own Qt objects, event pumps, timers, and painting, and delegate decisions to the behavior helpers.

When adding RDP or session behavior, prefer adding/extending a focused helper (and a test) rather than growing `QtMainWindow`, `QtRdpSessionWidget`, or `FreeRdpProcess`.

### Data Flow (Qt)

```text
QtMainWindow (QMainWindow: title bar, tab bar, sidebar profile list, session stack)
  └─ QtRdpSessionWidget (one per tab: renders frames, forwards input/focus/resize)
       └─ FreeRdpProcess (manages FreeRDP context on a worker thread)
            └─ RdpClipboardBridge → WindowsClipboardBackend (clipboard redirection)
```

`FreeRdpProcess` (`rdp/FreeRdpProcess.{h,cpp}`) exposes a framework-neutral API (`FreeRdpProcess.h`) and hides the FreeRDP context in a pimpl (`FreeRdpProcessNative.{h,cpp}`). Its callbacks (state/frame/cursor/desktop-resized/certificate) fire on the **FreeRDP worker thread** — consumers must marshal to their own UI thread before touching thread-affine objects; `FreeRdpProcess` does no marshalling itself.

### Key Modules

- `src/qt/` - Qt UI: `QtMain.cpp` (entry point: single-instance mutex, COM/Winsock init, Fusion style + stylesheet, startup-connection parsing), `QtMainWindow` (window chrome, tabs, profile sidebar, update flow), `QtRdpSessionWidget` (per-session render/input), `QtWindowChromeBehavior` (frameless caption hit-testing), `QtProfileDialog`.
- `src/rdp/` - FreeRDP integration and the framework-agnostic RDP helpers described above.
- `src/session/` - `SessionManager` maps UUID session IDs to views and handles open/close/reconnect lifecycle; `SessionCollectionBehavior` / `SessionTabBehavior` / `SessionResumePolicy` hold the testable logic.
- `src/profiles/` - `Profile` and `ProfileRepository` handle JSON profile persistence in `%AppData%/RdpBox/profiles.json`.
- `src/ui/` - Legacy MFC dialogs plus the shared `*Behavior` helpers used by both UIs.
- `src/common/` - Shared native types (`PointI`, `SizeI`, `FrameBuffer`, `CursorInfo` in `NativeTypes.h`), `AppPaths` (paths + portable mode), `PasswordProtection` (DPAPI vs portable credential encoding), `ConnectionLaunchArgs` (parse launch-by-name args), `UpdateClient` (GitHub release check/download).

### Window chrome (Qt)

The Qt window is frameless with a custom title bar. If `QWindowKit::Widgets` is available it is used (`RDPBOX_USE_QWINDOWKIT` defined); otherwise the app falls back to native Win32 hit-testing (`nativeEvent` → `QtWindowChromeBehavior`). Control this with the cache var `RDPBOX_QWINDOWKIT` = `AUTO` (default) / `ON` / `OFF`.

### App features worth knowing

- **Launch/restore**: `--portable` enables portable mode; a connections argument launches saved connections by name on startup (`common/ConnectionLaunchArgs`). Single-instance is enforced via a named mutex in `QtMain.cpp`.
- **Background updater**: `common/UpdateClient` + `ui/MainWindowUpdateBehavior` check GitHub for a newer `RdpBox.exe`, download it, and prompt to relaunch. The release build runs `upx -9` post-build if `upx` is on PATH.
- **Credentials**: stored encrypted via `PasswordProtection` (DPAPI in normal mode, a portable scheme in `--portable` mode). Treat clipboard, keyboard hook, and credential code as security-sensitive; avoid new plaintext persistence.

### File Layout Conventions

Several large classes are intentionally split across multiple `.cpp` files in the same directory — when editing them, find the right partial rather than collapsing them together:
- Qt: `QtMainWindow.{h,cpp}` (single file today, but keep behavior in the `*Behavior` helpers rather than inlining it).
- Legacy MFC (only relevant with `RDPBOX_BUILD_LEGACY=ON`): `MainWindow.{h,cpp}` + `MainWindowChrome.cpp` + `MainWindowSessions.cpp` + `MainWindowUpdate.cpp`; `RdpSessionView.{h,cpp}` + `RdpSessionViewInput.cpp` + `RdpSessionViewMessages.cpp` + `RdpSessionViewRendering.cpp`.
- `WindowsClipboardBackend.{h,cpp}` + `WindowsClipboardBackendDataObject.cpp` (`IDataObject` impl) + `WindowsClipboardBackendWindow.cpp` (hidden message window), shared by both UIs.

Profile persistence uses `nlohmann_json` (in `common/AppPaths.cpp` and `profiles/ProfileRepository.cpp`). `cJSON` is used **only** by the tests as an independent parser to cross-validate what `ProfileRepository` writes — the dual-library setup is intentional. Don't remove `cJSON` from `tests/CMakeLists.txt` or from `find_package(cJSON)` in `src/CMakeLists.txt` (the latter is needed because `tests/CMakeLists.txt` is included via `add_subdirectory` from `src/`).

### FreeRDP Integration Details

`FreeRdpProcess` registers custom callbacks with the FreeRDP 3 API:
- Frame rendering uses `gdi_init(PIXEL_FORMAT_BGRX32)` with custom `BeginPaint`/`EndPaint` handling.
- Cursor mapping (`RdpCursorClassifier`) compares remote cursors against local cursor masks and prefers native cursors when possible.
- Dynamic resolution uses `DispClientContext` + `SendMonitorLayout` on resize (`RdpResizeBurstTracker` coalesces bursts, `RdpResolutionRecovery` handles recovery); resize may trigger a reconnect.
- Keyboard handling translates Win32 `WM_KEY*` messages to RDP scancodes via `RdpKeyboardInputRouter` / `RdpInputEventUtil`, with modifier resync (`RdpModifierSyncTracker`) and reserved-shortcut handling (`RdpReservedShortcutTracker`).

## Test Setup

Tests are standalone native executables under `tests/`, organized by module (`tests/common/`, `tests/rdp/`, `tests/session/`, `tests/profiles/`, `tests/ui/`, `tests/qt/`, `tests/smoke/`). Each test is its own `add_executable` + `add_test` pair — there is no aggregated test binary. Test exes intentionally compile only the specific source files under test (plus minimal system/vcpkg libs) so they don't drag in Qt/MFC/FreeRDP.

When adding a new test, register it in `tests/CMakeLists.txt` with both `add_executable` and `add_test(...)`, pulling in the specific `src/...` files under test. If the test needs a runtime DLL on PATH (e.g. a Qt module), use the `rdpbox_prepend_test_runtime_path(<test> <target>)` helper (see `QtWindowChromeBehaviorTests`).

## Coding Style

- C++20, 4-space indentation, braces on the next line for functions
- PascalCase classes, lowerCamelCase methods, `m_` prefix for members
- Windows headers must be guarded with `#define WIN32_LEAN_AND_MEAN` and `#define NOMINMAX` before inclusion
- Conventional Commits (`feat:`, `fix:`)
- Keep decision logic in framework-agnostic `*Behavior` helpers (with tests); keep Qt/MFC classes as thin adapters
