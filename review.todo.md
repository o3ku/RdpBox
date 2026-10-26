# RdpBox 代码评审待办

> 来源：Issue #1 的全量走查。每条 = 一个可独立提交的改动。
> 路径相对仓库根目录，行号取自 `main@72f275e`，改动后可能漂移。
> 优先级：`P0` 崩溃/UB 盲区 → `P1` 功能失效 → `P2` 重复代码/架构 → `P3` 可维护性。

## P0 崩溃与未定义行为

- [x] **P0-1 点阵指针跨作用域泄漏**（2026-10-24 复核证伪，2026-10-26 对码确认）
  - 位置：`src/common/rdp/FreeRdpProcessNative.cpp:186-200`（`nativefreerdp_register_pointer`）
  - 证伪机理（现行代码可证）：`pointer_free` 有 `cursorSerial == 0` 早退守卫——只有经过
    `pointer_new` 发放过序列号的指针才会被释放，而 `pointer_new` 在发放后**显式初始化** cursor
    （destroyCursorInfo(旧) + createCursor）。未经 New 的模板尾部垃圾到不了 destroyCursorInfo。
    （结论绑 3.19.0 的回调序已于 2026-10-26 在 3.32.1 上复审成立，见建议顺序第 5 条。）

- [x] **P0-2 `gdi_free` 与 FreeRDP 生命周期重复释放**（2026-10-24 复核，2026-10-26 对码确认已修）
  - 现行代码：`FreeRdpProcessNative.cpp:495-503` 唯一 `gdi_free`（worker 持有连接），`gdi_init`
    `:463-466` 每会话一次（重连可重跑 post_connect）。PR #6（79f6026）确立 freeContext() 单一
    锁护释放点：freerdp_client_stop → freerdp_client_context_free。FreeRdpStopSmoke（三轮
    start→fail→stop）53/53 持续在跑，对冲 double-free 面。（2026-10-26 已在 3.32.1 上复审成立，见建议顺序第 5 条。）

- [x] **P0-3 连接线程与 UI 线程职责错位**（2026-10-24 复核，2026-10-26 对码确认已修）
  - 现行代码：`gdi_init` 已在 worker 线程的 post_connect（`FreeRdpProcessNative.cpp:463-466`，
    注释明言「post_connect can run again on reconnect」）；PR #6（79f6026 "let the worker own the
    context end to end"）：Private::context 整删、freeContext() 单一释放点、stop() 不再直接触
    context 指针（abort 走 lockedContext()）。UI 线程不再参与 GDI 生命周期。

- [x] **P0-4 MFC 主窗口所有权不明确**（2026-10-24 复核证伪，2026-10-26 对码确认）
  - 前置确认结果：`MainWindow : CFrameWnd` **未覆写** PostNcDestroy → 走 CFrameWnd 默认
    （`delete this`），这是标准 MFC 所有权：`new MainWindow()` → `m_pMainWnd = frame` →
    关窗自动删除。`createShell()` 失败路径的 `delete frame` 发生在 `m_pMainWnd` 赋值**之前**
    （RdpBoxApp.cpp:66-68），无双重删除窗口。三处"交织"实为同一所有权的三个阶段，不是竞争。

## P1 功能失效

- [x] **P1-1 MFC 下 Ctrl+N / Ctrl+P 彻底丢失**（2026-10-24 复核证伪，2026-10-26 对码确认）
  - 证伪机理：原述**泵序反了**——MFC 消息泵是 `PreTranslateMessage` **先于** `WindowProc`
    （PumpMessage → PreTranslate → Translate/Dispatch → WindowProc）。WindowProc 里吞掉
    keydown 发生在 PreTranslate 已经返回之后，本地快捷键照常触发；`PassThrough` 只影响
    远端转发（本就该拦）。`RdpSessionViewMessages.cpp:28-31` 现行行为正确。
    （真机手测清单里保留一条 Ctrl+N/P 验证即可闭环。）

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

- [x] **P1-3 Qt 连接结束后焦点丢失**（2026-10-24 复核证伪，见「核实结论」区）
  - 证伪依据：`handleBecameVisible()` 已有 `setFocus`——回到标签页（visible 路径）自动恢复焦点，
    `Finished` 分支无需抢焦点；断线会话不抢键盘捕获是合理设计。真机若发现需点两次，重开此条。

