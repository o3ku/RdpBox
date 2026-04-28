# RdpBox MFC Shell Implementation Plan

> Historical note: this plan documents the MFC migration work. The current repository already contains that migration, so use it only for historical context.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Qt widget shell with a native MFC UI while keeping the existing FreeRDP session logic.

**Architecture:** The app will run as an MFC `CWinApp`/`CFrameWnd` shell with a single top strip (tabs + New/Connections buttons) and a native session view per tab. Existing FreeRDP, profile storage, cursor tracking, resize tracking, and clipboard code remain in place and are driven from the MFC shell through QtCore/QtGui only.

**Tech Stack:** C++20, MFC/Win32, QtCore, QtGui, FreeRDP 3.x, CMake, vcpkg.

---

### Task 1: Switch the application entrypoint to MFC

**Files:**
- Create: `src/mfc/RdpBoxApp.h`
- Create: `src/mfc/RdpBoxApp.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the MFC app class and Qt event pump**

```cpp
class CRdpBoxApp : public CWinApp
{
public:
    BOOL InitInstance() override;
    int ExitInstance() override;
    BOOL OnIdle(LONG lCount) override;

private:
    std::unique_ptr<QGuiApplication> m_qtApp;
    std::vector<std::string> m_qtArgsUtf8;
    std::vector<char*> m_qtArgv;
};
```

- [ ] **Step 2: Rewire CMake to build the MFC shell target**

Use `CMAKE_MFC_FLAG 2` on MSVC, drop `Qt::Widgets`, and link `Qt::Core`, `Qt::Gui`, `freerdp`, `freerdp-client`, `winpr`, `cjson`, `Ole32`, `Shell32`, and `Comctl32`.

- [ ] **Step 3: Register the new app entrypoint as the executable source set**

Replace the old `main.cpp`/Qt window bootstrap in the target source list with `mfc/RdpBoxApp.cpp`.

### Task 2: Build the native MFC shell window

**Files:**
- Create: `src/mfc/MainWindow.h`
- Create: `src/mfc/MainWindow.cpp`
- Create: `src/mfc/resource.h`
- Create: `src/resources/mfc_ui.rc`

- [ ] **Step 1: Add a frame window with a single top strip**

```cpp
class MainWindow : public CFrameWnd
{
public:
    MainWindow();
    bool createShell();

private:
    void layoutChildren();

    CTabCtrl m_tabCtrl;
    CButton m_newButton;
    CButton m_connectionsButton;
    CWnd m_sessionHost;
};
```

- [ ] **Step 2: Add the dialog resources needed by the shell**

Define `IDD_PROFILE_DIALOG`, `IDD_CONNECTION_DIALOG`, and the control IDs in `resource.h`, then create the corresponding dialog templates in `mfc_ui.rc`.

- [ ] **Step 3: Hook up tab selection and top-row commands**

Implement `OnTcnSelchange`, `OnBnClickedNew`, and `OnBnClickedConnections` so the shell can switch sessions and open the dialogs.

### Task 3: Port session management to the MFC shell

**Files:**
- Create: `src/session/SessionManager.h`
- Create: `src/session/SessionManager.cpp`
- Create: `src/mfc/RdpSessionView.h`
- Create: `src/mfc/RdpSessionView.cpp`

- [ ] **Step 1: Add a native session view that embeds FreeRdpProcess**

```cpp
class CRdpSessionView : public CWnd
{
public:
    bool create(CWnd *parent, const CRect &rect);
    void connectToHost(const Profile &profile);
    void disconnect();
    void reconnect();
    void setReconnectRequestedCallback(std::function<void()> callback);
};
```

- [ ] **Step 2: Paint the remote framebuffer with GDI**

Read `FreeRdpProcess::frame()` as a `QImage`, draw it in `OnPaint`, and show a centered overlay when the connection is starting or disconnected.

- [ ] **Step 3: Forward keyboard and mouse input to FreeRDP**

Reuse the existing modifier tracker, key translation, mouse button, mouse move, wheel, and low-level key hook logic from the Qt widget shell.

- [ ] **Step 4: Keep tab/session mapping in a native manager**

Mirror the existing UUID-to-session mapping, tab insertion/removal, close, and reconnect behavior from the Qt `SessionManager`.

### Task 4: Port profile dialogs to MFC

**Files:**
- Create: `src/ui/ProfileEditDialog.h`
- Create: `src/ui/ProfileEditDialog.cpp`
- Create: `src/ui/ConnectionListDialog.h`
- Create: `src/ui/ConnectionListDialog.cpp`

- [ ] **Step 1: Add a native profile editor dialog**

Use the existing `Profile` and `ProfileRepository` types, with MFC controls for name, host, port, username, password, clipboard, and certificate options.

- [ ] **Step 2: Add a native connection picker dialog**

Implement search, multi-selection, edit, duplicate, delete, and connect actions against `ProfileRepository`.

- [ ] **Step 3: Keep dialog behavior aligned with the current shell**

The connection dialog should reopen when the last session closes, and the profile editor should create a UUID when saving a new profile.

### Task 5: Remove Qt Widgets from the shell build

**Files:**
- Modify: `src/CMakeLists.txt`
- Modify: `src/main.cpp` (remove from build)
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`
- Modify: `src/session/SessionManager.h`
- Modify: `src/session/SessionManager.cpp`
- Modify: `src/ui/ProfileEditDialog.h`
- Modify: `src/ui/ProfileEditDialog.cpp`
- Modify: `src/ui/ConnectionListDialog.h`
- Modify: `src/ui/ConnectionListDialog.cpp`

- [ ] **Step 1: Remove the old Qt widget shell files from the executable source list**

Stop compiling the old `QMainWindow`, `QTabBar`, `QStackedWidget`, and `QDialog` shell files.

- [ ] **Step 2: Keep the backend FreeRDP sources in the target**

Retain `FreeRdpProcess`, `RdpCursorClassifier`, `RdpModifierSyncTracker`, `RdpResizeBurstTracker`, `RdpClipboardBridge`, and `WindowsClipboardBackend` in the executable.

- [ ] **Step 3: Update the startup and shutdown path**

Create the MFC shell window on launch, close all sessions on shutdown, and keep the Qt event pump alive until exit.
