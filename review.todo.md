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

- [x] **P1-2 Qt 版未初始化密码保护模式，密码被静默丢弃**（PR #3 已合）
  - 位置：`src/qtui/QtMain.cpp`（缺 `PasswordProtection::setMode`）、`src/common/profiles/ProfileRepository.cpp:53-90`
  - 现象：Qt 入口从不决定 portable/DPAPI 模式 → 非 Windows 下 `g_portableKey` 为空，
    密码静默丢弃；Windows 下 `--portable` 被解析却仍用 DPAPI 加密（换机失效）。
  - 修法：初始化抽到 `common/PasswordProtectionStartup`，MFC/Qt 共用，且两个入口都**先应用
    `--portable`（flag 落地）再选模式**；`initialize()` 按 `AppPaths::isPortableMode()` 选模式，
    `#ifndef _WIN32` 直接 Portable（无 DPAPI）。
    `ProfileRepository::persistWarningNeeded()` 把「保存成功但密码被丢」也报出来。
  - 顺序为何关键：先选模式后应用 flag 时，首启 `--portable` 仍取 DPAPI → 密码加密进 exe 目录的
    profiles.json（换机失效），且每次 `save()` 把 `portableMode` 写回 false 与 flag 拉锯；
    `dataRootPath()` 同样依赖 flag 已落地。挪顺序后「模式 == 路径 == flag」，拉锯自然消失。

- [ ] **P1-3 Qt 连接结束后焦点丢失**
  - 位置：`src/qtui/QtRdpSessionWidget.cpp:611-627`（`Finished` 分支不恢复焦点）、`:875-878`（`canCaptureSystemKeys`）
  - 现象：断线后 `Finished` 时无人 `setFocus`，`canCaptureSystemKeys()` 要求 `hasFocus()`，
    回到标签页必须再点一次才能捕获 Win/Ctrl+Esc 之类的键。
  - 修法：`Finished` 分支里若 `isVisible()` 则 `setFocus`，并同步 `clearKeyboardTarget`。

- [x] **P1-4 Qt 鼠标捕获缺少异常路径补发**（PR #6：`WindowDeactivate` / `Hide` 也补发释放）
  - 位置：`src/qtui/QtRdpSessionWidget.cpp:858-873`（`grabMouse`/`releaseMouse`）
  - 现象：MFC 侧在 `ReleaseCapture` 后会补发按键释放（`src/common/rdp/RdpSessionViewInput.cpp:141-152`），
    Qt 只处理 `QEvent::UngrabMouse`；拖窗口/切标签时远端收不到 mouse-up，按钮卡在按下态。
  - 修法：`UngrabMouse`、`deactivateWindow`、`hide` 统一走 `releasePressedMouseButtons()`。

- [ ] **P1-5 非 Windows 下仍提示可更新**
  - 位置：`src/common/UpdateClient.cpp:595-615`（平台 stub 恒 false）、`src/qtui/QtMainWindow.cpp:1882-1925`
  - 现象：Linux 构建照样显示「有新版本」，点击后下载/校验直接失败，只弹错误。
  - 修法：`fetchLatestRelease` 阶段就按平台过滤 asset，无匹配不进入「可更新」状态。

## P2 重复代码与架构

- [~] **P2-1 会话状态机在 MFC/Qt 各写一份**（1a 已落地 2026-10-26 PR #11，CodeBuddy 评审通过）
  - **P2-1a 完成**：Qt 采纳 `RdpSessionMouseState`（吞掉私有按钮位图+coalescer+last point，净 -40 行）；
    两侧帧闸门去摊平为单个 `FrameGateState` 成员；新增 `resetPendingMove()` 解决 Qt 排队回调下
    全量 reset 抹掉已记录按下的丢 release 风险（CodeBuddy 评审发现）；补 `RdpSessionMouseStateTests`
    （基线 53→54）。
  - **P2-1b 待做**：真 `SessionConnectionState` 状态机抽取（帧闸门生命周期+resize burst+恢复相位+
    overlay 决策），视图只留定时器/事件 API 薄壳。

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

- [x] **P2-4 `common/` 里混进 MFC 视图**（2026-10-25 PR #9 合并，CodeBuddy 评审通过）
  - 13 个 MFC 视图私有文件迁 `src/mfcui/session/`（RdpSessionView 全家 + FocusNotification +
    ProcessEventBehavior/Queue + ResumeRecovery；判定依据=qtui 零引用且 FreeRdpProcess 不依赖）。
  - 双白名单消灭：`RDPBOX_COMMON_SOURCES`(65) + `COMMON_WIN32_SOURCES`(9) + 各 UI 文件，
    REMOVE_ITEM hack 删除；新旧多重集逐元素等价（Qt/Win 87、MFC 130、Qt/Linux 78）。
  - `common/` 现 afx/CWnd/CString 零命中。
  - 遗留：`RdpWinKeyCodes.h`、`common/ui/MainWindowActivation.h`、`mfcui/MainWindowStatePersistence.h`
    三个头从未进任何列表（base 上就有），待顺手清理。
