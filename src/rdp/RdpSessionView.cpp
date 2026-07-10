#include "RdpSessionView.h"

#include "rdp/FrameBufferMemory.h"
#include "rdp/RdpCursorClassifier.h"
#include "rdp/RdpFocusNotification.h"
#include "rdp/RdpProcessEventQueueBehavior.h"
#include "rdp/RdpSessionViewBehavior.h"
#include "rdp/RdpSystemChordTrace.h"
#include "ui/MainWindowShortcuts.h"
#include "ui/Win10Theme.h"

#include <imm.h>
#include <utility>

namespace
{
constexpr UINT_PTR kResizeTimerId = 1;
constexpr UINT kResumeRecoveryTimeoutMs = 2500;

CRdpSessionView *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;

class WindowProcessEventTarget final : public rdp::process_event_queue::EventTarget
{
public:
    explicit WindowProcessEventTarget(HWND hwnd)
        : m_hwnd(hwnd)
    {
    }

    bool canPost() const override
    {
        return m_hwnd && ::IsWindow(m_hwnd);
    }

    void postStateChanged(FreeRdpProcess::State state, std::uintptr_t generation) override
    {
        ::PostMessageW(m_hwnd,
                       CRdpSessionView::WM_APP_RDP_STATE,
                       static_cast<WPARAM>(state),
                       static_cast<LPARAM>(generation));
    }

    void postFrameUpdated(std::uintptr_t generation) override
    {
        ::PostMessageW(m_hwnd,
                       CRdpSessionView::WM_APP_RDP_FRAME,
                       0,
                       static_cast<LPARAM>(generation));
    }

    void postCursorUpdated(std::uintptr_t generation) override
    {
        ::PostMessageW(m_hwnd,
                       CRdpSessionView::WM_APP_RDP_CURSOR,
                       0,
                       static_cast<LPARAM>(generation));
    }

