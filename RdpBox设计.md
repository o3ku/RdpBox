# RdpBox 设计（历史记录）

> 注意：本文档记录的是早期 Qt 方案。当前仓库实现已经切换为 MFC/Win32 + FreeRDP 的现行架构，除非是在追溯历史设计，否则不要把本文作为当前实现说明。

## 1. 目标

使用 Qt 复刻一个名为 `RdpBox` 的精简版远程连接管理器，初始版本仅支持：

- RDP 协议
- 多标签页管理 RDP 会话
- 基于 FreeRDP 的内嵌连接引擎

本版本目标是先做出一个可用、稳定、边界清晰的 MVP，不追求与现有 1Remote 的 RDP 能力完全对齐。

## 2. 范围

### 2.1 本期包含

- RDP 连接配置的新增、编辑、删除
- 连接列表与搜索
- 多标签打开、切换、关闭、重连
- 基本凭证登录
- 证书校验失败时的提示与接受策略
- 剪贴板重定向
- 窗口化 / 全屏切换
- 断线提示与手动重连
- 本地配置持久化

### 2.2 本期不包含

- ActiveX / MSTSC engine
- SSH / VNC / SFTP 等其他协议
- RemoteApp
- 多屏精细控制
- 打印机 / 磁盘 / 摄像头 / 智能卡 / 音频采集重定向
- 拖拽拆分多窗口
- 高级 `.rdp` 兼容配置导入导出
- 网关高级策略与企业级审计能力

## 3. 可行性结论

结论：可行，且初始版本选择 FreeRDP 明显优于复刻 Windows ActiveX 路线。

原因：

- 多标签管理本质上是标准桌面 UI 问题，Qt Widgets 适合实现。
- 只做 RDP 后，协议层可以大幅收缩，不需要保留当前项目的多协议抽象复杂度。
- FreeRDP 适合作为统一连接内核，避免把 ActiveX/COM/窗口消息补丁迁移到 Qt。
- 当前 1Remote 的代码已经证明“标签壳”和“RDP 宿主”可以分层实现，这种结构适合直接迁移到 Qt。

风险仍然存在，但主要集中在：

- FreeRDP 视图嵌入 Qt 后的焦点管理
- Tab 切换时键盘和鼠标输入恢复
- 窗口缩放与远端分辨率同步
- Windows 打包时 FreeRDP 依赖收集

## 4. 总体设计

### 4.1 架构原则

- UI 壳与 RDP 引擎解耦
- 会话生命周期统一由 SessionManager 管理
- 每个标签页只承载一个会话视图
- 配置模型与运行态模型分离
- 先实现稳定的最小功能，再逐步加高级重定向能力

### 4.2 分层结构

建议采用以下分层：

1. UI 层
   - 主窗口、连接列表、标签页、编辑对话框、提示框
2. 应用层
   - 会话打开/关闭/重连、标签调度、命令分发
3. 引擎适配层
   - FreeRDP 初始化、连接参数映射、事件回调、错误转换
4. 数据层
   - 配置持久化、密码存储、最近连接记录

## 5. 核心模块

### 5.1 MainWindow

职责：

- 承载整体 UI 框架
- 左侧显示连接列表
- 右侧显示标签页区域
- 提供新建连接、编辑连接、连接、关闭标签、全屏等命令入口

建议技术：

- `QMainWindow`
- 中心区域使用 `QSplitter`
- 标签页使用 `QTabWidget` 或自定义 `QTabBar + QStackedWidget`

### 5.2 ProfileRepository

职责：

- 持久化 RDP 配置
- 提供增删改查
- 维护最近连接时间、显示名、地址等元数据

建议存储：

- 初始版本使用 JSON 文件
- 文件示例：`profiles.json`
- 密码不直接明文写入配置，使用系统安全存储或单独加密字段

建议字段：

- `id`
- `displayName`
- `host`
- `port`
- `username`
- `domain`
- `passwordRef` 或加密后的 `password`
- `clipboardEnabled`
- `fullScreenOnConnect`
- `ignoreCertificate`
- `lastConnectedAt`

### 5.3 SessionManager

职责：

