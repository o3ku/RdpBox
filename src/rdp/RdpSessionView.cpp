#include "RdpSessionView.h"

#include "common/Win32String.h"
#include "rdp/FrameBufferMemory.h"
#include "rdp/RdpCursorClassifier.h"
#include "rdp/RdpInputModifiers.h"
#include "rdp/RdpFocusNotification.h"
#include "resources/resource.h"
#include "ui/MainWindowShortcuts.h"
#include "ui/ParentResizeForwarder.h"
#include "ui/Win10Theme.h"

#include <algorithm>
#include <imm.h>

namespace
{
constexpr UINT_PTR kResizeTimerId = 1;
constexpr UINT_PTR kMouseMoveTimerId = 2;
constexpr UINT kResumeRecoveryTimeoutMs = 2500;
constexpr int kInitialFrameDiscardWithCodec = 3;
constexpr int kInitialFrameDiscardNoCodec = 5;

CRdpSessionView *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;

bool isAltKey(DWORD vkCode)
{
    return vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU;
}

bool isSystemKey(DWORD vkCode)
{
    return vkCode == VK_LWIN || vkCode == VK_RWIN;
}

bool isControlDown()
{
    return (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}

bool isCtrlEscapeSequence(DWORD vkCode)
{
    return vkCode == VK_ESCAPE && isControlDown();
}

bool shouldCaptureLowLevelKey(const KBDLLHOOKSTRUCT *info)
{
    if (!info)
        return false;

    return isSystemKey(info->vkCode)
        || isAltKey(info->vkCode)
        || isCtrlEscapeSequence(info->vkCode)
        || (info->vkCode == VK_TAB && (info->flags & LLKHF_ALTDOWN));
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION || !g_systemKeyTarget)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    if (!shouldCaptureLowLevelKey(info) || !g_systemKeyTarget->canCaptureSystemKeys())
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
    const bool sysContext = isAltKey(info->vkCode) || (info->flags & LLKHF_ALTDOWN);
    const std::uint32_t message = keyUp
        ? (sysContext ? WM_SYSKEYUP : WM_KEYUP)
        : (sysContext ? WM_SYSKEYDOWN : WM_KEYDOWN);
    const std::intptr_t keyLParam = static_cast<std::intptr_t>((info->scanCode & 0xFFu) << 16)
        | (extended ? 0x01000000 : 0)
        | (keyUp ? 0xC0000000 : 0);

    g_systemKeyTarget->forwardNativeKeyMessage(
        message,
        static_cast<std::uintptr_t>(info->vkCode),
        keyLParam);
    return 1;
}

void ensureKeyboardHook()
{
    if (!g_keyboardHook)
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);
}

void releaseKeyboardHookIfUnused()
{
    if (!g_systemKeyTarget && g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}

}

IMPLEMENT_DYNAMIC(CRdpSessionView, CWnd)

BEGIN_MESSAGE_MAP(CRdpSessionView, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_CANCELMODE()
    ON_WM_CAPTURECHANGED()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_LBUTTONUP()
    ON_WM_RBUTTONDOWN()
    ON_WM_RBUTTONDBLCLK()
    ON_WM_RBUTTONUP()
    ON_WM_MBUTTONDOWN()
    ON_WM_MBUTTONDBLCLK()
    ON_WM_MBUTTONUP()
    ON_WM_MOUSEWHEEL()
    ON_MESSAGE(CRdpSessionView::WM_APP_RDP_STATE, &CRdpSessionView::OnRdpStateChanged)
    ON_MESSAGE(CRdpSessionView::WM_APP_RDP_FRAME, &CRdpSessionView::OnRdpFrameUpdated)
    ON_MESSAGE(CRdpSessionView::WM_APP_RDP_CURSOR, &CRdpSessionView::OnRdpCursorUpdated)
    ON_MESSAGE(CRdpSessionView::WM_APP_RDP_CERT, &CRdpSessionView::OnRdpCertRequest)
END_MESSAGE_MAP()

CRdpSessionView::CRdpSessionView() = default;

CRdpSessionView::~CRdpSessionView()
{
    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;

    releaseKeyboardHookIfUnused();
    stopProcess();
    releaseCursorHandle();
}

bool CRdpSessionView::create(CWnd *parent, const CRect &rect)
{
    static HBRUSH s_backgroundBrush = ::CreateSolidBrush(RGB(17, 17, 17));
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  s_backgroundBrush,
                                                  nullptr);

    m_created = CreateEx(0, className, L"RdpSessionView",
                         WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                         rect, parent, 0) != FALSE;
    if (m_created) {
        HFONT rawOverlayFont = Win10Theme::createUiFont(11);
        if (rawOverlayFont)
            m_overlayFont.Attach(rawOverlayFont);
        disableLocalIme();
        SetFocus();
    }

    return m_created;
}