- [x] **P1-4 Qt 鼠标捕获缺少异常路径补发**（PR #6：`WindowDeactivate` / `Hide` 也补发释放）
  - 位置：`src/qtui/QtRdpSessionWidget.cpp:858-873`（`grabMouse`/`releaseMouse`）
  - 现象：MFC 侧在 `ReleaseCapture` 后会补发按键释放（`src/common/rdp/RdpSessionViewInput.cpp:141-152`），
    Qt 只处理 `QEvent::UngrabMouse`；拖窗口/切标签时远端收不到 mouse-up，按钮卡在按下态。
  - 修法：`UngrabMouse`、`deactivateWindow`、`hide` 统一走 `releasePressedMouseButtons()`。

- [x] **P1-5 非 Windows 下仍提示可更新**（2026-10-24 复核证伪，2026-10-26 对码确认）
  - 证伪依据：`UpdateClient.cpp` 双实现——Win32 走 winhttp（:358），非 Win32 stub（:585）恒
    返回 false → Linux 构建根本进不了「有新版本」状态，原述的失败路径不可达。

## P2 重复代码与架构

- [x] **P2-1 会话状态机在 MFC/Qt 各写一份**（1a 落地 2026-10-26 PR #11；1b 勘察后裁定不做，2026-10-26）
  - **P2-1a 完成**：Qt 采纳 `RdpSessionMouseState`（吞掉私有按钮位图+coalescer+last point，净 -40 行）；
    两侧帧闸门去摊平为单个 `FrameGateState` 成员；新增 `resetPendingMove()` 解决 Qt 排队回调下
    全量 reset 抹掉已记录按下的丢 release 风险（CodeBuddy 评审发现）；补 `RdpSessionMouseStateTests`
    （基线 53→54）。
  - **P2-1b 裁定不做**（2026-10-26 勘察）：状态机的决策核心（闸门算术/overlay 文案/钳制/burst/恢复策略）
    已全部是共享纯类；剩余重复仅 ~40-60 行/侧的编排，且每处与框架调用交织（SetTimer/KillTimer vs
    QTimer、Invalidate vs update、CString vs QString）。抽 `SessionConnectionState`+定时器抽象层
    预计 +150~200 行接口/适配器去消 ~100 行重复——净增代码且加间接层，收益不抵。除非未来出现
    第三个前端或两侧编排再次漂移出真 bug，否则维持现状。

- [x] **P2-2 Qt 完全绕过 `SessionManager`**（2026-10-26 勘察后裁定不做接口采纳）
  - 勘察发现前提不成立：Qt 并没有复制 SessionManager 的生命周期策略——布局/可见性/换页由
    QTabWidget 原生提供（SessionManager 的存在恰因 MFC 没有这层）。三份索引中 `m_tabs`(QTabWidget)
    与 `m_tabBar`(自绘 BrowserTabBar) 是 **Qt 内部**平行索引，`m_sessionStates` 是纯表现层状态
    （tooltip/图标/RTT 着色），均不在 SessionManager 职责面内。
  - 真正两侧同形的策略只有 open 去重（~10 行）与改名身份迁移（~15 行），不值 4 个接口适配器 +
    64 处调用点改写 + 手工回归标签生命周期。
  - 若做，只做 Qt 内部简化：m_tabs/m_tabBar 双索引合一（QStackedWidget + BrowserTabBar），
    独立于本条，属 P3 级小单。
- [x] **P2-3 键盘物理状态构造写了三份**（2026-10-26 PR #12 合并，CodeBuddy 评审通过）
  - 三份文件私有 static（hook/MFC 视图/Qt 视图，且已漂移：MFC 份 modifiers 内联、无独立函数）统一为
    `common/rdp/RdpKeyboardPhysicalProbing.{h,cpp}` 的 `rdp::physical_keyboard`；`RdpKeyboardPhysicalState`
    结构拆叶子头共享。评审修正：签名 unsigned int、lossy 的 `physicalKeyboardModifiers()` 不进公共 API、
    Probing 头不拉 Router。净 -52 行。
  - 遗留（挂账）：`knowsNonModifierKeys`/`pressedVirtualKeys` 全仓只读不写（死字段，Router.cpp:52/396），
    建议加注释；Probing 非 Windows 空状态可编一个 #ifndef _WIN32 断言（随 P3-5）。

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

