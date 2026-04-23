# RdpBox MVP Design

## Goal

Transform the POC (single hardcoded connection) into a usable multi-tab RDP connection manager with profile CRUD, tab-based sessions, reconnect, and search.

## Architecture

```
MainWindow
  ├── Toolbar: New Connection | Edit | Delete | Connect (dropdown)
  └── QTabWidget
        ├── Tab 1 → RdpSessionWidget
        ├── Tab 2 → RdpSessionWidget
        └── ...
```

No left sidebar. All connection management through toolbar and dialogs.

## New Modules

| Module | Responsibility |
|--------|---------------|
| `Profile` | Data struct: id, name, host, port, username, password, clipboardEnabled, ignoreCertificate |
| `ProfileRepository` | JSON file read/write, CRUD, search by name/host |
| `ProfileEditDialog` | Dialog for creating/editing a profile |
| `ConnectionListDialog` | Dialog listing all profiles with search, double-click to connect |
| `SessionManager` | Manage multiple RdpSessionWidget lifecycles (open/close/reconnect), map tabs to sessions |

## File Structure

```
src/
  profiles/
    Profile.h
    ProfileRepository.h / .cpp
  ui/
    ProfileEditDialog.h / .cpp
    ConnectionListDialog.h / .cpp
  session/
    SessionManager.h / .cpp
  MainWindow.h / .cpp          (modified)
  rdp/                          (existing, unchanged)
```

## Data Format

`profiles.json` stored next to the executable (or in AppData). Plain-text passwords for MVP.

```json
[
  {
    "id": "a1b2c3d4-...",
    "name": "Office Server",
    "host": "10.0.0.8",
    "port": 3389,
    "username": "admin",
    "password": "secret",
    "clipboardEnabled": true,
    "ignoreCertificate": true
  }
]
```

## Profiles Path

Use `QStandardPaths::AppDataLocation` (e.g. `C:/Users/<user>/AppData/Local/RdpBox/`). Create directory on first run.

## User Flows

### New Connection

1. Click "New" toolbar button
2. `ProfileEditDialog` opens with empty fields
3. User fills in name, host, port, username, password, options
4. OK → `ProfileRepository::addProfile(profile)` → saves to JSON
5. Optionally auto-connect after creation

### Open Connection

1. Click "Connect" toolbar button → `ConnectionListDialog` opens
2. Dialog shows all profiles in a `QListWidget` with search `QLineEdit` at top
3. User types to filter by name or host
4. Double-click a profile → dialog accepts with selected profile ID
5. `SessionManager::openSession(profile)` creates a new tab and connects

### Close Tab

1. User clicks tab close button
2. `SessionManager::closeSession(sessionId)` stops the process, removes the tab

### Reconnect

1. Connection drops → `RdpSessionWidget` shows "Disconnected - Click to Reconnect" overlay
2. User clicks overlay → `SessionManager::reconnectSession(sessionId)` restarts the process

### Edit/Delete Profile

1. "Edit" toolbar button → `ConnectionListDialog` (with edit/delete buttons)
2. Select a profile, click Edit → `ProfileEditDialog` pre-filled
3. Select a profile, click Delete → confirmation → `ProfileRepository::removeProfile(id)`

## SessionManager

Core interface:
- `openSession(const Profile &profile)` → creates tab, starts connection
- `closeSession(const QString &sessionId)` → stops process, removes tab
- `reconnectSession(const QString &sessionId)` → restarts process with same profile
- Internal: maps tab index → sessionId, sessionId → RdpSessionWidget*

Each session gets a UUID. Tab title shows profile name.

## RdpSessionWidget Changes

- Add click handler on "Disconnected" overlay to emit a `reconnectRequested()` signal
- `SessionManager` connects this signal to its `reconnectSession` method

## MainWindow Changes

- Replace single `RdpSessionWidget*` with `QTabWidget*` as central widget
- Add toolbar with actions: New, Edit, Connect
- Remove hardcoded connection parameters
- On startup: load profiles from `ProfileRepository`, show empty tab area

## ProfileEditDialog Fields

| Field | Widget | Default |
|-------|--------|---------|
| Name | QLineEdit | "" |
| Host | QLineEdit | "" |
| Port | QSpinBox | 3389 |
| Username | QLineEdit | "" |
| Password | QLineEdit (echoMode=Password) | "" |
| Clipboard | QCheckBox | checked |
| Ignore Certificate | QCheckBox | checked |

## Technical Constraints

- Qt 5.15.17 via vcpkg, x64-windows-static-md
- CMake 3.24+, Ninja, C++20
- Windows only (Win32 HWND embedding)
- wfreerdp.exe in tools/ directory