void CRdpSessionView::connectToHost(const Profile &profile)
{
    m_profile = profile;
    stopProcess(false);

    m_process = std::make_shared<FreeRdpProcess>();
    bindProcessCallbacks(++m_processGeneration);
    startProcess();
}

void CRdpSessionView::reconnect()
{
    if (!m_profile.isValid())
        return;

    m_reconnecting = true;
    connectToHost(m_profile);
}

void CRdpSessionView::setReconnectRequestedCallback(std::function<void()> callback)
{
    m_reconnectRequested = std::move(callback);
}

void CRdpSessionView::setConnectedCallback(std::function<void()> callback)
{
    m_connectedCallback = std::move(callback);
}

FreeRdpProcess::ConnectionInfo CRdpSessionView::connectionInfo() const
{
    if (!m_process)
        return {};
    return m_process->connectionInfo();
}

bool CRdpSessionView::isConnected() const
{
    return m_connected;
}

void CRdpSessionView::setResizeSuppressed(bool suppressed)
{
    m_resizeSuppressed = suppressed;
    if (suppressed) {
        KillTimer(kResizeTimerId);
        m_resizeBurstTracker.reset();
    }
}

void CRdpSessionView::flushPendingResize()
{
    if (!m_hasPendingResize || !m_connected || !m_process)
        return;

    m_hasPendingResize = false;
    m_resizeBurstTracker.reset();
    beginResolutionUpdate();
    m_process->requestResize(m_pendingResize);
}

void CRdpSessionView::handleHostResume(bool autoReconnect)
{
    if (!m_profile.isValid())
        return;

    if (autoReconnect) {
        reconnect();
        return;
    }

    stopProcess(true);
}

void CRdpSessionView::handleBecameVisible()
{
    requestDeferredResumeRefreshIfNeeded();
}

void CRdpSessionView::bindProcessCallbacks(std::uintptr_t generation)
{
    if (!m_process)
        return;

    const HWND hwnd = GetSafeHwnd();
    if (!hwnd)
        return;

    auto binding = std::make_shared<ProcessBinding>();
    binding->hwnd = hwnd;
    binding->generation = generation;
    m_processBinding = binding;

    m_process->setStateChangedCallback([binding](FreeRdpProcess::State state) {
        if (binding->hwnd && ::IsWindow(binding->hwnd))
            ::PostMessageW(binding->hwnd, WM_APP_RDP_STATE, static_cast<WPARAM>(state), static_cast<LPARAM>(binding->generation));
    });

    m_process->setFrameUpdatedCallback([binding]() {
        if (binding->hwnd && ::IsWindow(binding->hwnd))
            ::PostMessageW(binding->hwnd, WM_APP_RDP_FRAME, 0, static_cast<LPARAM>(binding->generation));
    });

    m_process->setDesktopResizedCallback([binding](const SizeI &) {
        if (binding->hwnd && ::IsWindow(binding->hwnd))
            ::PostMessageW(binding->hwnd, WM_APP_RDP_FRAME, 0, static_cast<LPARAM>(binding->generation));
    });

    m_process->setCursorUpdatedCallback([binding]() {
        if (binding->hwnd && ::IsWindow(binding->hwnd))
            ::PostMessageW(binding->hwnd, WM_APP_RDP_CURSOR, 0, static_cast<LPARAM>(binding->generation));
    });

    m_process->setCertificateChallengeCallback([binding](const FreeRdpProcess::CertificateChallenge &challenge) {
        {
            std::scoped_lock lock(binding->certMutex);
            binding->pendingCert = PendingCertificateRequest{ binding->generation, challenge };
        }
        if (binding->hwnd && ::IsWindow(binding->hwnd))
            ::PostMessageW(binding->hwnd, WM_APP_RDP_CERT, 0, static_cast<LPARAM>(binding->generation));
    });
}