- [x] **P2-5 头文件自包含与重复 include**（6376060 已修；PR #8 header hygiene 一并收）
  - 对码确认：`ConnectionLaunchArgs.cpp` 自包含 include 只剩 1 处；`RdpKeyboardInputRouter.h`
    的 `NativeTypes.h` 只剩 1 处（P2-3 拆叶子头后由 `RdpKeyboardPhysicalState.h` 承接）。
    `/showIncludes`/IWYU 全面复查未做——真要收口挂 P3 待办。

## P3 可维护性与工程化

- [x] **P3-1 剪贴板后端单文件 1168 行**（2026-10-26 PR #13 合并，CodeBuddy 评审通过）
  - 主文件 1181→835 行：文件组包簇 → `WindowsClipboardBackendFiles.cpp`（286 行），format map →
    `WindowsClipboardBackendFormats.cpp`（85 行）；13 个搬家函数字节级一致（CodeBuddy AST 级比对）。
    `onServerFileContentsRequest` 的 OLE IStream 路径提取为早退 helper，回调拍平。
  - 评审确认 OLE 提取后 dataObject 先释放等价（证据链：STGMEDIUM 拥有流，DataObject.cpp:472 AddRef）。

- [x] **P3-2 剪贴板文件列表的计数与越界**（计数/越界半 2026-10-25 PR #8；目录静默空回半 2026-10-26 PR #14）
  - 计数（cItems vs 跳过项）：PR #8 已收——addToFileArrays 只在 descriptor 建成后递增 fileCount，
    循环内 WINPR_ASSERT 锁不变量。
  - listIndex 越界：PR #8 已收——进入 FormatEtc/回退数组前统一校验。
  - 目录型 listIndex 静默空回（PR #13 评审发现）：PR #14 已修——目录项在 OLE 尝试前直接回
    CB_RESPONSE_FAIL；判定排除 INVALID_FILE_ATTRIBUTES 通配；手测项（拖文件夹）随 GUI 清单。
  - 挂账（PR #14 评审遗留）：
    - DataObject.cpp:332 的目录判定与 isDirectoryDescriptorEntry **语义不等价**（FD_ATTRIBUTES
      标志位门 vs INVALID 排除），复用需带 validity 参数——清理项。
    - CodeBuddy 自查 P2：出站侧（我们主动粘目录）streamSeek(DataObject.cpp:261) 对 size=0 目录
      回 E_FAIL，资源管理器在 RANGE 阶段吃到 seek 失败而非干净失败响应——真机观察后定。

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

- [x] **P3-5 崩溃面几乎没有测试覆盖**（分三块陆续落地：PR #6/#8/#10/#11/#14）
  - `fileGroupDescriptorByteSize` 溢出钳制分支：PR #8 补齐（0/1/max/奇数尺寸边界）。
  - `RdpProcessEventQueueBehavior` generation 过滤：PR #8 补齐 + PR #10 修复其编译雷后真跑。
  - P0 回归：PR #6 的 FreeRdpStopSmoke（三轮 start→fail→stop，poison-on-free）常驻 ctest。
  - 追加：RdpSessionMouseStateTests（PR #11）、isDirectoryDescriptorEntry 断言（PR #14）。
  - 仍挂：Probing 非 Win32 空状态断言——但 Linux CI 不编测试树，该断言无处可跑；等 Linux
    测试基建存在再说（与 P3-8 一并考虑）。

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
5. ~~P0-1/P0-2 复审~~（2026-10-26 完成，对 3.32.1 源码 tag bf217a5 逐条复验）：
   - **P0-1 证伪在 3.32.1 成立**：`graphics.c:86-96` Pointer_Alloc = `calloc(1, Prototype->size)`
     全尺寸清零后拷贝模板——派生尾部恒零；模板拷贝 `*Prototype = *pointer` 界内。
   - **P0-2 证伪在 3.32.1 成立**：libfreerdp 全库 `gdi_free` 仅 2 处（gdi.c 定义 + gdi_init
     内部失败自善后），核心链从不代释放；`gdi_free` 自身幂等（gdi 判空 + 置 nullptr）。
   - **附带（PR #14 依赖）**：3.32.1 `cliprdr_common.c:391-395` 对 file-contents 响应
     `dataLen < 4` 拒收（WARN+ERROR_INVALID_DATA）；发送侧 `cliprdr_main.c:72` 线上 dataLen
     仍由实写回填——我们的 fail 响应形态（基载 8/cbRequested 0）在 3.32.1 同样正确。
6. 结构性重构：P2-4 → P2-1 → P2-2（先搬目录再抽公共逻辑，否则抽完还得搬）
