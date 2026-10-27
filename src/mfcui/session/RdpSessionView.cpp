#include "RdpSessionView.h"

#include "common/rdp/FrameBufferMemory.h"
#include "common/rdp/RdpCursorClassifier.h"
#include "common/rdp/RdpKeyboardPhysicalProbing.h"
#include "mfcui/session/RdpFocusNotification.h"
#include "mfcui/session/RdpProcessEventQueueBehavior.h"
#include "common/rdp/RdpSessionKeyboardHook.h"
#include "common/rdp/RdpSessionAutoReconnect.h"
#include "common/rdp/RdpSessionViewBehavior.h"
#include "mfcui/Win10Theme.h"

#include <imm.h>
#include <utility>

namespace
{
constexpr UINT_PTR kResizeTimerId = 1;
constexpr UINT kResumeRecoveryTimeoutMs = 2500;

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
} // namespace

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
    rdp::session_view_input::clearKeyboardTarget(this);
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
    m_inputSuspended = false;
    bindProcessCallbacks(++m_processGeneration);
    startProcess();
}

void CRdpSessionView::reconnect()
{
    restartSession(false);
}

void CRdpSessionView::restartSession(bool autoAttempt)
{
    if (!m_profile.isValid())
        return;

    if (!shouldStartAutoReconnect(autoAttempt, m_stateReachedRunning, m_autoReconnectAttempts)) {
        // Nothing is retried: settle on the disconnected overlay this view
        // would have shown had the attempt not been automatic.
        stopProcess(true);
        return;
    }

    if (autoAttempt)
        ++m_autoReconnectAttempts;

    // Two independent facts: this view started a reconnect before the previous
    // attempt reached Running, and this view started that one automatically.
    // Only the latter is a reason to suppress the server's error text.
    m_reconnectInFlight = !m_stateReachedRunning;
    m_autoReconnectInFlight = m_reconnectInFlight && autoAttempt;
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

bool CRdpSessionView::isSessionEnded() const
{
    return !m_process || m_process->state() == FreeRdpProcess::State::Finished;
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
        restartSession(true);
        return;
    }

    stopProcess(true);
}

void CRdpSessionView::handleBecameVisible()
{
    requestDeferredResumeRefreshIfNeeded();
    if (m_connected)
        updateCursorFromProcess();
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
    if (!m_process || m_inputSuspended || !GetSafeHwnd())
        return;

    KillTimer(kResumeRecoveryTimerId);
    m_resumeRecovery.reset();
    m_connected = false;
    m_cachedFrameGeneration = 0;
    FrameBufferMemory::release(m_cachedFrame);
    m_frameGate.resolutionUpdatePending = false;
    m_frameGate.waitingForFirstContentFrame = true;
    m_frameGate.active = true;
    m_frameGate.remaining = 0;
    m_resizeBurstTracker.reset();
    if (m_mouseState.pointerMoveTimerActive()) {
        KillTimer(kMouseMoveTimerId);
        m_mouseState.setPointerMoveTimerActive(false);
    }
    m_keyboardState.reset();
    m_mouseState.reset();
    m_resolutionRecovery.reset();
    showOverlay(rdp::session_view::startOverlayText(m_reconnecting).c_str());
    m_reconnecting = false;
    m_stateReachedRunning = false;

    const SizeI viewSize = currentViewSize();
    m_process->start(m_profile.host, m_profile.port,
                     m_profile.username, m_profile.password,
                     m_profile.domain,
                     viewSize.width, viewSize.height,
                     m_profile.clipboardEnabled, m_profile.ignoreCertificate);
}

