# RdpBox

Windows-only multi-tab RDP session manager built with MFC/Win32 and FreeRDP 3.x. RdpBox embeds the FreeRDP client in-process — it does not launch `wfreerdp.exe` as a subprocess.

## Features

- Multi-tab RDP sessions with per-tab close button (also middle-click to close).
- Profile CRUD: create, edit, duplicate, delete, search.
- Profile fields: name, host, port, domain, username, password, clipboard redirection, certificate handling, full-screen on connect.
- Persistent storage at `%AppData%/RdpBox/profiles.json`.
- Reconnect on demand when a session disconnects.
- Dynamic resolution updates via FreeRDP `DispClientContext`.
- Clipboard redirection (text + files) via `cliprdr`.
- Cursor redirection with native cursor classification (arrow, I-beam, resize, etc.).
- Local IME suppression and low-level keyboard hook for Win/Alt/Alt-Tab capture while the session is focused.
- F11 to toggle full screen, Esc to leave full screen.
- Per-profile certificate verification: with `Ignore certificate errors` unchecked, RdpBox prompts before accepting an unverified or changed certificate.

## Build

### Prerequisites

- Visual Studio 2019 or later with the **Desktop development with C++** workload (MFC required).
- vcpkg with the dependencies installed for the `x64-windows-static-md` triplet:
  - `cjson`
  - `freerdp` (with the client target)
- CMake 3.24+ and Ninja.
- Set the `VCPKG_ROOT` environment variable to your vcpkg checkout (e.g. `D:\Local\vcpkg-latest`). The CMake presets resolve the toolchain file from `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`.
- Configure from a Visual Studio Developer command prompt (or any shell where `cl` is on PATH). The presets pin `CMAKE_CXX_COMPILER=cl`; clang/lld will fail to link the FreeRDP static libraries because vcpkg builds them with `/GL`.

### Configure and build

```bash
# Debug
cmake --preset msvc-debug
cmake --build --preset msvc-debug

# Release
cmake --preset msvc-release
cmake --build --preset msvc-release
```

The executable is `build/<preset>/src/RdpBox.exe`.

### Tests

```bash
cmake --preset msvc-debug-test
cmake --build --preset msvc-debug-test
ctest --preset msvc-debug-test --output-on-failure

# Single test
ctest --preset msvc-debug-test -R RdpCursorClassifierTests --output-on-failure
```

## First connection

1. Launch `RdpBox.exe`. The Connections dialog opens automatically because no session is active.
2. Click **New…** and fill in at least the **Name** and **Host** fields. Defaults: port `3389`, clipboard enabled, ignore certificate errors enabled.
3. Click **OK**. The new profile appears in the list. Double-click it (or click **Connect**) to open it in a tab.

To close a tab, click the **×** on the tab strip, middle-click the tab, or right-click → **Close**. Right-click → **Reconnect** restarts a disconnected session.

## Security notes

- In normal mode, profile passwords are stored in `profiles.json` using Windows DPAPI protection instead of plain text. They can normally only be decrypted by the same Windows user on the same machine.
- In portable mode, saved credentials are encrypted with the built-in portable key path instead of plain text, so they can be moved with the portable directory without an interactive unlock step.
- With **Ignore certificate errors** disabled, RdpBox prompts the first time it sees an unverified certificate and again whenever the fingerprint changes. Accepting an untrusted certificate is a one-shot decision; RdpBox does not currently persist accepted fingerprints.
- The clipboard, low-level keyboard hook, and stored credentials are security-sensitive. Avoid logging them.

## Project layout

```text
src/
  RdpBoxApp.*       MFC CWinApp entry
  WinMain.cpp       Win32 entry point
  MainWindow.*      Frame window, top strip, tab host, fullscreen toggle
  common/           Native types (PointI/SizeI/FrameBuffer/CursorInfo) and string helpers
  profiles/         Profile + ProfileRepository (cJSON persistence)
  rdp/              FreeRdpProcess (worker thread), RdpSessionView (MFC view), cursor/clipboard/input helpers
  session/          SessionManager (UUID ↔ tab mapping, lifecycle)
  ui/               ProfileEditDialog, ConnectionListDialog, BrowserTabBar
  resources/        MFC dialog resources, icons
tests/              Standalone test executables (built with -DBUILD_TEST=ON)
```

See `CLAUDE.md` and `AGENTS.md` for additional architecture and contribution notes.
