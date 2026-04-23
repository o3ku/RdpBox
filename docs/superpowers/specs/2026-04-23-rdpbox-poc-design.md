# RdpBox POC: FreeRDP Subprocess Embedding

## Goal

Validate that wfreerdp.exe can be launched as a child process and embedded into a Qt Widgets application, with stable keyboard/mouse input, resize handling, and disconnect recovery.

## Architecture

Single MainWindow containing one RdpSessionWidget. On startup, it launches wfreerdp.exe with a hardcoded RDP address and embeds the remote desktop view.

```
MainWindow
  └── RdpSessionWidget (QWidget)
        └── QWindow (wraps wfreerdp's rendering window)
        └── FreeRdpProcess (QProcess managing wfreerdp.exe)
```

## File Structure

```
src/
  CMakeLists.txt
  main.cpp
  MainWindow.h / .cpp
  rdp/
    FreeRdpProcess.h / .cpp
    RdpSessionWidget.h / .cpp
tools/
  wfreerdp.exe  (manually downloaded)
```

## Components

### FreeRdpProcess

- Manages wfreerdp.exe lifecycle via QProcess
- Constructs command-line arguments from connection parameters
- Monitors process state (running, finished, error)
- Emits Qt signals: connected, disconnected, errorOccurred

Constructor arguments:
- host, port, username, password (hardcoded for POC)
- parentWindowId (WId of the container widget)
- extra flags: clipboard redirection, certificate handling

Command template:
```
wfreerdp.exe /v:<host>:<port> /u:<user> /p:<pass> /parent-window:<hwnd> +clipboard
```

### RdpSessionWidget

- QWidget subclass that hosts the FreeRDP rendering area
- Creates a container for the child process window
- Uses QWindow::fromWinId() + QWidget::createWindowContainer() to embed the wfreerdp window
- Handles resize by sending WM_SIZE to the child window
- Handles focus restoration via showEvent / focusInEvent
- Shows disconnect overlay when session ends

### MainWindow

- QMainWindow with RdpSessionWidget as central widget
- Hardcoded connection: host, port, username, password
- Shows status in title bar (Connecting / Connected / Disconnected)
- Close event terminates child process cleanly

## Embedding Mechanism

1. RdpSessionWidget creates a native QWidget container and exposes its HWND
2. FreeRdpProcess launches wfreerdp.exe with /parent-window:<HWND>
3. wfreerdp creates its rendering window as a child of that HWND
4. RdpSessionWidget wraps the child window with QWindow::fromWinId()
5. The QWindow is embedded via QWidget::createWindowContainer()

If /parent-window is not supported by the wfreerdp build, fallback:
- Launch wfreerdp.exe without /parent-window
- Find its window by process ID and class name
- Use Win32 SetParent() to reparent

## Validation Criteria

| Item | Pass Condition |
|------|----------------|
| Window embedding | Remote desktop renders inside the Qt widget, no overflow |
| Keyboard/mouse | Input works after connection, focus restores after alt-tab |
| Resize | Window resize updates remote desktop area (fixed resolution + scaling acceptable for POC) |
| Disconnect | Process exit detected, UI shows message, application does not crash |

## Technical Constraints

- Qt 5 (via vcpkg, x64-windows-static-md triplet)
- CMake 3.24+ with Ninja generator
- C++20
- Windows only (Win32 HWND-based embedding)
- wfreerdp.exe manually placed in tools/ directory
