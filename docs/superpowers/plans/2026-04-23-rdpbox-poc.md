# RdpBox POC Implementation Plan

> Historical note: this plan targets the original Qt-based POC and is no longer the authoritative implementation guide for the current MFC/Win32 codebase.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal Qt application that launches wfreerdp.exe as a child process, embeds it via `/parent-window`, and validates keyboard/mouse input, resize, and disconnect handling.

**Architecture:** MainWindow hosts a single RdpSessionWidget. FreeRdpProcess (QProcess wrapper) manages wfreerdp.exe lifecycle. The wfreerdp rendering window is created as a child of RdpSessionWidget's HWND via the `/parent-window` flag — no QWindow wrapping needed for POC.

**Tech Stack:** Qt 5 (via vcpkg), CMake 3.24+, Ninja, C++20, Win32 API (user32)

---

## File Structure

| File | Responsibility |
|------|---------------|
| `src/CMakeLists.txt` | Build targets for RdpBox executable |
| `src/main.cpp` | Application entry point |
| `src/MainWindow.h` | Main window header |
| `src/MainWindow.cpp` | Main window — hosts RdpSessionWidget, hardcoded connection |
| `src/rdp/FreeRdpProcess.h` | Subprocess management header |
| `src/rdp/FreeRdpProcess.cpp` | QProcess wrapper for wfreerdp.exe |
| `src/rdp/RdpSessionWidget.h` | RDP view container header |
| `src/rdp/RdpSessionWidget.cpp` | QWidget that hosts FreeRDP rendering, handles resize/focus/disconnect |

---

### Task 1: Project Scaffold

**Files:**
- Create: `src/CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `src/MainWindow.h`
- Create: `src/MainWindow.cpp`

- [ ] **Step 1: Create `src/CMakeLists.txt`**

```cmake
set(RDPBOX_SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    rdp/FreeRdpProcess.h
    rdp/FreeRdpProcess.cpp
    rdp/RdpSessionWidget.h
    rdp/RdpSessionWidget.cpp
)

add_executable(RdpBox WIN32 ${RDPBOX_SOURCES})

target_include_directories(RdpBox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RdpBox PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
)
```

- [ ] **Step 2: Create `src/MainWindow.h`**

```cpp
#pragma once

#include <QMainWindow>

class RdpSessionWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    RdpSessionWidget *m_sessionWidget;
};
```

- [ ] **Step 3: Create `src/MainWindow.cpp`**

```cpp
#include "MainWindow.h"
#include "rdp/RdpSessionWidget.h"

#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_sessionWidget(new RdpSessionWidget(this))
{
    setWindowTitle("RdpBox - POC");
    setCentralWidget(m_sessionWidget);
    resize(1280, 800);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}
```

- [ ] **Step 4: Create `src/main.cpp`**

```cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
```

- [ ] **Step 5: Build and verify empty window**

Run:
```bash
cmake --preset msvc-debug
cmake --build --preset msvc-debug
```

Expected: `RdpBox.exe` builds and shows an empty 1280x800 window.

- [ ] **Step 6: Commit**

```bash
git add src/CMakeLists.txt src/main.cpp src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: add project scaffold with empty MainWindow"
```

---

### Task 2: FreeRdpProcess

**Files:**
- Create: `src/rdp/FreeRdpProcess.h`
- Create: `src/rdp/FreeRdpProcess.cpp`

- [ ] **Step 1: Create `src/rdp/FreeRdpProcess.h`**

```cpp
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class FreeRdpProcess : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Starting, Running, Finished };

    explicit FreeRdpProcess(QObject *parent = nullptr);
    ~FreeRdpProcess();

    void start(const QString &exePath,
               const QString &host,
               int port,
               const QString &username,
               const QString &password,
               WId parentHwnd);
    void stop();

    State state() const { return m_state; }

signals:
    void stateChanged(FreeRdpProcess::State newState);

private:
    void setState(State newState);

    QProcess *m_process = nullptr;
    State m_state = State::Idle;
};
```

- [ ] **Step 2: Create `src/rdp/FreeRdpProcess.cpp`**

```cpp
#include "FreeRdpProcess.h"

FreeRdpProcess::FreeRdpProcess(QObject *parent)
    : QObject(parent)
{
}

FreeRdpProcess::~FreeRdpProcess()
{
    stop();
}

void FreeRdpProcess::start(const QString &exePath,
                            const QString &host,
                            int port,
                            const QString &username,
                            const QString &password,
                            WId parentHwnd)
{
    if (m_state == State::Running || m_state == State::Starting)
        return;

    m_process = new QProcess(this);

    connect(m_process, &QProcess::started, this, [this]() {
        setState(State::Running);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        Q_UNUSED(exitCode);
        setState(State::Finished);
        m_process->deleteLater();
        m_process = nullptr;
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setState(State::Finished);
        m_process->deleteLater();
        m_process = nullptr;
    });

    QStringList args;
    args << QStringLiteral("/v:%1:%2").arg(host).arg(port);
    args << QStringLiteral("/u:%1").arg(username);
    args << QStringLiteral("/p:%1").arg(password);
    args << QStringLiteral("/parent-window:%1").arg(reinterpret_cast<qlonglong>(parentHwnd));
    args << QStringLiteral("+clipboard");
    args << QStringLiteral("/cert:ignore");

    setState(State::Starting);
    m_process->start(exePath, args);
}

