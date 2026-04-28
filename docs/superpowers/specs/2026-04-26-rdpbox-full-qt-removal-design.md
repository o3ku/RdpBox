# RdpBox Full Qt Removal Design

> Historical note: this document describes the Qt-removal transition plan. The repository is now MFC/Win32-first; keep this file only as a record of the migration work.

## Goal

Produce an MFC-first RdpBox build that no longer depends on any Qt module at build time or runtime, while preserving the current single-window shell, profile CRUD flow, session tabs, FreeRDP connection lifecycle, reconnect behavior, clipboard bridge, resize handling, and remote rendering behavior.

## Scope

### In Scope

- Remove all Qt dependencies from application startup, build configuration, source code, tests, and resources.
- Keep the existing MFC shell as the primary UI.
- Replace Qt container, string, geometry, image, cursor, and event types with standard C++ and Win32-native equivalents.
- Keep the current profile storage format in `profiles.json`.
- Keep current session behaviors: one tab per connection, reconnect on disconnect, reopen the connection picker when the last tab closes.

### Out of Scope

- Reworking the UX beyond what is required for Qt removal.
- Replacing MFC with another framework.
- Adding new product features.
- Rewriting FreeRDP integration from scratch unless a targeted replacement is required to remove Qt.

## Current Problem

The repository already has an MFC shell, but the codebase is still structurally dependent on Qt:

- `CRdpBoxApp` creates and pumps `QGuiApplication`.
- `FreeRdpProcess` derives from `QObject` and uses signal-slot delivery.
- The RDP backend exposes `QImage`, `QCursor`, `QSize`, `QPoint`, `QString`, and Qt enums in public interfaces.
- Profile persistence uses `QString`, `QList`, `QUuid`, and `QJsonDocument`.
- Several tests and resources still target the old Qt UI shell.

This means the application is not actually an MFC-native build. It is an MFC host wrapped around a Qt-based backend and test surface.

## Target Architecture

```text
CRdpBoxApp (pure MFC/Win32 startup)
  -> MainWindow (MFC frame)
     -> SessionManager
        -> CRdpSessionView
           -> FreeRdpProcess
              -> FreeRDP callbacks / worker thread

ProfileRepository
  -> std::vector<Profile>
  -> cJSON serialization
  -> Win32 path + UUID helpers

RDP shared model layer
  -> PointI
  -> SizeI
  -> FrameBuffer
  -> CursorInfo
```

## Design Principles

1. Remove Qt from public interfaces before removing Qt from the build.
2. Keep behavior stable while replacing types and notification mechanisms.
3. Prefer small compatibility structs over leaking Win32 details everywhere.
4. Let the UI thread own painting and cursor application; let the worker thread own FreeRDP I/O.
5. Delete Qt-only code as soon as an MFC-native replacement exists.

## Component Design

### 1. Pure Native Profile Model

`Profile` will stop using `QString` and `QUuid`. The type will use native C++ strings with a clear storage rule:

- `id`: `std::string` using UTF-8
- `name`: `std::wstring`
- `host`: `std::wstring`
- `username`: `std::wstring`
- `password`: `std::wstring`
- `port`: `int`
- `clipboardEnabled`: `bool`
- `ignoreCertificate`: `bool`

Rationale:

- `id` is machine-oriented and serialized directly to JSON, so UTF-8 is sufficient and simpler for comparisons.
- User-entered UI strings stay as wide strings because MFC and Win32 already operate naturally on UTF-16.

`ProfileRepository` will move to:

- `std::vector<Profile>`
- `cJSON` for serialization
- Win32 file I/O helpers or standard file streams
- Win32 UUID generation via `CoCreateGuid`

Search logic will remain case-insensitive on profile name and host. The JSON schema will remain unchanged so existing user data still loads.

### 2. Shared Native RDP Types

A small native model header will replace Qt utility types used across the RDP layer.

Planned types:

- `struct PointI { int x; int y; };`
- `struct SizeI { int width; int height; };`
- `struct FrameBuffer { int width; int height; int stride; std::vector<std::uint8_t> pixels; };`
- `enum class CursorKind { Hidden, Arrow, IBeam, SizeWE, SizeNS, SizeNWSE, SizeNESW, SizeAll, Hand, Wait, AppStarting, Custom };`
- `struct CursorInfo { CursorKind kind; HCURSOR handle; bool ownsHandle; };`

Rules:

- `FrameBuffer` stores copied pixel data in a stable BGRA/BGRX layout suitable for GDI blitting.
- `CursorInfo` carries either a known system cursor classification or a custom `HCURSOR`.
- These types must not depend on MFC, Qt, or FreeRDP headers beyond what is strictly necessary.

This layer becomes the boundary between FreeRDP integration and the MFC shell.

### 3. FreeRdpProcess Without QObject

`FreeRdpProcess` will become a plain C++ class. Its responsibilities stay the same:

- configure FreeRDP settings
- own the FreeRDP context
- run the worker thread
- receive framebuffer and cursor updates
- send keyboard, mouse, focus, resize, and clipboard events

It will no longer:

- derive from `QObject`
- expose signals
- use `QMetaObject::invokeMethod`
- expose Qt types in any public method

Notification model:

- `setStateChangedCallback(std::function<void(State)>)`
- `setFrameUpdatedCallback(std::function<void()>)`
- `setDesktopResizedCallback(std::function<void(const SizeI &)>)`
- `setCursorUpdatedCallback(std::function<void()>)`

Threading model:

- FreeRDP worker thread remains internal to `FreeRdpProcess`.
- State, framebuffer, desktop size, and cursor data remain protected by a mutex.
- UI callbacks must not directly touch MFC windows from the worker thread.
- The MFC consumer will convert backend callbacks into `PostMessage` notifications or equivalent marshaling to the window thread.

This preserves the current async behavior without a Qt event loop.

### 4. Native Framebuffer and Cursor Pipeline

Framebuffer updates currently flow through `QImage`; cursor updates flow through `QCursor/QPixmap`.
Both will be replaced.

Framebuffer design:

- Copy `context->gdi->primary_buffer` into `FrameBuffer::pixels`.
- Store width, height, and stride alongside pixels.
- `CRdpSessionView::OnPaint` uses `StretchDIBits` or `SetDIBitsToDevice` to render the frame.

Cursor design:

- Keep the current classifier logic conceptually, but port it to native pixel buffers.
- Cursor analysis will operate on a native image buffer representation instead of `QImage`.
- Known cursor shapes map to shared system cursors via `LoadCursor`.
- Unknown cursors become custom `HCURSOR` values created from raw pixel data.
- Ownership is explicit so the view can destroy only cursors it owns.

The view will no longer need conversion helpers from `QCursor` or `QPixmap`.

### 5. Native Session View

`CRdpSessionView` remains the MFC host for one active RDP session, but it becomes a pure Win32/MFC consumer of the native backend types.

It will:

- read `FrameBuffer` snapshots from `FreeRdpProcess`
- paint via GDI
- receive backend notifications via posted window messages
- translate `WM_KEY*`, `WM_MOUSE*`, focus, wheel, and resize messages into backend calls
- manage overlay text for connecting/disconnected states
- manage any custom cursor lifetime returned by the backend

It will not:

- hold `QMetaObject::Connection`
- call any Qt cursor or image APIs
- translate through `Qt::MouseButton`, `Qt::KeyboardModifiers`, `QSize`, or `QPoint`

### 6. Modifier and Resize Helpers

`RdpModifierSyncTracker` and `RdpResizeBurstTracker` are currently lightweight but still Qt-typed.

Changes:

- `RdpResizeBurstTracker` switches from `QSize` to `SizeI`.
- `RdpModifierSyncTracker` replaces `QVector` with `std::vector`.
- Modifier state input switches from `Qt::KeyboardModifiers` to a native bitmask enum that expresses Shift/Ctrl/Alt.

This keeps their behavior while making them framework-independent.

### 7. Native App Startup

`CRdpBoxApp` will stop creating `QGuiApplication`, storing Qt argv copies, and pumping Qt events from `OnIdle`.

Startup becomes:

1. initialize common controls
2. initialize OLE/COM if required for clipboard and GUID helpers
3. create the main MFC frame
4. show the window
5. post the initial open-connections command

Shutdown becomes:

1. close active sessions
2. release backend objects
3. uninitialize any native subsystems that were explicitly initialized

There will be no Qt startup or message pumping path left.

## Data Flow

### Session Startup

1. User opens a profile from the connection dialog.
2. `SessionManager` creates a session view and passes the selected `Profile`.
3. `CRdpSessionView` starts `FreeRdpProcess`.
4. `FreeRdpProcess` configures FreeRDP settings using native string conversion helpers.
5. Backend worker connects and starts producing framebuffer, cursor, and state updates.
6. The backend triggers callbacks; the view posts itself update messages.
7. The view repaints and updates the displayed cursor.