    void postCertificateRequest(std::uintptr_t generation) override
    {
        ::PostMessageW(m_hwnd,
                       CRdpSessionView::WM_APP_RDP_CERT,
                       0,
                       static_cast<LPARAM>(generation));
    }

private:
    HWND m_hwnd = nullptr;
};

bool isVirtualKeyPhysicallyDown(int virtualKey)
{
    return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

unsigned int physicalKeyboardModifiers()
{
    unsigned int modifiers = ModifierNone;
    if (isVirtualKeyPhysicallyDown(VK_CONTROL))
        modifiers |= ModifierControl;
    if (isVirtualKeyPhysicallyDown(VK_SHIFT))
        modifiers |= ModifierShift;
    if (isVirtualKeyPhysicallyDown(VK_MENU))
        modifiers |= ModifierAlt;
    if (isVirtualKeyPhysicallyDown(VK_LWIN) || isVirtualKeyPhysicallyDown(VK_RWIN))
        modifiers |= ModifierWin;
    return modifiers;
}

RdpKeyboardPhysicalState physicalKeyboardState()
{
    RdpKeyboardPhysicalState state;
    state.modifiers = physicalKeyboardModifiers();
    if (isVirtualKeyPhysicallyDown(VK_RCONTROL))
        state.controlVirtualKey = VK_RCONTROL;
    else if (isVirtualKeyPhysicallyDown(VK_LCONTROL))
        state.controlVirtualKey = VK_LCONTROL;
    if (isVirtualKeyPhysicallyDown(VK_RSHIFT))
        state.shiftVirtualKey = VK_RSHIFT;
    else if (isVirtualKeyPhysicallyDown(VK_LSHIFT))
        state.shiftVirtualKey = VK_LSHIFT;
    if (isVirtualKeyPhysicallyDown(VK_RMENU))
        state.altVirtualKey = VK_RMENU;
    else if (isVirtualKeyPhysicallyDown(VK_LMENU))
        state.altVirtualKey = VK_LMENU;
    if (isVirtualKeyPhysicallyDown(VK_RWIN))
        state.winVirtualKey = VK_RWIN;
    else if (isVirtualKeyPhysicallyDown(VK_LWIN))
        state.winVirtualKey = VK_LWIN;
    return state;
}

bool isReservedLowLevelShortcut(const KBDLLHOOKSTRUCT *info)
{
    if (!info)
        return false;

    return ui::isReservedMainWindowShortcut(isVirtualKeyPhysicallyDown(VK_CONTROL),
                                            isVirtualKeyPhysicallyDown(VK_MENU)
                                                || (info->flags & LLKHF_ALTDOWN) != 0,
                                            static_cast<unsigned int>(info->vkCode));
}

RdpLowLevelKeyEvent lowLevelKeyEventFromInfo(const KBDLLHOOKSTRUCT *info, bool keyUp)
{
    if (!info)
        return {};

    return RdpLowLevelKeyEvent{
        static_cast<unsigned int>(info->vkCode),
        static_cast<unsigned int>(info->flags),
        keyUp,
        g_systemKeyTarget && ::GetFocus() == g_systemKeyTarget->GetSafeHwnd(),
        isReservedLowLevelShortcut(info)
    };
}

bool shouldCaptureLowLevelKey(const KBDLLHOOKSTRUCT *info, bool keyUp)
{
    if (!info || !g_systemKeyTarget)
        return false;

    return g_systemKeyTarget->shouldCaptureLowLevelKey(
        lowLevelKeyEventFromInfo(info, keyUp),
        physicalKeyboardState());
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION || !g_systemKeyTarget)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    const bool traceKey = rdp::trace::shouldTraceSystemChordVirtualKey(static_cast<unsigned int>(info->vkCode));
    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const bool shouldCapture = shouldCaptureLowLevelKey(info, keyUp);
    const bool canCapture = g_systemKeyTarget->canCaptureSystemKeys();
    if (traceKey) {
        rdp::trace::logSystemChordEvent(
            shouldCapture && canCapture ? L"hook-forward" : L"hook-pass",
            static_cast<unsigned int>(info->vkCode),
            0,
            0,
            static_cast<unsigned int>(info->flags),
            g_systemKeyTarget->activeKeyboardModifiers(),
            ::GetFocus() == g_systemKeyTarget->GetSafeHwnd(),
            false,
            0);
    }
    if (!shouldCapture || !canCapture)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
    const RdpLowLevelKeyEvent lowLevelEvent = lowLevelKeyEventFromInfo(info, keyUp);
    const std::uint32_t message =
        g_systemKeyTarget->messageForLowLevelKey(lowLevelEvent, physicalKeyboardState());
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
    rdp::trace::resetSystemChordTrace();

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
    if (!rdp::session_view::shouldFlushPendingResize(m_hasPendingResize,
                                                     m_connected,
                                                     static_cast<bool>(m_process)))
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
        WindowProcessEventTarget target(binding->hwnd);
        rdp::process_event_queue::postStateChanged(target, state, binding->generation);
    });

    m_process->setFrameUpdatedCallback([binding]() {
        WindowProcessEventTarget target(binding->hwnd);
        rdp::process_event_queue::postFrameUpdated(target, binding->generation);
    });

    m_process->setDesktopResizedCallback([binding](const SizeI &) {
        WindowProcessEventTarget target(binding->hwnd);
        rdp::process_event_queue::postFrameUpdated(target, binding->generation);
    });

    m_process->setCursorUpdatedCallback([binding]() {
        WindowProcessEventTarget target(binding->hwnd);
        rdp::process_event_queue::postCursorUpdated(target, binding->generation);
    });

    m_process->setCertificateChallengeCallback([binding](const FreeRdpProcess::CertificateChallenge &challenge) {
        {
            std::scoped_lock lock(binding->certMutex);
            rdp::process_event_queue::storePendingCertificateRequest(
                binding->pendingCert,
                binding->generation,
                challenge);
        }
        WindowProcessEventTarget target(binding->hwnd);
        rdp::process_event_queue::postCertificateRequest(target, binding->generation);
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
    m_keyboardRouter.reset();
    m_reservedShortcutTracker.reset();
    m_pressedMouseButtons = 0;
    m_hasLastPointerPoint = false;
    m_mouseMoveCoalescer.reset();
    m_resolutionRecovery.reset();
    if (m_mouseMoveTimerActive) {
        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
    }
    showOverlay(rdp::session_view::startOverlayText(m_reconnecting).c_str());
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
    m_keyboardRouter.reset();
    m_reservedShortcutTracker.reset();
    m_pressedMouseButtons = 0;
    m_hasLastPointerPoint = false;
    m_mouseMoveCoalescer.reset();
    m_resolutionRecovery.reset();
    m_resumeRecovery.reset();
    if (showDisconnectedOverlay)
        showOverlay(rdp::session_view::finishedOverlayText({}).c_str());
    releaseCursorHandle();
}

void CRdpSessionView::onStateChanged(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Running:
        m_connected = true;
        if (m_process && m_frameGateActive) {
            auto info = m_process->connectionInfo();
            m_frameGateRemaining =
                rdp::session_view::initialFrameDiscardCount(!info.codecName.empty());
        }
        beginFrameCapture(L"connect");
        if (!m_resolutionUpdatePending && !m_waitingForFirstContentFrame && !m_frameGateActive)
            clearOverlay();
        m_keyboardRouter.reset();
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
                overlayText = rdp::session_view::finishedOverlayText(error).c_str();
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

RdpKeyboardPhysicalState CRdpSessionView::currentKeyboardPhysicalState() const
{
    return physicalKeyboardState();
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
    m_frameGateRemaining = rdp::session_view::initialFrameDiscardCount(true);
    m_resolutionRecovery.begin(m_connected);
    syncRecoveryTimer();
    beginFrameCapture(L"resize");
    showOverlay(rdp::session_view::startOverlayText(true).c_str());
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
    return rdp::session_view::normalizedViewSize(rect.Width(), rect.Height());
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

    const SizeI size = rdp::session_view::normalizedViewSize(cx, cy);

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
    m_keyboardRouter.onFocusGained();
    disableLocalIme();
    setFocusToFreeRdp();
}

void CRdpSessionView::OnKillFocus(CWnd *newWnd)
{
    CWnd::OnKillFocus(newWnd);
    rdp::trace::logSystemChordNote(L"kill-focus",
                                   m_keyboardRouter.activeKeyboardModifiers(),
                                   false,
                                   m_keyboardRouter.captureSystemKeysWithoutFocus(),
                                   m_keyboardRouter.pressedKeyCount());
    flushPendingMouseMove();
    releasePressedMouseButtons();
    // Reserved shortcuts (Ctrl+N/Ctrl+P) open a modal dialog that steals focus
    // before the matching key-up returns here, so the tracked key-up would never
    // be consumed. Drop the tracked keys on focus loss to avoid swallowing a
    // later, unrelated key-up for the same virtual key on the remote host.
    m_reservedShortcutTracker.reset();
    sendKeyboardActions(m_keyboardRouter.handleFocusLost(physicalKeyboardState()));
    if (m_keyboardRouter.captureSystemKeysWithoutFocus()) {
        rdp::trace::logSystemChordNote(L"defer-focus-release",
                                       m_keyboardRouter.activeKeyboardModifiers(),
                                       false,
                                       m_keyboardRouter.captureSystemKeysWithoutFocus(),
                                       m_keyboardRouter.pressedKeyCount());
        return;
    }
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

void CRdpSessionView::sendKeyboardAction(const RdpKeyboardInputRouter::KeyAction &action)
{
    if (!m_process)
        return;

    const unsigned int virtualKey = action.virtualKey != 0
        ? action.virtualKey
        : static_cast<unsigned int>(MapVirtualKeyW(action.key.scanCode, MAPVK_VSC_TO_VK_EX));
    if (rdp::trace::shouldTraceSystemChordVirtualKey(virtualKey)) {
        rdp::trace::logSystemChordEvent(L"send-key",
                                        virtualKey,
                                        action.down ? WM_KEYDOWN : WM_KEYUP,
                                        0,
                                        action.key.extended ? 0x01000000u : 0u,
                                        m_keyboardRouter.activeKeyboardModifiers(),
                                        ::GetFocus() == GetSafeHwnd(),
                                        m_keyboardRouter.captureSystemKeysWithoutFocus(),
                                        m_keyboardRouter.pressedKeyCount());
    }
    m_process->sendKey(action.key, action.down, action.wasDown);
}

void CRdpSessionView::sendKeyboardActions(const std::vector<RdpKeyboardInputRouter::KeyAction> &actions)
{
    for (const auto &action : actions)
        sendKeyboardAction(action);
}

void CRdpSessionView::rememberReservedShortcutKey(unsigned int virtualKey)
{
    m_reservedShortcutTracker.noteHandledKeyDown(virtualKey);
}

bool CRdpSessionView::consumeReservedShortcutKey(unsigned int virtualKey)
{
    return m_reservedShortcutTracker.consumeHandledKeyUp(virtualKey);
}

void CRdpSessionView::releaseKeyboardTargetIfInactive()
{
    if (::GetFocus() != GetSafeHwnd() && !m_keyboardRouter.captureSystemKeysWithoutFocus()) {
        if (g_systemKeyTarget == this)
            g_systemKeyTarget = nullptr;
        releaseKeyboardHookIfUnused();
    }
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
    rdp::trace::logSystemChordNote(L"release-all-pressed-keys",
                                   m_keyboardRouter.activeKeyboardModifiers(),
                                   ::GetFocus() == GetSafeHwnd(),
                                   m_keyboardRouter.captureSystemKeysWithoutFocus(),
                                   m_keyboardRouter.pressedKeyCount());

    if (!m_process || m_process->state() != FreeRdpProcess::State::Running) {
        m_keyboardRouter.reset();
        m_reservedShortcutTracker.reset();
        return;
    }

    sendKeyboardActions(m_keyboardRouter.releaseAllPressedKeys());
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