void FreeRdpProcess::stop()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    setState(State::Finished);
}

void FreeRdpProcess::setState(State newState)
{
    if (m_state == newState)
        return;
    m_state = newState;
    emit stateChanged(newState);
}
```

- [ ] **Step 3: Build to verify compilation**

Run:
```bash
cmake --build --preset msvc-debug
```

Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/rdp/FreeRdpProcess.h src/rdp/FreeRdpProcess.cpp
git commit -m "feat: add FreeRdpProcess for wfreerdp.exe subprocess management"
```

---

### Task 3: RdpSessionWidget

**Files:**
- Create: `src/rdp/RdpSessionWidget.h`
- Create: `src/rdp/RdpSessionWidget.cpp`

This is the core embedding widget. It owns a FreeRdpProcess, handles resize by resizing the wfreerdp child window via Win32 `MoveWindow`, and restores focus via `SetFocus` on the child window.

- [ ] **Step 1: Create `src/rdp/RdpSessionWidget.h`**

```cpp
#pragma once

#include <QWidget>
#include "FreeRdpProcess.h"

class QLabel;

class RdpSessionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RdpSessionWidget(QWidget *parent = nullptr);
    ~RdpSessionWidget();

    void connectToHost(const QString &exePath,
                       const QString &host,
                       int port,
                       const QString &username,
                       const QString &password);

signals:
    void titleStateChanged(FreeRdpProcess::State state);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    void onStateChanged(FreeRdpProcess::State state);
    HWND findChildWindow() const;
    void resizeChildWindow();
    void showOverlay(const QString &text);

    FreeRdpProcess *m_process = nullptr;
    QLabel *m_overlay = nullptr;
    HWND m_childWindow = nullptr;
};
```

- [ ] **Step 2: Create `src/rdp/RdpSessionWidget.cpp`**

```cpp
#include "RdpSessionWidget.h"

#include <QLabel>
#include <QResizeEvent>
#include <QTimer>

#include <windows.h>

RdpSessionWidget::RdpSessionWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
}

RdpSessionWidget::~RdpSessionWidget()
{
    if (m_process)
        m_process->stop();
}

void RdpSessionWidget::connectToHost(const QString &exePath,
                                      const QString &host,
                                      int port,
                                      const QString &username,
                                      const QString &password)
{
    if (m_process)
        m_process->stop();

    m_childWindow = nullptr;
    m_process = new FreeRdpProcess(this);

    connect(m_process, &FreeRdpProcess::stateChanged,
            this, &RdpSessionWidget::onStateChanged);

    m_process->start(exePath, host, port, username, password, winId());

    showOverlay("Connecting...");
}

void RdpSessionWidget::onStateChanged(FreeRdpProcess::State state)
{
    emit titleStateChanged(state);

    switch (state) {
    case FreeRdpProcess::State::Running:
        // wfreerdp creates its child window shortly after start.
        // Poll for it with a short delay.
        QTimer::singleShot(500, this, [this]() {
            m_childWindow = findChildWindow();
            if (m_childWindow) {
                resizeChildWindow();
                SetFocus(m_childWindow);
            }
        });
        delete m_overlay;
        m_overlay = nullptr;
        break;
    case FreeRdpProcess::State::Finished:
        m_childWindow = nullptr;
        showOverlay("Disconnected");
        break;
    default:
        break;
    }
}

HWND RdpSessionWidget::findChildWindow() const
{
    HWND result = nullptr;
    EnumChildWindows(reinterpret_cast<HWND>(winId()),
        [](HWND child, LPARAM lParam) -> BOOL {
            *reinterpret_cast<HWND*>(lParam) = child;
            return FALSE; // take the first child
        }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void RdpSessionWidget::resizeChildWindow()
{
    if (!m_childWindow)
        return;
    MoveWindow(m_childWindow, 0, 0, width(), height(), TRUE);
}

void RdpSessionWidget::showOverlay(const QString &text)
{
    if (!m_overlay) {
        m_overlay = new QLabel(text, this);
        m_overlay->setAlignment(Qt::AlignCenter);
        m_overlay->setStyleSheet(
            "QLabel { background: #1e1e1e; color: #cccccc; font-size: 18px; }");
    } else {
        m_overlay->setText(text);
    }
    m_overlay->setGeometry(0, 0, width(), height());
    m_overlay->show();
    m_overlay->raise();
}

void RdpSessionWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    resizeChildWindow();
    if (m_overlay)
        m_overlay->setGeometry(0, 0, width(), height());
}

void RdpSessionWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    if (m_childWindow)
        SetFocus(m_childWindow);
}
```

- [ ] **Step 3: Build to verify compilation**

