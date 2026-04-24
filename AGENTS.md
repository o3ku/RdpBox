# Repository Guidelines

## Project Structure & Module Organization
`src/` contains the application code for this Qt desktop client. Keep UI widgets in `src/ui/`, RDP process and embedding logic in `src/rdp/`, profile persistence in `src/profiles/`, and tab/session orchestration in `src/session/`. Top-level entry points live in `src/main.cpp`, `src/MainWindow.*`, and `src/CMakeLists.txt`.

`tools/` holds runtime helper binaries such as `wfreerdp.exe`. `docs/superpowers/` stores design notes and plans; treat it as reference, not runtime code. `build/` contains generated CMake/Ninja output and should not be edited by hand.

## Architecture Notes
This version is a minimal multi-tab RDP session manager built on FreeRDP and was referenced from `..\1remote` during implementation. Keep the design small and tab-centric: one profile opens one session widget, and session lifecycle logic belongs in `SessionManager` and `RdpSessionWidget`.

## Build, Test, and Development Commands
This project uses CMake presets and vcpkg on Windows.

- `cmake --preset msvc-debug`: configure a Debug build in `build/msvc-debug`.
- `cmake --build --preset msvc-debug`: build the app executable.
- `cmake --preset msvc-release && cmake --build --preset msvc-release`: prepare a release build.
- `cmake --preset msvc-debug-test`: configure with `BUILD_TEST=ON` for future test targets.
- `ctest --preset msvc-debug-test --output-on-failure`: run tests after a test preset has been configured and built.

## Coding Style & Naming Conventions
Use C++20 and follow the existing Qt-oriented style: 4-space indentation, braces on the next line for functions, and concise early returns. Class names use PascalCase (`FreeRdpDownloader`), methods use lowerCamelCase (`openSession`), and member fields use the `m_` prefix (`m_profileRepo`).

Prefer small classes with clear ownership boundaries between UI, session management, and RDP process control. Match nearby include ordering and signal/slot patterns before introducing new conventions.

## Testing Guidelines
There is a CMake test hook, but no committed `tests/` directory yet. Add new tests under `tests/` and wire them through the top-level `BUILD_TEST` option and `add_test(...)`. Name tests after the behavior they verify, for example `ProfileRepositoryTests` or `SessionManager_Reconnect`.

Until automated coverage exists, include manual verification steps in every change, especially for connection flow, tab lifecycle, resize behavior, and profile CRUD.

## Commit & Pull Request Guidelines
Recent history follows Conventional Commits such as `feat:` and `fix:`. Keep subjects short, imperative, and scoped to one change.

Pull requests should explain the user-visible impact, list Windows build/test commands run, and call out any changes to profile storage, downloader behavior, or RDP lifecycle. Include screenshots for dialog or tab UI changes.

## Security & Configuration Tips
Profiles are stored in AppData as `profiles.json`, and passwords are currently persisted in plain text for the MVP. Do not commit real credentials, exported profile data, or local build artifacts.
