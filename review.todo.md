# RdpBox 代码评审待办

> 来源：Issue #1 的全量走查。每条 = 一个可独立提交的改动。
> 路径相对仓库根目录，行号取自 `main@72f275e`，改动后可能漂移。
> 优先级：`P0` 崩溃/UB 盲区 → `P1` 功能失效 → `P2` 重复代码/架构 → `P3` 可维护性。

## P0 崩溃与未定义行为

- [ ] **P0-1 点阵指针跨作用域泄漏**
  - 位置：`src/common/rdp/FreeRdpProcessNative.cpp:186-200`（`nativefreerdp_register_pointer`）
  - 现象：`rdpPointer pointer` 是栈变量，`graphics_register_pointer` 会按 `pointer.size`
    （写成 `sizeof(NativePointer)`）整体拷贝模板，Free 回调里 `toNativePointer(pointer)->cursor`
    读到的 `CursorInfo` 从未初始化：`destroyCursorInfo` 会拿到随机 HANDLE/堆指针。
  - 修法：模板改为堆分配并在 `nativefreerdp_client_free` 释放，或在 `pointer_new` 里首次
    `std::memset` 掉 `NativePointer` 的尾部。补一条 `Pointer_Alloc` 路径的单测。

- [ ] **P0-2 `gdi_free` 与 FreeRDP 生命周期重复释放**
  - 位置：`src/common/rdp/FreeRdpProcessNative.cpp:403`（`gdi_init`）、`:426`（`post_disconnect` 里 `gdi_free`）
  - 现象：上游 3.x 在 `post_disconnect` 之后还有 `freerdp_client_stop` → `rdp_free` → `update_free`，
    这条链会再次释放 GDI 相关资源；自行 `gdi_free` 使 `context->gdi` 置空后，后续 update 侧仍持有旧指针。
  - 修法：明确唯一释放点——要么只保留 `post_disconnect`（与 X11/Windows 客户端一致），
    要么挪到 `freerdp_client_context_free`（`src/common/rdp/FreeRdpProcess.cpp:294`）之前，
    不能两处都留；改完在 freerdp 3.5 与 3.9 上各跑一次 连接→断开→重连，
    用 Application Verifier 看有没有 double free。

- [ ] **P0-3 连接线程与 UI 线程职责错位**
  - 位置：`src/common/rdp/FreeRdpProcess.cpp:200-300`、`src/qtui/QtRdpSessionWidget.cpp:557-582`
  - 现象：`gdi_init` 在 UI 线程的 `start()` 里调，真正的收帧/`gdi_free` 在 worker 线程和 UI 线程之间
    交叉（`stop()` 由 `~QtRdpSessionWidget:242`、`handleHostResume:294` 触发，都在 UI 线程；
    MFC 侧同理走 `RdpSessionView::destroy`）。
  - 修法：把 `gdi_init` 挪进 worker 线程入口（`FreeRdpProcess.cpp:211` 起），
    生命周期与 `freerdp_connect`（`:241`）对齐；
    或给 `stop()` 加超时并在超时后放弃 join + 明确日志，避免断网时 UI 卡死。

- [ ] **P0-4 MFC 主窗口所有权不明确**
  - 位置：`src/mfcui/RdpBoxApp.cpp:67-75`、`src/mfcui/MainWindowSessions.cpp:125-136`
  - 现象：`new MainWindow()` 交给 `m_pMainWnd`（`MainWindow` 是否覆写 `PostNcDestroy` 要先确认，
    若无则 MFC 默认 `delete this`），`createShell()` 失败时又走 `delete frame`，
    `OnDestroy` 里还会 `m_sessionManager.reset()`——三处所有权交织，当前只靠判空侥幸不炸。
  - 前置：先确认 `MainWindow::PostNcDestroy` 有没有覆写。
  - 修法：明确单一所有权——`PostNcDestroy` 里 `delete this` 并把 `m_pMainWnd` 置空，
    `RdpBoxApp` 不再持有裸指针。

## P1 功能失效

- [ ] **P1-1 MFC 下 Ctrl+N / Ctrl+P 彻底丢失**
  - 位置：`src/common/rdp/RdpSessionViewMessages.cpp:18-40`（`handleWindowKeyMessage`）、
    `src/common/ui/MainWindowShortcuts.cpp`（`isReservedMainWindowShortcut`）、
    `src/mfcui/MainWindow.cpp:155-175`（`PreTranslateMessage`）
  - 现象：按下 local-only 快捷键时 `noteConsumedLocalShortcutKey` 记账并 `PassThrough`，
    但 `WindowProc` 对 keydown 仍返回 0（吞掉），而 MFC 处理快捷键的 `PreTranslateMessage`
    只在消息未被吞时执行 → 远端收不到，本地也不响应。
  - 修法：local-only 快捷键的 keydown/keyup 直接返回 `NotHandled`，让消息回到 MFC 泵。
    Qt 侧的 `ShortcutOverride`（`QtRdpSessionWidget.cpp:329`）行为是对的，可对照。