void CRdpSessionView::clearProcessCallbacks()
{
    if (!m_process)
        return;

    m_process->setStateChangedCallback({});
    m_process->setFrameUpdatedCallback({});
    m_process->setDesktopResizedCallback({});
    m_process->setCursorUpdatedCallback({});
    m_process->setCertificateChallengeCallback({});
}

bool CRdpSessionView::isCurrentGeneration(std::uintptr_t generation) const
{
    return generation == m_processGeneration;
}

void CRdpSessionView::disableLocalIme() const
{
    const HWND hwnd = GetSafeHwnd();
    if (!hwnd)
        return;

    ImmAssociateContext(hwnd, nullptr);
}

void CRdpSessionView::startProcess()
{
    if (!m_process || !GetSafeHwnd())
        return;

    KillTimer(kResumeRecoveryTimerId);
    m_resumeRecovery.reset();
    m_connected = false;
    m_cachedFrameGeneration = 0;
    FrameBufferMemory::release(m_cachedFrame);
    m_resolutionUpdatePending = false;
    m_waitingForFirstContentFrame = true;
    m_frameGateActive = true;
    m_frameGateRemaining = 0;
    m_captureDirectory.clear();
    m_captureDirectory.shrink_to_fit();
    m_captureFramesRemaining = 0;
    m_captureFrameIndex = 0;
    m_resizeBurstTracker.reset();
    m_modifierTracker.reset();
    m_keyboardModifiers = ModifierNone;
    m_pressedKeys.clear();
    m_reservedShortcutTracker.reset();
    m_pressedMouseButtons = 0;
    m_hasLastPointerPoint = false;
    m_mouseMoveCoalescer.reset();
    m_resolutionRecovery.reset();
    if (m_mouseMoveTimerActive) {
        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
    }
    showOverlay(m_reconnecting ? L"Reconnecting..." : L"Connecting...");
    m_reconnecting = false;

    const SizeI viewSize = m_profile.fullScreenOnConnect
        ? fullScreenSize()
        : currentViewSize();
    m_process->start(m_profile.host, m_profile.port,
                     m_profile.username, m_profile.password,
                     m_profile.domain,
                     viewSize.width, viewSize.height,
                     m_profile.clipboardEnabled, m_profile.ignoreCertificate);
}

void CRdpSessionView::stopProcess(bool showDisconnectedOverlay)
{
    KillTimer(kResizeTimerId);
    KillTimer(kResumeRecoveryTimerId);
    if (m_mouseMoveTimerActive) {
        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
    }

    if (m_process) {
        flushPendingMouseMove();
        releasePressedMouseButtons();
        releaseAllPressedKeys();
        clearProcessCallbacks();
    }

    ++m_processGeneration;

    if (m_process) {
        m_process->stop();
        m_process.reset();
    }

    m_connected = false;
    m_resolutionUpdatePending = false;
    m_waitingForFirstContentFrame = false;
    m_frameGateActive = false;
    m_frameGateRemaining = 0;
    m_pendingResize = {};
    m_hasPendingResize = false;
    m_cachedFrameGeneration = 0;
    FrameBufferMemory::release(m_cachedFrame);
    m_captureDirectory.clear();
    m_captureDirectory.shrink_to_fit();
    m_captureFramesRemaining = 0;
    m_captureFrameIndex = 0;
    m_processBinding.reset();
    m_resizeBurstTracker.reset();
    m_modifierTracker.reset();
    m_keyboardModifiers = ModifierNone;
    m_pressedKeys.clear();
    m_reservedShortcutTracker.reset();
    m_pressedMouseButtons = 0;
    m_hasLastPointerPoint = false;
    m_mouseMoveCoalescer.reset();
    m_resolutionRecovery.reset();
    m_resumeRecovery.reset();
    if (showDisconnectedOverlay)
        showOverlay(L"Disconnected - Click to Reconnect");
    releaseCursorHandle();
}