Run:
```bash
cmake --build --preset msvc-debug
```

Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/rdp/RdpSessionWidget.h src/rdp/RdpSessionWidget.cpp
git commit -m "feat: add RdpSessionWidget with HWND embedding and focus handling"
```

---

### Task 4: MainWindow Integration

**Files:**
- Modify: `src/MainWindow.cpp`
- Modify: `src/MainWindow.h`

Wire MainWindow to launch a connection on startup with hardcoded parameters.

- [ ] **Step 1: Update `src/MainWindow.h`**

Replace entire file:

```cpp
#pragma once

#include <QMainWindow>
#include "rdp/FreeRdpProcess.h"

class RdpSessionWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateTitle(FreeRdpProcess::State state);

    RdpSessionWidget *m_sessionWidget;
};
```

- [ ] **Step 2: Update `src/MainWindow.cpp`**

Replace entire file:

```cpp
#include "MainWindow.h"
#include "rdp/RdpSessionWidget.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_sessionWidget(new RdpSessionWidget(this))
{
    setWindowTitle("RdpBox - POC");
    setCentralWidget(m_sessionWidget);
    resize(1280, 800);

    // --- Hardcoded connection parameters (change these for your environment) ---
    const QString host = "127.0.0.1";
    const int port = 3389;
    const QString username = "administrator";
    const QString password = "";
    // ---------------------------------------------------------------------------

    const QString exePath = QCoreApplication::applicationDirPath()
                            + "/../../tools/wfreerdp.exe";
    m_sessionWidget->connectToHost(exePath, host, port, username, password);

    connect(m_sessionWidget, &RdpSessionWidget::titleStateChanged,
            this, &MainWindow::updateTitle);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}

void MainWindow::updateTitle(FreeRdpProcess::State state)
{
    QString suffix;
    switch (state) {
    case FreeRdpProcess::State::Idle:     suffix = "Idle"; break;
    case FreeRdpProcess::State::Starting: suffix = "Connecting..."; break;
    case FreeRdpProcess::State::Running:  suffix = "Connected"; break;
    case FreeRdpProcess::State::Finished: suffix = "Disconnected"; break;
    }
    setWindowTitle("RdpBox - " + suffix);
}
```

- [ ] **Step 3: Build**

Run:
```bash
cmake --build --preset msvc-debug
```

Expected: Compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: wire MainWindow to auto-connect on startup"
```

---

### Task 5: Build & Manual Verification

This task requires manually downloading wfreerdp.exe and verifying all four POC criteria.

- [ ] **Step 1: Download wfreerdp.exe**

Download the latest FreeRDP Windows binary from GitHub Releases:
```
https://github.com/FreeRDP/FreeRDP/releases
```

Extract `wfreerdp.exe` and place it at:
```
RdpBox/tools/wfreerdp.exe
```

Note: The FreeRDP release zip contains `wfreerdp.exe` along with DLLs. Place all files from the zip into `tools/`.

- [ ] **Step 2: Configure and build**

Run:
```bash
cmake --preset msvc-debug
cmake --build --preset msvc-debug
```

- [ ] **Step 3: Verify window embedding**

Launch `build/msvc-debug/RdpBox.exe`.

- Update the hardcoded host/username/password in `src/MainWindow.cpp` to point to a real RDP server.
- Rebuild and run.
- **Pass:** wfreerdp connects and the remote desktop renders inside the Qt window, not as a separate window.

- [ ] **Step 4: Verify keyboard/mouse input**

While connected:
- Click on the remote desktop area and type — keys should appear on the remote session.
- Alt-Tab away and back, then click the remote desktop — focus should restore and keys should work.

- [ ] **Step 5: Verify window resize**

- Drag the window edges to resize.
- **Pass:** The remote desktop area scales or updates to match the new widget size.

- [ ] **Step 6: Verify disconnect handling**

- Close the RDP session from the server side, or wait for a timeout.
- **Pass:** The application shows "Disconnected" overlay and does not crash.

- [ ] **Step 7: Commit tools/ placeholder (gitignore the binary)**

Add a `.gitkeep` so the tools directory is tracked, but ignore the binary:

```bash
echo "wfreerdp*" > tools/.gitignore
touch tools/.gitkeep
git add tools/.gitignore tools/.gitkeep
git commit -m "chore: add tools/ directory placeholder for wfreerdp.exe"
```

---

## Spec Coverage Check

| Spec Requirement | Task |
|-----------------|------|
| wfreerdp.exe subprocess via QProcess | Task 2 (FreeRdpProcess) |
| /parent-window embedding | Task 2 (start args), Task 3 (findChildWindow) |
| Keyboard/mouse input | Task 3 (focusInEvent + SetFocus) |
| Resize handling | Task 3 (resizeEvent + MoveWindow) |
| Disconnect detection | Task 2 (QProcess::finished), Task 3 (onStateChanged overlay) |
| Hardcoded connection | Task 4 (MainWindow constructor) |
| Status in title bar | Task 4 (updateTitle) |
| tools/wfreerdp.exe location | Task 5 (download + gitignore) |