- [ ] **P1-2 Qt 版未初始化密码保护模式，密码被静默丢弃**
  - 位置：`src/qtui/QtMain.cpp`（缺 `PasswordProtection::setMode`）、`src/common/profiles/ProfileRepository.cpp:53-90`
  - 现象：Qt 入口从不决定 portable/DPAPI 模式，默认模式在非 Windows 下走到
    `protectPortable`，而 portable key 未初始化 → 返回空串，密码存不住、`unprotect` 恒失败。
    MFC 侧有 `ensurePasswordProtectionReady()`（`src/mfcui/RdpBoxApp.cpp:30`）。
  - 修法：Qt 启动时按 `--portable` 与平台调用同一个初始化函数（抽到 `common/`），
    并补 `ProfileRepository` 的「空密码不得静默写盘」断言。

- [ ] **P1-3 Qt 连接结束后焦点丢失**
  - 位置：`src/qtui/QtRdpSessionWidget.cpp:611-627`（`Finished` 分支不恢复焦点）、`:875-878`（`canCaptureSystemKeys`）
  - 现象：断线后 `Finished` 时无人 `setFocus`，`canCaptureSystemKeys()` 要求 `hasFocus()`，
    回到标签页必须再点一次才能捕获 Win/Ctrl+Esc 之类的键。
  - 修法：`Finished` 分支里若 `isVisible()` 则 `setFocus`，并同步 `clearKeyboardTarget`。

- [ ] **P1-4 Qt 鼠标捕获缺少异常路径补发**
  - 位置：`src/qtui/QtRdpSessionWidget.cpp:858-873`（`grabMouse`/`releaseMouse`）
  - 现象：MFC 侧在 `ReleaseCapture` 后会补发按键释放（`src/common/rdp/RdpSessionViewInput.cpp:141-152`），
    Qt 只处理 `QEvent::UngrabMouse`；拖窗口/切标签时远端收不到 mouse-up，按钮卡在按下态。
  - 修法：`UngrabMouse`、`deactivateWindow`、`hide` 统一走 `releasePressedMouseButtons()`。

- [ ] **P1-5 非 Windows 下仍提示可更新**
  - 位置：`src/common/UpdateClient.cpp:595-615`（平台 stub 恒 false）、`src/qtui/QtMainWindow.cpp:1882-1925`
  - 现象：Linux 构建照样显示「有新版本」，点击后下载/校验直接失败，只弹错误。
  - 修法：`fetchLatestRelease` 阶段就按平台过滤 asset，无匹配不进入「可更新」状态。

## P2 重复代码与架构

- [ ] **P2-1 会话状态机在 MFC/Qt 各写一份**
  - 位置：`src/common/rdp/RdpSessionView.cpp`（752 行）、`src/qtui/QtRdpSessionWidget.cpp`（1049 行）
  - 现象：`FrameGate`、`ResizeBurst`、`RecoveryTimer`、`MouseMoveCoalescer`、
    `pressedMouseButtons`、`releaseAllPressedKeys` 逐行相似，只有定时器 API 不同。
  - 修法：抽 `SessionConnectionState`（纯逻辑，`RdpSessionViewBehavior.h` 已有一半）+
    `IMouseButtonTracker`，两侧只留薄壳。

- [ ] **P2-2 Qt 完全绕过 `SessionManager`**
  - 位置：`src/qtui/QtMainWindow.cpp:2248-2320`
  - 现象：`m_tabs` + `m_tabBar` + `m_sessionStates` 三份平行索引，靠线性查找同步；
    `migrateSessionIdentity`（`:2304`）与 `sessionTabIndexForProfileName` 都是重复劳动。
  - 修法：让 Qt 也实现 `ISessionTabs/ISessionHost`，复用 `mfcui/session/` 里已经抽好的逻辑
    （目录需要先按 P2-4 搬家）。