void CRdpSessionView::onStateChanged(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Running:
        m_connected = true;
        if (m_process && m_frameGateActive) {
            auto info = m_process->connectionInfo();
            m_frameGateRemaining = info.codecName.empty()
                ? kInitialFrameDiscardNoCodec
                : kInitialFrameDiscardWithCodec;
        }
        beginFrameCapture(L"connect");
        if (!m_resolutionUpdatePending && !m_waitingForFirstContentFrame && !m_frameGateActive)
            clearOverlay();
        m_modifierTracker.reset();
        m_keyboardModifiers = ModifierNone;
        m_pressedKeys.clear();
        m_reservedShortcutTracker.reset();
        m_pressedMouseButtons = 0;
        m_hasLastPointerPoint = false;
        m_resizeBurstTracker.reset();
        m_resolutionRecovery.reset();
        KillTimer(kResumeRecoveryTimerId);
        m_resumeRecovery.reset();
        updateCursorFromProcess();
        setFocusToFreeRdp();
        if (m_process)
            m_process->requestResize(currentViewSize());
        Invalidate(FALSE);
        if (m_connectedCallback)
            m_connectedCallback();
        break;
    case FreeRdpProcess::State::Finished:
        m_connected = false;
        m_resolutionUpdatePending = false;
        m_waitingForFirstContentFrame = false;
        m_frameGateActive = false;
        m_frameGateRemaining = 0;
        KillTimer(kResizeTimerId);
        KillTimer(kResumeRecoveryTimerId);
        m_resolutionRecovery.reset();
        m_resumeRecovery.reset();
        {
            CString overlayText = L"Disconnected";
            if (m_process) {
                std::string error = m_process->lastDisconnectError();
                if (!error.empty())
                    overlayText = CString(error.c_str()) + L"\r\nClick to Reconnect";
                else
                    overlayText = L"Disconnected - Click to Reconnect";
            }
            showOverlay(overlayText);
        }
        releaseCursorHandle();
        Invalidate(FALSE);
        break;
    default:
        break;
    }
}

void CRdpSessionView::updateCursorFromProcess()
{
    if (!m_process)
        return;

    releaseCursorHandle();

    const CursorInfo cursorInfo = m_process->cursor();
    m_cursorHandle = RdpCursorClassifier::cursorHandleFromInfo(cursorInfo);
    m_ownsCursorHandle = cursorInfo.ownsHandle;

    if (GetSafeHwnd())
        ::SetCursor(m_cursorHandle);
}

void CRdpSessionView::setFocusToFreeRdp()
{
    if (!m_process || !GetSafeHwnd())
        return;

    const bool hadWindowFocus = (::GetFocus() == GetSafeHwnd());
    const bool hadSystemKeyTarget = (g_systemKeyTarget == this);

    g_systemKeyTarget = this;
    ensureKeyboardHook();

    if (!hadWindowFocus)
        SetFocus();

    if (rdp::shouldSendFocusIn(hadWindowFocus,
                               hadSystemKeyTarget,
                               ::GetFocus() == GetSafeHwnd())) {
        m_process->sendFocusIn();
    }
}

void CRdpSessionView::showOverlay(const CString &text)
{
    m_overlayText = text;
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void CRdpSessionView::clearOverlay()
{
    m_overlayText.Empty();
    if (GetSafeHwnd())
        Invalidate(FALSE);
}

void CRdpSessionView::beginResolutionUpdate()
{
    m_resolutionUpdatePending = true;
    m_frameGateActive = true;
    m_frameGateRemaining = kInitialFrameDiscardWithCodec;
    m_resolutionRecovery.begin(m_connected);
    syncRecoveryTimer();
    beginFrameCapture(L"resize");
    showOverlay(L"Reconnecting...");
}

void CRdpSessionView::beginResumeRecovery()
{
    if (!m_process)
        return;

    beginFrameCapture(L"resume");
    showOverlay(L"Resuming session...");
    m_process->requestRefresh();
    syncRecoveryTimer();
}

void CRdpSessionView::requestDeferredResumeRefreshIfNeeded()
{
    if (!m_process)
        return;

    if (m_resumeRecovery.onBecameVisible(
            m_connected && m_process->state() == FreeRdpProcess::State::Running)
        != RdpResumeRecovery::Action::RequestRefresh) {
        return;
    }

    beginResumeRecovery();
}

void CRdpSessionView::syncRecoveryTimer()
{
    if (m_resumeRecovery.awaitingFrame() || m_resolutionRecovery.active())
        SetTimer(kResumeRecoveryTimerId, kResumeRecoveryTimeoutMs, nullptr);
    else
        KillTimer(kResumeRecoveryTimerId);
}

SizeI CRdpSessionView::currentViewSize() const
{
    CRect rect;
    GetClientRect(&rect);
    return SizeI{std::max(1, rect.Width()), std::max(1, rect.Height())};
}

SizeI CRdpSessionView::fullScreenSize() const
{
    HMONITOR monitor = GetSafeHwnd()
        ? ::MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST)
        : nullptr;
    if (monitor) {
        MONITORINFO info = {};
        info.cbSize = sizeof(info);
        if (::GetMonitorInfoW(monitor, &info))
            return SizeI{info.rcMonitor.right - info.rcMonitor.left,
                         info.rcMonitor.bottom - info.rcMonitor.top};
    }
    return currentViewSize();
}

