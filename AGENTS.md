# AGENTS.md

This file provides guidance to the AI agent when working with code in this repository.

## Build & test (Windows, MSVC + Ninja + vcpkg)
- Configure from a Visual Studio Developer prompt: `cl` must be on `PATH`, and `CMakePresets.json` points the vcpkg toolchain at `D:/local/vcpkg-latest/...` (the `VCPKG_ROOT` env var is not used by the presets).
- The shared preset sets `RDPBOX_BUILD_LEGACY=ON`, so `msvc-debug`/`msvc-release` build only the legacy MFC target. Build Qt with `msvc-release-qt`, or add `-DRDPBOX_BUILD_QT=ON -DRDPBOX_BUILD_LEGACY=OFF`.
- CMake targets: `RdpBox` (Qt, output binary `RdpBoxQt`) and `RdpBoxLegacy` (output `RdpBox.exe`).
- Tests: `cmake --preset msvc-debug-test`, `cmake --build --preset msvc-debug-test`, `ctest --preset msvc-debug-test --output-on-failure` (`-R <Name>` for a single test). `tests/qt` only builds when Qt is enabled, which `msvc-debug-test` does not do by default.
- Tests are Windows-only: non-Windows configuration forces `BUILD_TEST=OFF`. Linux CI (`cmake --preset linux-release`, Ubuntu 24.04) compiles and packages only the Qt target via pkg-config `freerdp3`.
- CI builds both front ends; releases publish `RdpBox.exe` (legacy) and `RdpBoxQt.exe` (static Qt, Windows) plus the Qt Linux portable bundle `RdpBoxQt-<version>-linux-x64.tar.gz` and macOS app bundle `RdpBoxQt-<version>-macos-<arch>.zip`. macOS CI (`cmake --preset macos-release`, brew `qt@5`/`freerdp3`) builds and packages only the Qt target.
- The version is duplicated in `CMakeLists.txt` and `src/CMakeLists.txt` (keep both in sync; Linux packaging reads `src/CMakeLists.txt`). A release tag or `-DRDPBOX_VERSION_OVERRIDE=` overrides the display version.

## Layout & architecture
- Shared code lives in `src/common/` (`profiles/`, `rdp/`, `session/`, `ui/`). Front ends are `src/qtui/` (Qt 5) and `src/mfcui/` plus `src/mfcui/session/` (legacy MFC); only the window/session-host layer differs.
- `FreeRdpProcess` (`src/common/rdp/`) embeds FreeRDP in-process on a worker thread and posts frames/state/cursor updates to the UI thread. The per-session UI is `QtRdpSessionWidget` (Qt) or `CRdpSessionView` (legacy).
- Add new RDP behavior to focused helpers under `src/common/rdp/` rather than growing `FreeRdpProcess`, `QtRdpSessionWidget`, or `CRdpSessionView`.
- Large classes are split into same-directory `.cpp` partials (e.g. `MainWindow*`, `RdpSessionView*`, `WindowsClipboardBackend*`); edit the existing partial instead of merging them.

## Tests
- Each test is its own `add_executable` + `add_test` pair in `tests/CMakeLists.txt`; shared production sources compile once into `rdpbox_test_support`.
- `tests/profiles/ProfileRepositoryTests.cpp` parses output with cJSON on purpose while production uses `nlohmann_json`; do not drop either dependency.
- Manual verification still matters for connection flow, tab lifecycle, clipboard sync, input capture, and resize/reconnect; note what you checked.

## Style
- C++20, 4-space indentation, Allman braces for functions; PascalCase types, lowerCamelCase methods, `m_` member prefix.
- Define `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before Windows headers.
- Conventional Commits (`feat:`, `fix:`), one logical change per commit.

## Persistence & security
- Profiles live in `%AppData%/RdpBox/profiles.json`; passwords use DPAPI/portable `PasswordProtection`. Do not add plaintext persistence or log secrets.
- Treat clipboard, keyboard-hook, and credential changes as security-sensitive and call them out in PR notes.