- [x] **假绿通道堵漏**（2026-10-25 PR #10）：PR #8 引入两处测试编译错误
  （不存在的 `FreeRdpProcess::State::Connecting`、`FILEDESCRIPTORW` 缺 `<shlobj.h>`），
  双方验证均漏网——CodeBuddy 容器无 MSVC，我方 verify 只编 `RdpBoxLegacy` 就跑 ctest
  （陈旧二进制 53/53），GitHub Windows CI 在 1b372e1 红 1m56s 未被查看。
  verify 脚本已改为全量编 `msvc-debug-test`；**合并后必须查 CI 状态**。

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

- [x] **P3-7 便携模式写 flag 失败静默回落**（Qt 侧 PR #3；MFC 侧 PR #6）
  - 位置：`src/common/AppPaths.cpp:250`、`src/qtui/QtMain.cpp:36`
  - 现象：`enablePortableMode()` 返回 false 时 `isPortableMode()` 读不到 flag，
    静默回落 APPDATA，两个入口都丢弃了返回值。
  - 修法：`AppPaths::portableModeEnableFailed()` 记录失败，Qt 启动弹一次可见告警。
  - MFC 侧（PR #6）：`CRdpBoxApp::InitInstance()` 在窗口显示后按
    `portableModeEnableFailed()` 弹一次 `MessageBox`，路径文案改用新增的
    `AppPaths::executableDirectory()`（Qt 侧也换成它，不再自己拼 `QFileInfo`）。
  - 已顺带修好的一半：`dataRootPath()` 不再需要「flag 另找时机落地」——两个入口都在选模式前
    应用 flag，所以路径与密码模式始终来自同一个 flag。

- [x] **P3-3 `wcscpy_s` 截断走 invalid parameter handler**（PR #4 已合）
  - 位置：`src/common/rdp/WindowsClipboardBackend.cpp:450`
  - 现象：路径超长时 `wcscpy_s` 触发 invalid parameter handler，Release 下直接终止进程。
  - 修法：换 `StringCchCopyW` 并检查返回值；同时校验 `pathLength < wcslen(fileName)`。

- [x] **P3-4 Windows Qt UI 从没进过 CI**（PR #6）
  - 位置：`.github/workflows/windows.yml`、`.github/workflows/release.yml`、`CMakePresets.json`
  - 现象：两处 CI 都显式 `-DRDPBOX_BUILD_QT=OFF`，presets 里也没有 Qt on Windows 的组合，
    `qtui/` 的 Windows 分支（`FramelessWin.h`、`SetWindowsHookEx`）等于无编译验证。
  - 已做：新增 `msvc-release-qt` preset（`inherits: msvc-base`，Qt ON / Legacy OFF）；
    CI 编译 `RdpBox`，配置阶段加 `CMAKE_AUTOCONFIRM=1`（依赖缺失时失败而不是挂住）。
    （2026-10-24 修正：RdpBox 是 Qt5 应用，依赖实为 `qt5-base qt5-svg`——原 `qtbase` 是 Qt6、
    从未编译过；CI 现从 kkzi/qt-static 预编译 bundle 取 Qt，不再源码编译。）
  - 已验证（2026-10-24）：windows.yml 与 release.yml 多轮全绿，Qt release 目标每次 CI 都编；
    freerdp 头文件已改为无版本前缀 include（3aa9c57），兼容现行 vcpkg 布局。

- [x] **P3-6 `stop()` 无超时 join，断网时整个窗口卡死**（PR #6）
  - 位置：`src/common/rdp/FreeRdpProcess.cpp`（`stop()`）
  - 现象：`join()` 无条件等待；`freerdp_disconnect()` 卡住（挂起的服务端、半开 TCP）时，
    UI 线程上的 `stop()`（析构、切标签、休眠恢复都在 UI 线程）会一直阻塞。
  - 已做：一次连接的状态搬进 `FreeRdpBackend`，`worker` 只持有它；`stop()` 最多等 5s
    `Abandoned::released`，仍未置位就放弃这次会话。放弃之后：
    - 输入/尺寸请求改为排队，由 worker 在线程上应用，UI 线程不再直接调 freerdp
    - `NativeRdpContext::owner` 的每次解引用先过 `AliveFlag`
    - freerdp context 由 worker 出口唯一释放，`stop()` 在确认 worker 已放手后才释放自己那份
  - 已验证（2026-10-24）：本机 freerdp 3.19 与 CI freerdp 3.32.1 双侧 53/53 ctest
    （含 FreeRdpStopSmoke：start→快速失败→析构 stop()，三轮 exit 0，修前 0xC0000005）。

