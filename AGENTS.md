# Repository Guidelines

## Project Structure & Module Organization
`src/` contains the Windows desktop client. Keep app shell code in `src/RdpBoxApp.*`, `src/MainWindow.*`, and `src/WinMain.cpp`. Use `src/ui/` for dialogs and list UIs, `src/session/` for tab/session orchestration, `src/profiles/` for profile models and persistence, `src/rdp/` for FreeRDP integration and input/rendering helpers, and `src/common/` for shared native types/utilities. Runtime assets and Windows resources live under `src/resources/`.

`tests/` contains standalone native test executables wired through CMake when `BUILD_TEST=ON`. `tools/` is for helper binaries or packaging assets, `docs/superpowers/` is design/reference material, and `build/` is generated output that should not be edited by hand or committed.

## Architecture Notes
RdpBox is a Windows-only multi-tab RDP session manager built with native Win32/MFC-style UI plumbing and FreeRDP 3.x. It embeds FreeRDP in-process rather than launching `wfreerdp.exe` as a subprocess.

Keep ownership boundaries clear:
- `SessionManager` owns session lifecycle and tab mapping.
- `RdpSessionView` owns per-session rendering, focus/input forwarding, and reconnect UX.
- `FreeRdpProcess` owns the worker-thread FreeRDP connection and publishes frame/state/cursor updates back to the UI thread.
- `RdpClipboardBridge` and `WindowsClipboardBackend` own clipboard redirection.

Prefer adding new RDP behavior in focused helpers under `src/rdp/` rather than growing `RdpSessionView` or `FreeRdpProcess` further.

## Build, Test, and Development Commands
This project uses CMake presets on Windows with MSVC and Ninja.

- `cmake --preset msvc-debug`: configure a Debug build in `build/msvc-debug`.
- `cmake --build --preset msvc-debug`: build the app executable.
- `cmake --preset msvc-release`
- `cmake --build --preset msvc-release`: configure and build the release variant.
- `cmake --preset msvc-debug-test`: configure with `BUILD_TEST=ON`.
- `cmake --build --preset msvc-debug-test`: build the test executables.
- `ctest --preset msvc-debug-test --output-on-failure`: run all registered tests.

If you add a new test, register it in `tests/CMakeLists.txt` with `add_test(...)`.

## Coding Style & Naming Conventions
Use C++20, 4-space indentation, and braces on the next line for functions. Class names use PascalCase, methods use lowerCamelCase, and member fields use the `m_` prefix. Match nearby include ordering and keep Windows headers guarded with `WIN32_LEAN_AND_MEAN` and `NOMINMAX` before inclusion.

Prefer small helpers with explicit ownership over large cross-cutting classes. Follow the existing Win32 threading and event-delivery patterns instead of inventing a new abstraction layer.

## Testing Guidelines
Tests already exist under `tests/profiles/` and `tests/rdp/`. Keep new tests near the module they cover and name them after the behavior under test, for example `ProfileRepositoryTests` or `RdpCursorClassifierTests`.

Current test targets are lightweight native executables rather than a single aggregated test binary. For UI-heavy or integration-heavy changes that are hard to automate, include manual verification steps in your change notes, especially for connection flow, tab lifecycle, clipboard sync, input capture, and resize/reconnect behavior.

## Commit & Pull Request Guidelines
Use Conventional Commits such as `feat:` and `fix:` with short imperative subjects. Keep each commit scoped to one logical change.

Pull requests should summarize user-visible impact, list the Windows build/test commands you ran, and call out any changes to profile storage, clipboard behavior, input handling, or RDP lifecycle. Include screenshots for dialog or session UI changes.

## Security & Configuration Tips
Profiles are stored in `%AppData%/RdpBox/profiles.json`, and current MVP behavior may persist credentials locally. Do not commit real credentials, exported profile data, or generated build artifacts.

Treat clipboard, keyboard hook, and session credential changes as security-sensitive areas. Minimize logging of secrets and avoid introducing new plaintext persistence without explicit approval.