void CRdpSessionView::releaseCursorHandle()
{
    if (m_cursorHandle && m_ownsCursorHandle)
        DestroyCursor(m_cursorHandle);

    m_cursorHandle = nullptr;
    m_ownsCursorHandle = false;
}

void CRdpSessionView::OnSize(UINT type, int cx, int cy)
{
    CWnd::OnSize(type, cx, cy);

    if (!m_connected || !m_process) {
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        return;
    }

    const SizeI size{std::max(1, cx), std::max(1, cy)};

    if (m_resizeSuppressed) {
        m_pendingResize = size;
        m_hasPendingResize = true;
        beginResolutionUpdate();
        return;
    }

    if (m_resizeBurstTracker.onResize(size)) {
        beginResolutionUpdate();
        m_process->requestResize(size);
        SetTimer(kResizeTimerId, 50, nullptr);
    }

    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CRdpSessionView::OnTimer(UINT_PTR timerId)
{
    if (timerId == kMouseMoveTimerId) {
        if (!m_process) {
            KillTimer(kMouseMoveTimerId);
            m_mouseMoveTimerActive = false;
            return;
        }

        const auto pending = m_mouseMoveCoalescer.onTimer();
        if (pending) {
            m_process->sendMouseMove(*pending, currentViewSize());
            return;
        }

        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
        return;
    }

    if (timerId == kResumeRecoveryTimerId) {
        KillTimer(kResumeRecoveryTimerId);
        if (m_resolutionRecovery.onTimeout()
            || m_resumeRecovery.onTimeout() == RdpResumeRecovery::Action::Reconnect) {
            reconnect();
        }
        return;
    }

    if (timerId != kResizeTimerId) {
        CWnd::OnTimer(timerId);
        return;
    }

    if (!m_connected || !m_process) {
        KillTimer(kResizeTimerId);
        m_resizeBurstTracker.reset();
        return;
    }

    const SizeI size = currentViewSize();
    if (m_resizeBurstTracker.onTimeout(size)) {
        beginResolutionUpdate();
        m_process->requestResize(size);
        return;
    }

    KillTimer(kResizeTimerId);
}

void CRdpSessionView::OnSetFocus(CWnd *oldWnd)
{
    CWnd::OnSetFocus(oldWnd);
    disableLocalIme();
    setFocusToFreeRdp();
}

void CRdpSessionView::OnKillFocus(CWnd *newWnd)
{
    CWnd::OnKillFocus(newWnd);
    flushPendingMouseMove();
    releasePressedMouseButtons();
    releaseAllPressedKeys();
    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
}

void CRdpSessionView::noteConsumedLocalShortcutKey(unsigned int virtualKey)
{
    rememberReservedShortcutKey(virtualKey);
}

void CRdpSessionView::OnCancelMode()
{
    CWnd::OnCancelMode();
    releasePressedMouseButtons();
}

void CRdpSessionView::OnCaptureChanged(CWnd *wnd)
{
    CWnd::OnCaptureChanged(wnd);
    releasePressedMouseButtons();
}

void CRdpSessionView::sendTrackedKey(const KeyIdentifier &key, bool down, bool wasDown)
{
    if (!m_process)
        return;

    m_process->sendKey(key, down, wasDown);
    trackKeyState(key, down);
}

bool CRdpSessionView::hasTrackedKey(const KeyIdentifier &key) const
{
    return std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key) != m_pressedKeys.end();
}

