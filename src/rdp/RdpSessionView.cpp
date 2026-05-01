#include "RdpSessionView.h"

#include "common/Win32String.h"
#include "rdp/FrameBufferMemory.h"
#include "rdp/RdpCursorClassifier.h"
#include "ui/ParentResizeForwarder.h"
#include "ui/Win10Theme.h"

#include <algorithm>

#include <imm.h>

namespace
{
constexpr UINT_PTR kResizeTimerId = 1;
constexpr UINT_PTR kMouseMoveTimerId = 2;
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

    m_process = std::make_unique<FreeRdpProcess>();
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

void CRdpSessionView::bindProcessCallbacks(std::uintptr_t generation)
{
    if (!m_process)
        return;

    m_process->setStateChangedCallback([this, generation](FreeRdpProcess::State state) {
        postProcessMessage(WM_APP_RDP_STATE, static_cast<WPARAM>(state), generation);
    });

    m_process->setFrameUpdatedCallback([this, generation]() {
        postProcessMessage(WM_APP_RDP_FRAME, 0, generation);
    });

    m_process->setDesktopResizedCallback([this, generation](const SizeI &) {
        postProcessMessage(WM_APP_RDP_FRAME, 0, generation);
    });

    m_process->setCursorUpdatedCallback([this, generation]() {
        postProcessMessage(WM_APP_RDP_CURSOR, 0, generation);
    });

    m_process->setCertificateChallengeCallback([this, generation](const FreeRdpProcess::CertificateChallenge &challenge) {
        {
            std::scoped_lock lock(m_certMutex);
            m_pendingCert = challenge;
        }
        postProcessMessage(WM_APP_RDP_CERT, 0, generation);
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

bool CRdpSessionView::postProcessMessage(UINT message, WPARAM wParam, std::uintptr_t generation) const
{
    const HWND hwnd = GetSafeHwnd();
    if (!hwnd || !::IsWindow(hwnd))
        return false;

    return ::PostMessageW(hwnd, message, wParam, static_cast<LPARAM>(generation)) != FALSE;
}

bool CRdpSessionView::isCurrentGeneration(std::uintptr_t generation) const
{
    return generation == m_processGeneration;
}

bool CRdpSessionView::isInTopLevelResizeBorder() const
{
    const HWND hwnd = GetSafeHwnd();
    if (!hwnd)
        return false;

    HWND root = ::GetAncestor(hwnd, GA_ROOT);
    if (!root)
        root = ::GetParent(hwnd);
    if (!root)
        return false;

    POINT cursorPos = {};
    if (!::GetCursorPos(&cursorPos))
        return false;

    RECT windowRect = {};
    if (!::GetWindowRect(root, &windowRect))
        return false;

    const int frameX = std::max(0, ::GetSystemMetrics(SM_CXSIZEFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER));
    const int frameY = std::max(0, ::GetSystemMetrics(SM_CYSIZEFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER));
    if (frameX == 0 && frameY == 0)
        return false;

    const bool nearLeft = cursorPos.x >= windowRect.left && cursorPos.x < (windowRect.left + frameX);
    const bool nearRight = cursorPos.x < windowRect.right && cursorPos.x >= (windowRect.right - frameX);
    const bool nearTop = cursorPos.y >= windowRect.top && cursorPos.y < (windowRect.top + frameY);
    const bool nearBottom = cursorPos.y < windowRect.bottom && cursorPos.y >= (windowRect.bottom - frameY);
    return nearLeft || nearRight || nearTop || nearBottom;
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
    {
        std::scoped_lock lock(m_certMutex);
        m_pendingCert.reset();
    }
    m_resizeBurstTracker.reset();
    m_modifierTracker.reset();
    m_mouseMoveCoalescer.reset();
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
    if (m_mouseMoveTimerActive) {
        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
    }
    ++m_processGeneration;

    if (m_process) {
        clearProcessCallbacks();
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
    {
        std::scoped_lock lock(m_certMutex);
        m_pendingCert.reset();
    }
    m_resizeBurstTracker.reset();
    m_modifierTracker.reset();
    m_mouseMoveCoalescer.reset();
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
        m_resizeBurstTracker.reset();
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

    if (::GetFocus() != GetSafeHwnd())
        SetFocus();

    g_systemKeyTarget = this;
    ensureKeyboardHook();
    m_process->sendFocusIn();
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
    beginFrameCapture(L"resize");
    showOverlay(L"Reconnecting...");
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
    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
}