void CRdpSessionView::stopProcess(bool showDisconnectedOverlay)
{
    // SessionManager destroys the HWND before the view object goes away, so
    // the destructor lands here with no window. The timers died with it, and
    // the MFC KillTimer wrapper asserts on a null HWND in debug builds.
    if (GetSafeHwnd()) {
        KillTimer(kResizeTimerId);
        KillTimer(kResumeRecoveryTimerId);
        if (m_mouseState.pointerMoveTimerActive()) {
            KillTimer(kMouseMoveTimerId);
            m_mouseState.setPointerMoveTimerActive(false);
        }
    }

    if (m_process) {
        flushPendingMouseMove();
        releasePressedMouseButtons();
        releaseAllPressedKeys();
        clearProcessCallbacks();
    }

    ++m_processGeneration;

    if (m_process) {
        // Close input forwarding before the teardown that can pump messages:
        // an input message delivered through that pump must not reach the
        // session behind this point.
        m_inputSuspended = true;
        m_process->stop();
        // stop() has returned: the session is swapped out and submit()
        // drops input on its own from here, so input forwarding reopens.
        m_inputSuspended = false;
        // Read it before the process goes: the overlay that follows a give-up
        // has to name the error the session actually ended with.
        m_lastDisconnectError = m_process->lastDisconnectError();
        m_process.reset();
    }

    m_connected = false;
    m_frameGate.resolutionUpdatePending = false;
    m_frameGate.waitingForFirstContentFrame = false;
    m_frameGate.active = false;
    m_frameGate.remaining = 0;
    m_pendingResize = {};
    m_hasPendingResize = false;
    m_cachedFrameGeneration = 0;
    FrameBufferMemory::release(m_cachedFrame);
    m_processBinding.reset();
    m_resizeBurstTracker.reset();
    m_keyboardState.reset();
    m_mouseState.reset();
    m_resolutionRecovery.reset();
    m_resumeRecovery.reset();
    if (showDisconnectedOverlay) {
        // Nothing is retried past this point, so the user gets the truth: the
        // last error if the session had one, otherwise a plain disconnect.
        m_autoReconnectAttempts = 0;
        showOverlay(rdp::session_view::finishedOverlayText(m_lastDisconnectError).c_str());
    }
    releaseCursorHandle();
}

