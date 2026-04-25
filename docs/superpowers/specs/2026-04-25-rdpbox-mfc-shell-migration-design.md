# RdpBox MFC Shell Migration Design

## Goal

Replace the Qt UI shell with a native MFC interface while keeping the FreeRDP connection and clipboard logic.

## Phase 1 Scope

- Build a native MFC main window.
- Keep the top row as a single strip containing the tab bar plus `New` and `Connections` entry points.
- Support profile CRUD entry points through dialogs.
- Support one tab per active RDP session.
- Keep FreeRDP logic, reconnect behavior, resize handling, and clipboard bridge.
- Continue using CMake.

## Out of Scope for Phase 1

- Sidebars, dock panels, tray integration, multi-window session management.
- Visual parity with the Qt UI.
- Full framework migration of every helper in one step.

## Target Architecture

```text
CMainFrame
  ├── top strip: tabs + New + Connections
  └── client area: active session view

CSessionManager
  ├── session lifecycle
  ├── tab/session mapping
  └── reconnect / close / open

CRdpSessionView
  ├── render remote frame
  ├── forward input
  └── show disconnected overlay

CConnectionDialog
CProfileDialog
ProfileRepository
FreeRdpProcess
```

## Components

| Component | Responsibility |
|---|---|
| `CMainFrame` | Owns the top strip, current session area, and window commands. |
| `CSessionManager` | Opens, closes, and reconnects sessions; keeps tabs and sessions in sync. |
| `CRdpSessionView` | Displays the remote framebuffer and forwards keyboard/mouse input. |
| `CConnectionDialog` | Lists saved profiles and opens a selected connection. |
| `CProfileDialog` | Creates and edits a profile. |
| `ProfileRepository` | Stores profiles in JSON. |
| `FreeRdpProcess` | Runs FreeRDP and exposes state/frame/cursor updates. |

## UI Layout

- One top row only.
- Tabs live on the same line as `New` and `Connections`.
- No separate toolbar.
- Main content is the active session view.

## Data Flow

1. User opens or creates a profile.
2. `CSessionManager` creates a new `CRdpSessionView` and starts `FreeRdpProcess`.
3. FreeRDP updates are marshaled back to the UI thread.
4. `CSessionManager` updates the tab title and session state.
5. Close/reconnect actions go through `CSessionManager`.

## Initial Behavior

- Start with the connections dialog if no session is open.
- Re-open connections dialog when the last tab closes.
- Tab title shows profile name.
- Disconnected sessions show a reconnect overlay.

## Migration Strategy

1. Add the MFC shell and top strip.
2. Port profile dialogs.
3. Port session host window and tab/session management.
4. Remove Qt UI dependencies from the shell.
5. Keep FreeRDP backend behavior stable during the cutover.

## Build

- Keep CMake as the build entrypoint.
- Move the UI target from Qt Widgets to MFC/Win32 as the shell migration progresses.