### Input Forwarding

1. MFC window receives keyboard, mouse, wheel, focus, or resize events.
2. `CRdpSessionView` converts those messages into native backend calls.
3. `FreeRdpProcess` forwards them to FreeRDP APIs.

### Persistence Flow

1. MFC dialogs build or edit `Profile` objects.
2. `ProfileRepository` serializes them with `cJSON`.
3. The saved JSON file remains compatible with the current `profiles.json`.

## Migration Strategy

### Phase 1: Remove Qt from the profile layer

- Port `Profile` and `ProfileRepository` first.
- Update MFC dialogs and `MainWindow` to use the new types.
- Keep behavior identical and preserve file compatibility.

### Phase 2: Introduce native RDP model types

- Add `PointI`, `SizeI`, `FrameBuffer`, `CursorInfo`, and native modifier flags.
- Convert helper classes to these types without changing behavior.

### Phase 3: Port FreeRdpProcess interfaces

- Replace public Qt types in `FreeRdpProcess`.
- Replace signal-slot notifications with callbacks.
- Keep the worker thread and FreeRDP callback topology intact.

### Phase 4: Port session rendering and input

- Update `CRdpSessionView` to consume native types and GDI rendering only.
- Remove all Qt cursor and image conversions.

### Phase 5: Delete framework residue

- Remove `main.cpp`, `qtmain_stub.cpp`, Qt resources, Qt tests, and Qt UI leftovers.
- Remove `AUTOMOC`, `find_package(Qt...)`, and Qt link libraries from CMake.
- Ensure the build no longer references any Qt include directory, library, or generated moc/rcc step.

## Error Handling

- If profile JSON cannot be parsed, repository loading should fail closed to an empty in-memory list rather than crash.
- If GUID generation fails, profile creation should fail explicitly instead of silently producing an invalid profile.
- If framebuffer or cursor conversion fails, the session remains connected and falls back to a safe default frame/cursor behavior.
- If worker callbacks arrive after the view is closing, the view must ignore them safely through lifetime checks or message filtering.
- If resize requests cannot be sent, the current session should continue running without crashing.

## Testing Strategy

### Automated Tests to Keep or Add

- `ProfileRepository` round-trip serialization compatibility tests.
- `RdpModifierSyncTracker` unit tests using native modifier flags.
- `RdpResizeBurstTracker` unit tests using `SizeI`.
- `RdpCursorClassifier` tests against native bitmap buffers or `HCURSOR` fixtures.

### Automated Tests to Remove

- Qt widget shell tests.
- Qt tab styling tests.
- Any test that exists only to validate Qt signal-slot or widget behavior.

### Manual Verification

- Launch app and confirm no Qt DLLs or Qt package dependencies are required.
- Open the connection dialog on startup when no sessions exist.
- Create, edit, duplicate, delete, and search profiles.
- Open multiple sessions and switch tabs.
- Confirm remote framebuffer paints correctly.
- Confirm keyboard, mouse, wheel, modifier sync, and focus handling still work.
- Confirm reconnect behavior after disconnect.
- Confirm reconnect on resize still functions.
- Confirm clipboard redirection still works.
- Confirm the connection dialog reopens when the last tab closes.

## Build Impact

After migration, the executable target should link only against:

- MFC/CRT
- FreeRDP / FreeRDP-Client / WinPR
- cJSON
- required Win32 system libraries such as `Ole32`, `Shell32`, and `Comctl32`

The build must not require:

- `Qt::Core`
- `Qt::Gui`
- `Qt::Widgets`
- `AUTOMOC`
- `QRC` resources

## Risks

1. The biggest risk is hidden Qt coupling inside FreeRDP-related helpers, especially cursor classification and async delivery.
2. Cursor parity may regress temporarily because custom cursor generation is moving off `QCursor/QPixmap`.
3. String conversion bugs can break authentication or profile persistence if UTF-8 and UTF-16 boundaries are not kept explicit.
4. Worker-thread-to-UI-thread dispatch must stay strict, or MFC window access from the backend thread will become unstable.

## Acceptance Criteria

- The project configures and builds without any Qt package.
- No production source file includes Qt headers.
- No runtime path creates `QApplication` or `QGuiApplication`.
- Profile CRUD, tab/session management, rendering, input forwarding, reconnect behavior, and clipboard redirection still work in the MFC build.
- The repository contains no Qt-only UI shell code, Qt styling resources, or Qt-only tests that are no longer relevant to the MFC application.