- [x] **P3-9 Qt 会话路径的鼠标与重连路径**（PR #6，`main` 上原本没有这条）
  - 位置：`src/qtui/QtRdpSessionWidget.cpp`（`updateState` / `mouseMoveEvent` / `mousePressEvent`）
  - 现象：
    1. 断线后 `Finished` 分支不清帧闸门，晚到帧走 `frameArrivalDecision` 的 `hideOverlay`，
       把「已断开」覆盖文字一起清掉，只剩黑屏
    2. Qt 不在 press 前合成 move，单击按服务端旧坐标派发
    3. 非焦点时 hover 仍喂 `RdpMouseMoveCoalescer`，它会吞掉焦点后的第一次真实移动
  - 已做：`Finished` 先清闸门并保留最后一帧；press 前补一次 `sendMouseMove`；hover 只在该 pane
    持有焦点时才转发（`shouldForwardMouseMove()`）；`WindowDeactivate` / `Hide` 也走
    `releasePressedMouseButtons()`（QEvent::UngrabMouse 不是唯一会丢抓取的路径）。
  - 未验证：Qt GUI 行为要真窗口手测（容器无 Windows）——并入 GUI 手测挂账
    （断网保留末帧 / 失焦点击落点 / 断开立连不闪 / 关标签不卡，用户暂缓）。

- [ ] **P3-5 崩溃面几乎没有测试覆盖**
  - 位置：`tests/`（21 个目标全是纯逻辑）、`tests/session/SessionManagerIntegrationTests.cpp`（用 Fake）
  - 现象：P0-1/P0-2/P3-2 这些点正好都在无测试区。
  - 修法：先给 `WindowsClipboardBehavior::fileGroupDescriptorByteSize`（已存在但边界没测）
    和 `RdpProcessEventQueueBehavior` 的 generation 过滤补用例；P0 修复再各配一条回归测试。

## P1-2 修复的复查（PR #3，修复后二次走查）

- `QtMainWindow::warnProfilePersistFailed()`（`src/qtui/QtMainWindow.cpp:2333`）条件被写成
  `persistWarningNeeded() || shown` 即提前返回，告警永不触发，逻辑与 `saveFailed()` 相反 —— 已改回 `!`。
- `ProfileRepository::persistWarningNeeded()` 新增的 `droppedPasswordCount()` 实际查不到「密码被丢」：
  `to_json` 从不写 `password` 明文键，写盘后每个条目必有 `passwordPortable`/`passwordProtected`，
  该函数恒为 0（同时读的是 `root["profiles"]` 这棵序列化后的树，不是内存状态）。
  已改为按内存配置判：`PasswordProtection::isReady()` 为假且某条配置密码非空 → 才算丢弃。
- 原测试里「断言 `persistWarningNeeded()` 为真」那条恒失败（且被 `setMode` 之外的时机掩盖），已换成
  「已就绪模式写入后 `passwordProtected` 存在、重载能读回明文、不报警」。
- 说明：保护模式未就绪（`setMode` 从没调过）在 Linux 侧无法用现有 API 构造，这条分支目前只有
  `isReady()` 判断做兜底，没有直接单测。

## 核实结论（基于 `main@72f275e`，FreeRDP 3.19.0 源码比对）

- **P0-1 ~ P0-4 全部不成立**：`Pointer_Alloc` 用 `calloc` 清零派生尾部；libfreerdp 3.19
  全库无 `gdi_free` 调用（纯客户端职责）；`gdi_init`/`gdi_free` 都在 worker 线程；
  MFC 默认 `PostNcDestroy` 即 `delete this`。P0-3 残余的「`stop()` 无超时 join」单列为 P3-6。
- **P1-1 / P1-3 / P1-4 / P1-5 不成立**：`PreTranslateMessage` 先于视图 `WindowProc`；
  `handleBecameVisible()` 已有 `setFocus`；`focusOutEvent` 已做三段释放；平台 stub 恒 false。
- **P1-2 成立（唯一必修，已修）**。
- **P2 全部属实**，属结构性重构，择机做。
- **P3-2 加重**：`clipboard->fileDescriptors[i]` 的 null 分支是结构性死代码
  （`addToFileArrays` 失败即整体 `return -1`），建议换成 `WINPR_ASSERT`。
- **P3-5 收窄**：真正零覆盖的是 `fileGroupDescriptorByteSize` 的溢出钳制分支。
- **P3-8 新增**：49 个测试目标各自链接一遍 MFC/FreeRDP，CI 墙钟被白白拉长。

## 建议顺序

1. ~~P1-2 + P3-7~~（密码保护初始化与便携模式失败可见性，PR #3）
2. ~~P3-3~~（`StringCchCopyW`，PR #4）
3. ~~P3-4 + P3-6 + P3-9~~（PR #6；PR #5 因 P1 未合，见其条目备注）
4. 待做：P1-2 / P3-7 的 Windows 手工验证（`--portable` 首启、DPAPI 换机）
5. 待做：P0-1/P0-2 的结论只绑定 freerdp 3.19.0，换版本要复审
   （2026-10-24 已触发：CI/发布二进制现在用 freerdp 3.32.1。53 个 ctest 含 StopSmoke
   已在 3.32.1 上全绿，部分对冲；`libfreerdp 全库无 gdi_free 调用` 这条结论仍未复审。）
6. 结构性重构：P2-4 → P2-1 → P2-2（先搬目录再抽公共逻辑，否则抽完还得搬）