void CRdpSessionView::onStateChanged(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Running: {
        m_stateReachedRunning = true;
        m_reconnectInFlight = false;
        m_autoReconnectInFlight = false;
        m_autoReconnectAttempts = 0;
        m_connected = true;
        if (m_process && m_frameGate.active) {
            auto info = m_process->connectionInfo();
            m_frameGate.remaining =
                rdp::session_view::initialFrameDiscardCount(!info.codecName.empty());
        }
        if (!m_frameGate.resolutionUpdatePending && !m_frameGate.waitingForFirstContentFrame
            && !m_frameGate.active)
            clearOverlay();
        m_keyboardState.reset();
        m_mouseState.reset();
        m_resizeBurstTracker.reset();
        m_resolutionRecovery.reset();
        KillTimer(kResumeRecoveryTimerId);
        m_resumeRecovery.reset();
        updateCursorFromProcess();
        setFocusToFreeRdp();
        Invalidate(FALSE);
        if (m_connectedCallback)
            m_connectedCallback();
        break;
    }
    case FreeRdpProcess::State::Finished:
        m_connected = false;
        m_frameGate.resolutionUpdatePending = false;
        m_frameGate.waitingForFirstContentFrame = false;
        m_frameGate.active = false;
        m_frameGate.remaining = 0;
        KillTimer(kResizeTimerId);
        KillTimer(kResumeRecoveryTimerId);
        m_resolutionRecovery.reset();
        m_resumeRecovery.reset();
        {
            // Retrying is a decision of its own (the recovery timer may already
            // have given up), so it is not read off m_reconnecting. An attempt
            // that will be retried says so rather than flashing the server's
            // error on its way to the next handshake.
            const bool willRetry = canAutoReconnect(m_stateReachedRunning)
                && shouldStartAutoReconnect(true, m_stateReachedRunning, m_autoReconnectAttempts);
            if (willRetry) {
                showOverlay(rdp::session_view::startOverlayText(true).c_str());
            } else {
                const std::string error = m_process ? m_process->lastDisconnectError() : std::string();
                const bool reportError = !error.empty()
                    && shouldReportDisconnectText(m_autoReconnectInFlight, m_autoReconnectAttempts);
                showOverlay(rdp::session_view::finishedOverlayText(reportError ? error : std::string()).c_str());
            }
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

    if (GetSafeHwnd()
        && IsWindowVisible()
        && m_process->state() == FreeRdpProcess::State::Running)
        ::SetCursor(m_cursorHandle);
}

RdpKeyboardPhysicalState CRdpSessionView::currentKeyboardPhysicalState() const
{
    return rdp::physical_keyboard::physicalKeyboardState();
}

void CRdpSessionView::setFocusToFreeRdp()
{
    if (!m_process || !GetSafeHwnd())
        return;

    const bool hadWindowFocus = (::GetFocus() == GetSafeHwnd());
    const bool hadSystemKeyTarget = rdp::session_view_input::isKeyboardTarget(this);

    rdp::session_view_input::setKeyboardTarget(this);

    if (!hadWindowFocus)
        SetFocus();

    if (rdp::shouldSendFocusIn(hadWindowFocus,
                               hadSystemKeyTarget,
                               ::GetFocus() == GetSafeHwnd())) {
        m_process->sendFocusIn();
    }
}

void CRdpSessionView::releaseKeyboardInputForTargetTransfer()
{
    releaseAllPressedKeys();
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
    m_frameGate.resolutionUpdatePending = true;
    m_frameGate.active = true;
    m_frameGate.remaining = rdp::session_view::initialFrameDiscardCount(true);
    m_resolutionRecovery.begin(m_connected);
    syncRecoveryTimer();
    showOverlay(rdp::session_view::startOverlayText(true).c_str());
}

void CRdpSessionView::beginResumeRecovery()
{
    if (!m_process)
        return;

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

    if (m_resizeBurstTracker.onResize(size))
        SetTimer(kResizeTimerId, 200, nullptr); // trailing debounce: WM_TIMER sends

    RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CRdpSessionView::OnTimer(UINT_PTR timerId)
{
    if (timerId == kMouseMoveTimerId) {
        if (!m_process || m_inputSuspended) {
            KillTimer(kMouseMoveTimerId);
            m_mouseState.setPointerMoveTimerActive(false);
            return;
        }

        const auto pending = m_mouseState.onPointerMoveTimer();
        if (pending) {
            m_process->sendMouseMove(*pending, currentViewSize());
            return;
        }

        KillTimer(kMouseMoveTimerId);
        m_mouseState.setPointerMoveTimerActive(false);
        return;
    }

    if (timerId == kResumeRecoveryTimerId) {
        KillTimer(kResumeRecoveryTimerId);
        // The timer outlives the handshake it was started for: firing it into
        // a reconnect that has not reached Running yet is what turned a dead
        // endpoint into a reconnect storm.
        const bool recoveryTimedOut = m_resolutionRecovery.onTimeout()
            || m_resumeRecovery.onTimeout() == RdpResumeRecovery::Action::Reconnect;
        if (recoveryTimedOut && canAutoReconnect(m_stateReachedRunning))
            restartSession(true);
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
    releaseAllPressedKeys();
    if (m_connected)
        updateCursorFromProcess();
}

void CRdpSessionView::OnKillFocus(CWnd *newWnd)
{
    CWnd::OnKillFocus(newWnd);
    flushPendingMouseMove();
    releasePressedMouseButtons();
    releaseAllPressedKeys();
    rdp::session_view_input::clearKeyboardTarget(this);
}

void CRdpSessionView::noteConsumedLocalShortcutKey(unsigned int virtualKey)
{
    m_keyboardState.noteConsumedLocalShortcutKey(virtualKey);
}

bool CRdpSessionView::consumeReservedShortcutKey(unsigned int virtualKey)
{
    return m_keyboardState.consumeReservedShortcutKey(virtualKey);
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

void CRdpSessionView::sendKeyboardAction(const RdpSessionKeyboardState::KeyAction &action)
{
    if (!m_process || m_inputSuspended)
        return;

    const unsigned int virtualKey = action.virtualKey != 0
        ? action.virtualKey
        : static_cast<unsigned int>(MapVirtualKeyW(action.key.scanCode, MAPVK_VSC_TO_VK_EX));
    m_process->sendKey(action.key, action.down, action.wasDown);
}

void CRdpSessionView::sendKeyboardActions(const std::vector<RdpSessionKeyboardState::KeyAction> &actions)
{
    for (const auto &action : actions)
        sendKeyboardAction(action);
}

void CRdpSessionView::releaseKeyboardTargetIfInactive()
{
    if (::GetFocus() != GetSafeHwnd()) {
        rdp::session_view_input::clearKeyboardTarget(this);
    }
}

void CRdpSessionView::sendTrackedMouseButton(MouseButton button, bool down, PointI point)
{
    if (!m_process || m_inputSuspended)
        return;

    m_process->sendMouseButton(button, down, point, currentViewSize());

    m_mouseState.noteButton(button, down);
}

void CRdpSessionView::releasePressedMouseButtons()
{
    const unsigned int pressedButtons = m_mouseState.pressedButtons();
    m_mouseState.clearPressedButtons();

    if (GetCapture() == this)
        ReleaseCapture();

    if (pressedButtons == 0)
        return;

    if (!m_process || m_process->state() != FreeRdpProcess::State::Running)
        return;

    const PointI point = m_mouseState.currentPointerPosition();
    if (pressedButtons & (1u << 0))
        sendTrackedMouseButton(MouseButton::Left, false, point);
    if (pressedButtons & (1u << 1))
        sendTrackedMouseButton(MouseButton::Right, false, point);
    if (pressedButtons & (1u << 2))
        sendTrackedMouseButton(MouseButton::Middle, false, point);
}

void CRdpSessionView::releaseAllPressedKeys()
{
    if (!m_process || m_process->state() != FreeRdpProcess::State::Running) {
        m_keyboardState.reset();
        return;
    }

    sendKeyboardActions(m_keyboardState.releaseAllPressedKeys());
}