- 统一管理会话生命周期
- 根据 Profile 创建会话
- 控制连接、断开、重连
- 对外暴露会话状态变化事件

核心接口建议：

- `openSession(profileId)`
- `closeSession(sessionId)`
- `reconnectSession(sessionId)`
- `activateSession(sessionId)`
- `sessionByTab(tabId)`

说明：

- UI 不直接操作 FreeRDP 对象，统一通过 SessionManager 调度。
- 这样后续即使切换到别的 RDP engine，也只需替换适配层。

### 5.4 TabManager

职责：

- 管理标签页与会话的映射
- 处理标签新增、切换、关闭
- 维护当前激活标签

建议规则：

- 一个 session 对应一个 tab
- 默认允许同一 profile 打开多个 tab
- 如后续需要“单实例连接”，在 SessionManager 增加策略即可

### 5.5 RdpSession

职责：

- 表示一个运行中的 RDP 会话
- 持有运行态信息而非配置原文

建议状态：

- `Idle`
- `Connecting`
- `Connected`
- `Disconnected`
- `Failed`

建议属性：

- `sessionId`
- `profileId`
- `state`
- `lastError`
- `createdAt`
- `connectedAt`

### 5.6 FreeRdpAdapter

职责：

- 封装 FreeRDP 的创建、参数设置、连接、断开和事件回调
- 把底层错误转换成 UI 可理解的消息
- 向上层输出统一事件

建议接口：

- `initialize(profile, QWidget* parent)`
- `connect()`
- `disconnect()`
- `resize(width, height)`
- `setFocused()`
- `enterFullScreen()`
- `leaveFullScreen()`

输出事件：

- `connected`
- `disconnected`
- `errorOccurred`
- `certificateRequested`
- `credentialsRequested`

### 5.7 RdpSessionWidget

职责：

- 承载单个 RDP 会话的可视区域
- 在 tab 内容区内嵌 FreeRDP 画面
- 处理 resize/focus/show/hide 事件

这是 MVP 成败的关键模块。它要解决：

- 标签切换后焦点恢复
- Widget resize 后通知引擎调整显示
- 全屏切换时父子窗口关系变化

## 6. 推荐技术路线

### 6.1 正式路线

`Qt Widgets + FreeRDP library`

原因：

- 适合长期维护
- UI 与渲染集成更自然
- 不需要把外部窗口强行塞回 Qt 控件树
- 后续能更稳定地处理焦点、尺寸和全屏

### 6.2 不推荐作为正式架构的路线

`Qt + 启动 wfreerdp.exe + 重父化嵌入 tab`

问题：

- 外部进程窗口生命周期更难控
- 焦点、快捷键、全屏和异常退出都更脆弱
- 后续重连、尺寸同步和状态管理成本更高

这条路线只适合快速 POC，不适合正式产品化。

## 7. 关键交互与事件流

### 7.1 打开连接

1. 用户双击连接项或点击“连接”
2. MainWindow 调用 SessionManager.openSession(profileId)
3. SessionManager 读取 Profile
4. 创建 RdpSession 与 RdpSessionWidget
5. FreeRdpAdapter 初始化并绑定到 Widget
6. TabManager 新增一个 tab
7. Adapter 发起连接
8. UI 更新状态为 `Connecting`
9. 连接成功后切换为 `Connected`

### 7.2 断线

1. FreeRDP 回调断开事件
2. Adapter 转换为统一断线事件
3. SessionManager 更新会话状态
4. Tab 页显示“已断开 / 重连”提示
5. 用户可手动关闭或重连

### 7.3 标签切换

1. 当前 tab 失活
2. 新 tab 激活
3. TabManager 通知 SessionManager
4. SessionManager 调用对应 RdpSessionWidget 的 `setFocused()`
5. 必要时触发一次延迟 focus 修正

说明：

- 这里很可能需要一个小的延迟聚焦机制，否则会遇到 Qt tab 切换后远程桌面不接收键盘输入的问题。

### 7.4 窗口缩放

1. RdpSessionWidget 收到 resize 事件
2. 合并短时间内连续 resize
3. 调用 FreeRdpAdapter.resize(width, height)
4. 如果底层不支持平滑动态分辨率，第一版允许退化为固定缩放或重连