void CRdpSessionView::trackKeyState(const KeyIdentifier &key, bool down)
{
    const auto it = std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key);
    if (down) {
        if (it == m_pressedKeys.end())
            m_pressedKeys.push_back(key);
        return;
    }

    if (it != m_pressedKeys.end())
        m_pressedKeys.erase(it);
}

void CRdpSessionView::rememberReservedShortcutKey(unsigned int virtualKey)
{
    m_reservedShortcutTracker.noteHandledKeyDown(virtualKey);
}

bool CRdpSessionView::consumeReservedShortcutKey(unsigned int virtualKey)
{
    return m_reservedShortcutTracker.consumeHandledKeyUp(virtualKey);
}

void CRdpSessionView::sendSynchronizedModifier(unsigned int virtualKey, bool down)
{
    const auto key = keyIdentifierFromVirtualKey(virtualKey);
    if (!key)
        return;

    const bool wasDown = down && hasTrackedKey(*key);
    sendTrackedKey(*key, down, wasDown);
}

unsigned int CRdpSessionView::keyboardModifiersForMessage(std::uint32_t message, unsigned int virtualKey) const
{
    return rdp::keyboardInputModifiersForKeyMessage(message, virtualKey, m_keyboardModifiers);
}

void CRdpSessionView::updateKeyboardModifierState(std::uint32_t message, unsigned int virtualKey)
{
    if (!rdp::isKeyboardModifierVirtualKey(virtualKey))
        return;

    m_keyboardModifiers = rdp::keyboardInputModifiersForKeyMessage(message, virtualKey, m_keyboardModifiers);
}

void CRdpSessionView::sendTrackedMouseButton(MouseButton button, bool down, PointI point)
{
    if (!m_process)
        return;

    m_process->sendMouseButton(button, down, point, currentViewSize());

    unsigned int bit = 0;
    switch (button) {
    case MouseButton::Left:
        bit = 1u << 0;
        break;
    case MouseButton::Right:
        bit = 1u << 1;
        break;
    case MouseButton::Middle:
        bit = 1u << 2;
        break;
    default:
        break;
    }

    if (bit == 0)
        return;

    if (down)
        m_pressedMouseButtons |= bit;
    else
        m_pressedMouseButtons &= ~bit;
}

void CRdpSessionView::releasePressedMouseButtons()
{
    const unsigned int pressedButtons = m_pressedMouseButtons;
    m_pressedMouseButtons = 0;

    if (GetCapture() == this)
        ReleaseCapture();

    if (pressedButtons == 0)
        return;

    if (!m_process || m_process->state() != FreeRdpProcess::State::Running)
        return;

    const PointI point = currentPointerPosition();
    if (pressedButtons & (1u << 0))
        sendTrackedMouseButton(MouseButton::Left, false, point);
    if (pressedButtons & (1u << 1))
        sendTrackedMouseButton(MouseButton::Right, false, point);
    if (pressedButtons & (1u << 2))
        sendTrackedMouseButton(MouseButton::Middle, false, point);
}

void CRdpSessionView::releaseAllPressedKeys()
{
    m_modifierTracker.reset();
    m_keyboardModifiers = ModifierNone;

    if (!m_process || m_process->state() != FreeRdpProcess::State::Running) {
        m_pressedKeys.clear();
        m_reservedShortcutTracker.reset();
        return;
    }

    const std::vector<KeyIdentifier> pressedKeys = m_pressedKeys;
    for (const auto &key : pressedKeys)
        sendTrackedKey(key, false, true);

    m_pressedKeys.clear();
    m_reservedShortcutTracker.reset();
}

void CRdpSessionView::updatePointerPosition(PointI point)
{
    m_lastPointerPoint = point;
    m_hasLastPointerPoint = true;
}

PointI CRdpSessionView::currentPointerPosition() const
{
    if (m_hasLastPointerPoint)
        return m_lastPointerPoint;

    return PointI{};
}