- [ ] **P2-3 键盘物理状态构造写了三份**
  - 位置：`src/common/rdp/RdpSessionView.cpp:73-108`、`src/common/rdp/RdpSessionKeyboardHook.cpp:44-61`、
    `src/qtui/QtRdpSessionWidget.cpp:117-166`（三份 `isVirtualKeyPhysicallyDown` + 修饰键 + 左右键判定）
  - 修法：收敛成一份 `BuildPhysicalKeyboardState()` 放 `common/rdp/`（非 Windows 下返回空状态），
    其余调用方改成直接引用它。

- [ ] **P2-4 `common/` 里混进 MFC 视图**
  - 位置：`src/common/rdp/RdpSessionView.h`（`#include <afxwin.h>`）、`src/CMakeLists.txt:20-23`
  - 现象：`common/` 不是「无 UI 框架依赖」的，导致 CMake 必须维护两份源文件白名单，
    两边已开始漂移（Qt 白名单缺 `WindowsClipboardBackend*`、`RdpFocusNotification*` 等）。
  - 修法：MFC 视图挪到 `mfcui/session/`，`common/` 只留 `RdpKeyboardInputRouter` 这类纯逻辑。

- [ ] **P2-5 头文件自包含与重复 include**
  - 位置：`src/common/ConnectionLaunchArgs.cpp:1-3`（自己 include 自己两次）、
    `src/common/rdp/RdpKeyboardInputRouter.h:3-13`（`NativeTypes.h` include 两次）
  - 修法：清掉，顺手开一次 `/showIncludes` 或 IWYU 复查。

## P3 可维护性与工程化

- [ ] **P3-1 剪贴板后端单文件 1168 行**
  - 位置：`src/common/rdp/WindowsClipboardBackend.cpp`（format map、文件描述符组包、
    OLE 回调、线程管理、注册表查询全在一起）
  - 修法：至少把「文件描述符构建」（`:672-700`）和「format map」拆成独立文件；
    `onServerFileContentsRequest`（`:902-980`）的 4 层嵌套改成早退。

- [ ] **P3-2 剪贴板文件列表的计数与越界**
  - 位置：`src/common/rdp/WindowsClipboardBackend.cpp:680-694`、`:959-975`
  - 现象：`cItems` 按 `fileCount` 写，但循环里遇到空 descriptor 就跳过 → 资源管理器读到未初始化项；
    `request->listIndex` 在 `:928` 先无条件写进 format，只有失败回退分支才判越界。
  - 修法：构建阶段维护真实的非空计数；`listIndex` 在使用前统一校验一次。

- [ ] **P3-3 `wcscpy_s` 截断走 invalid parameter handler**
  - 位置：`src/common/rdp/WindowsClipboardBackend.cpp:450`
  - 现象：路径超长时 `wcscpy_s` 触发 invalid parameter handler，Release 下直接终止进程。
  - 修法：换 `StringCchCopyW` 并检查返回值；同时校验 `pathLength < wcslen(fileName)`。

- [ ] **P3-4 Windows Qt UI 从没进过 CI**
  - 位置：`.github/workflows/windows.yml:121-133`、`.github/workflows/release.yml:141-153`、
    `CMakePresets.json`
  - 现象：两处 CI 都显式 `-DRDPBOX_BUILD_QT=OFF`，presets 里也没有 Qt on Windows 的组合，
    `qtui/` 的 Windows 分支（`FramelessWin.h`、`SetWindowsHookEx`）等于无编译验证；
    `.github/` 是 GitHub Actions，CNB 侧没有 `.cnb.yml`。
  - 修法：加 `msvc-release-qt` preset（vcpkg 加 `qtbase`/`qtsvg`），CI 至少编译一次；
    CNB 上补一份等价流水线。

- [ ] **P3-5 崩溃面完全没有测试覆盖**
  - 位置：`tests/`（21 个目标全是纯逻辑）、`tests/session/SessionManagerIntegrationTests.cpp`（用 Fake）
  - 现象：P0-1/P0-2/P3-2 这些点正好都在无测试区。
  - 修法：先给 `WindowsClipboardBehavior::fileGroupDescriptorByteSize`（已存在但边界没测）
    和 `RdpProcessEventQueueBehavior` 的 generation 过滤补用例；P0 修复再各配一条回归测试。

## 建议顺序

1. P0-1 → P0-2（改动小、风险最高，各配一条回归测试）
2. P0-4 → P0-3（所有权与线程归属，改动中等）
3. P1-1、P1-2（用户能直接感知的功能断崖）
4. P2-4 → P2-1 → P2-2（先搬目录再抽公共逻辑，否则抽完还得搬）
5. P3-4 尽早做，让 Qt Windows 分支有编译兜底