## 8. 数据设计

### 8.1 Profile

```json
{
  "id": "rdp-001",
  "displayName": "Office Server",
  "host": "10.0.0.8",
  "port": 3389,
  "username": "administrator",
  "domain": "",
  "passwordRef": "cred://rdp-001",
  "clipboardEnabled": true,
  "fullScreenOnConnect": false,
  "ignoreCertificate": true,
  "lastConnectedAt": "2026-04-23T12:00:00Z"
}
```

### 8.2 Session

运行态对象，不落盘：

- `sessionId`
- `profileId`
- `tabId`
- `state`
- `lastError`
- `createdAt`
- `connectedAt`

## 9. MVP 实现建议

### 9.1 第一阶段：POC

目标：证明 Qt 中可稳定承载 FreeRDP 会话。

交付物：

- 一个窗口
- 一个固定连接配置
- 一个 RDP 显示区域
- 能连接、断开、接收输入

通过标准：

- 连接后可正常键盘输入
- 鼠标点击后不会丢焦点
- resize 后画面可接受
- 断开时应用不崩溃

### 9.2 第二阶段：MVP

目标：形成可使用的多标签工具。

交付物：

- 连接配置 CRUD
- 标签页管理
- 重连
- 基本错误提示
- 本地配置持久化

### 9.3 第三阶段：可用性增强

目标：补齐基础体验问题。

交付物：

- 证书处理优化
- 最近连接记录
- 搜索过滤
- 连接状态图标
- 全屏切换更顺滑

## 10. 风险与对策

### 10.1 焦点问题

风险：

- tab 切换后远程桌面无法接收键盘

对策：

- 在 `showEvent`、`focusInEvent`、tab changed 事件中统一调用聚焦
- 必要时增加 0 到 50ms 的延迟 focus 修正

### 10.2 动态分辨率问题

风险：

- resize 后远端分辨率不同步或画面拉伸异常

对策：

- MVP 先接受“固定分辨率 + 缩放”
- 动态分辨率作为第二阶段增强，不在初版硬做

### 10.3 依赖部署问题

风险：

- Windows 发布包缺少 FreeRDP 依赖导致运行失败

对策：

- 尽早固定编译链和依赖版本
- 在 CI 或打包脚本中显式收集运行库

### 10.4 线程模型问题

风险：

- FreeRDP 回调线程与 Qt UI 线程混用造成崩溃

对策：

- 底层回调统一转发到 Qt 主线程
- UI 更新只允许在主线程执行

## 11. 与现有 1Remote 的关系

这次 Qt 版不建议直接照搬当前 1Remote 的完整结构，而是只吸收以下思路：

- 会话管理和 UI 容器分层
- 连接配置与运行时对象分离
- tab 只是 session 的承载壳
- 引擎实现必须被适配层包住

不建议初版直接迁移的内容：

- 复杂的多协议模型
- ActiveX 兼容补丁
- 大量 RDP 高级参数面板
- 过重的全局服务定位结构

## 12. 推荐目录结构

```text
RdpBox/
  app/
    main.cpp
    MainWindow.h/.cpp
  profiles/
    Profile.h/.cpp
    ProfileRepository.h/.cpp
  session/
    SessionManager.h/.cpp
    RdpSession.h/.cpp
    TabManager.h/.cpp
  rdp/
    FreeRdpAdapter.h/.cpp
    RdpSessionWidget.h/.cpp
    RdpError.h
  ui/
    ConnectionListWidget.h/.cpp
    ProfileEditDialog.h/.cpp
  storage/
    CredentialStore.h/.cpp
    JsonStore.h/.cpp
```

## 13. 最终建议

建议按下面顺序推进：

1. 先做 FreeRDP 在 Qt Widgets 中稳定嵌入的 POC
2. POC 通过后再做 ProfileRepository 和 SessionManager
3. 然后接入多标签页和重连逻辑
4. 最后再补全屏、证书策略、搜索和最近连接

如果 POC 阶段发现 FreeRDP library 与 Qt 嵌入存在不可接受的焦点或稳定性问题，再退回“外部进程 + 重父化”的临时方案做验证，但不建议直接把这条路线定为正式架构。
