#include "RdpSessionView.h"

#include "common/AppPaths.h"
#include "common/Win32String.h"
#include "rdp/RdpCursorClassifier.h"
#include "ui/ParentResizeForwarder.h"
#include "ui/Win10Theme.h"

#include <algorithm>
#include <cstring>

namespace Gdiplus
{
using std::min;
using std::max;
}
#include <gdiplus.h>
#include <imm.h>

namespace
{
constexpr UINT_PTR kResizeTimerId = 1;
constexpr UINT_PTR kMouseMoveTimerId = 2;
constexpr UINT kMouseMoveCoalesceMs = 16;
constexpr int kInitialFrameDiscardWithCodec = 3;
constexpr int kInitialFrameDiscardNoCodec = 5;

CRdpSessionView *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;

int getJpegEncoderClsid(CLSID *clsid)
{
    if (!clsid)
        return -1;

    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
        return -1;

    auto buffer = std::make_unique<BYTE[]>(size);
    auto *encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.get());
    if (Gdiplus::GetImageEncoders(num, size, encoders) != Gdiplus::Ok)
        return -1;

    for (UINT i = 0; i < num; ++i) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/jpeg") == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }

    return -1;
}

std::wstring captureTimestamp()
{
    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    wchar_t buffer[64] = {};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u-%03u",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

bool isFrameCaptureEnabled()
{
    wchar_t value[8] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"RDPBOX_CAPTURE_FRAMES", value, static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value))
        return false;

    return value[0] == L'1'
        || value[0] == L't'
        || value[0] == L'T'
        || value[0] == L'y'
        || value[0] == L'Y';
}

bool saveFrameAsJpeg(const FrameBuffer &frame, const std::wstring &path)
{
    if (frame.empty())
        return false;

    CLSID jpegClsid = {};
    if (getJpegEncoderClsid(&jpegClsid) < 0)
        return false;

    Gdiplus::Bitmap bitmap(frame.width, frame.height, frame.stride, PixelFormat32bppARGB,
                           const_cast<BYTE *>(frame.pixels.data()));

    Gdiplus::EncoderParameters params = {};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    ULONG quality = 90;
    params.Parameter[0].Value = &quality;

    return bitmap.Save(path.c_str(), &jpegClsid, &params) == Gdiplus::Ok;
}

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
    releaseRenderSurface();
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

void CRdpSessionView::disconnect()
{
    stopProcess();
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
    beginResolutionUpdate(m_pendingResize);
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
        auto pending = std::make_shared<FreeRdpProcess::CertificateChallenge>(challenge);
        {
            std::scoped_lock lock(m_certMutex);
            m_pendingCert = std::move(pending);
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
    m_renderedFrameGeneration = 0;
    m_cachedFrame = {};
    m_pendingVisibleFrame = {};
    m_resolutionUpdatePending = false;
    m_waitingForFirstContentFrame = true;
    m_frameGateActive = true;
    m_frameGateRemaining = 0;
    m_pendingDesktopSize = {};
    m_captureDirectory.clear();
    m_captureFramesRemaining = 0;
    m_captureFrameIndex = 0;
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
    m_pendingDesktopSize = {};
    m_cachedFrameGeneration = 0;
    m_renderedFrameGeneration = 0;
    m_cachedFrame = {};
    m_pendingVisibleFrame = {};
    releaseRenderSurface();
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
        m_pendingDesktopSize = {};
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

void CRdpSessionView::beginResolutionUpdate(SizeI size)
{
    m_resolutionUpdatePending = true;
    m_pendingDesktopSize = SizeI{(size.width + 3) & ~3, size.height};
    m_frameGateActive = true;
    m_frameGateRemaining = kInitialFrameDiscardWithCodec;
    m_pendingVisibleFrame = {};
    beginFrameCapture(L"resize");
    showOverlay(L"Reconnecting...");
}

void CRdpSessionView::beginFrameCapture(const wchar_t *reason)
{
    m_captureDirectory.clear();
    m_captureFramesRemaining = 0;
    m_captureFrameIndex = 0;

    if (!isFrameCaptureEnabled())
        return;

    const std::wstring root = AppPaths::frameCaptureRootPath();
    if (root.empty())
        return;

    const std::wstring sessionName = m_profile.name.empty() ? L"unnamed" : m_profile.name;
    std::wstring safeName = sessionName;
    for (auto &ch : safeName) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            ch = L'_';
    }

    const std::wstring directory = root + L"\\" + captureTimestamp() + L"_" + safeName + L"_" + reason;
    ::CreateDirectoryW(directory.c_str(), nullptr);
    m_captureDirectory = directory;
    m_captureFramesRemaining = 20;
    m_captureFrameIndex = 0;
}

void CRdpSessionView::captureFrameIfRequested(const FrameBuffer &frame)
{
    if (m_captureFramesRemaining <= 0 || m_captureDirectory.empty() || frame.empty())
        return;

    wchar_t fileName[64] = {};
    swprintf_s(fileName, L"frame-%02d.jpg", m_captureFrameIndex + 1);
    const std::wstring path = m_captureDirectory + L"\\" + fileName;
    if (saveFrameAsJpeg(frame, path)) {
        ++m_captureFrameIndex;
        --m_captureFramesRemaining;
    }
}

void CRdpSessionView::syncMouseModifiers()
{
    if (!m_process)
        return;

    const std::vector<RdpModifierSyncTracker::KeyAction> actions =
        m_modifierTracker.synchronize(currentModifiers());
    for (const auto &action : actions)
        m_process->sendKeyMessage(action.message, action.virtualKey, 0);
}

unsigned int CRdpSessionView::currentModifiers() const
{
    unsigned int modifiers = ModifierNone;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        modifiers |= ModifierControl;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        modifiers |= ModifierShift;
    if (GetKeyState(VK_MENU) & 0x8000)
        modifiers |= ModifierAlt;
    return modifiers;
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

void CRdpSessionView::flushPendingMouseMove()
{
    if (!m_process) {
        if (m_mouseMoveTimerActive) {
            KillTimer(kMouseMoveTimerId);
            m_mouseMoveTimerActive = false;
        }
        return;
    }

    const auto pending = m_mouseMoveCoalescer.flush();
    if (pending)
        m_process->sendMouseMove(*pending, currentViewSize());

    if (m_mouseMoveTimerActive) {
        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
    }
}

void CRdpSessionView::releaseCursorHandle()
{
    if (m_cursorHandle && m_ownsCursorHandle)
        DestroyCursor(m_cursorHandle);

    m_cursorHandle = nullptr;
    m_ownsCursorHandle = false;
}

bool CRdpSessionView::ensureRenderSurface(const FrameBuffer &frame)
{
    if (frame.empty())
        return false;

    if (m_renderDc && m_renderBitmap && m_renderBits
        && m_renderWidth == frame.width
        && m_renderHeight == frame.height) {
        return true;
    }

    releaseRenderSurface();

    HDC screenDc = ::GetDC(nullptr);
    if (!screenDc)
        return false;

    m_renderDc = ::CreateCompatibleDC(screenDc);
    if (!m_renderDc) {
        ::ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = frame.width;
    bmi.bmiHeader.biHeight = -frame.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_renderBitmap = ::CreateDIBSection(m_renderDc, &bmi, DIB_RGB_COLORS, &m_renderBits, nullptr, 0);
    ::ReleaseDC(nullptr, screenDc);

    if (!m_renderBitmap || !m_renderBits) {
        releaseRenderSurface();
        return false;
    }

    m_renderOldBitmap = ::SelectObject(m_renderDc, m_renderBitmap);
    m_renderWidth = frame.width;
    m_renderHeight = frame.height;
    return true;
}

void CRdpSessionView::releaseRenderSurface()
{
    if (m_renderDc && m_renderOldBitmap) {
        ::SelectObject(m_renderDc, m_renderOldBitmap);
        m_renderOldBitmap = nullptr;
    }

    if (m_renderBitmap) {
        ::DeleteObject(m_renderBitmap);
        m_renderBitmap = nullptr;
    }

    if (m_renderDc) {
        ::DeleteDC(m_renderDc);
        m_renderDc = nullptr;
    }

    m_renderBits = nullptr;
    m_renderWidth = 0;
    m_renderHeight = 0;
    m_renderedFrameGeneration = 0;
}

void CRdpSessionView::copyFrameToRenderSurface(const FrameBuffer &frame)
{
    if (!m_renderBits || frame.empty())
        return;

    const int dstStride = m_renderWidth * 4;
    auto *dst = static_cast<std::uint8_t *>(m_renderBits);
    const auto *src = frame.pixels.data();

    if (frame.stride == dstStride) {
        std::memcpy(dst, src, static_cast<std::size_t>(dstStride) * static_cast<std::size_t>(frame.height));
        return;
    }

    for (int y = 0; y < frame.height; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstStride),
                    src + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride),
                    static_cast<std::size_t>(dstStride));
    }
}

void CRdpSessionView::drawRenderSurface(HDC targetDc, const CRect &targetRect) const
{
    if (!targetDc || !m_renderDc || !m_renderBitmap)
        return;

    if (targetRect.Width() == m_renderWidth && targetRect.Height() == m_renderHeight) {
        ::BitBlt(targetDc, 0, 0, m_renderWidth, m_renderHeight, m_renderDc, 0, 0, SRCCOPY);
        return;
    }

    ::SetStretchBltMode(targetDc, COLORONCOLOR);
    ::StretchBlt(targetDc,
                 0, 0, targetRect.Width(), targetRect.Height(),
                 m_renderDc,
                 0, 0, m_renderWidth, m_renderHeight,
                 SRCCOPY);
}

BOOL CRdpSessionView::OnEraseBkgnd(CDC *dc)
{
    UNREFERENCED_PARAMETER(dc);
    return TRUE;
}

void CRdpSessionView::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);
    bool hideOverlayAfterFramePresent = false;

    if (m_process) {
        auto newFrame = m_process->frameIfNewer(m_cachedFrameGeneration);
        if (newFrame) {
            captureFrameIfRequested(*newFrame);

            if (m_frameGateActive) {
                m_pendingVisibleFrame = std::move(*newFrame);
                if (m_frameGateRemaining > 0)
                    --m_frameGateRemaining;

                if (m_frameGateRemaining == 0 && !m_pendingVisibleFrame.empty()) {
                    m_cachedFrame = std::move(m_pendingVisibleFrame);
                    m_pendingVisibleFrame = {};
                    m_frameGateActive = false;
                    m_waitingForFirstContentFrame = false;
                    m_resolutionUpdatePending = false;
                    m_pendingDesktopSize = {};
                    hideOverlayAfterFramePresent = true;
                }
            } else {
                m_cachedFrame = std::move(*newFrame);
                if (m_waitingForFirstContentFrame) {
                    m_waitingForFirstContentFrame = false;
                    if (!m_resolutionUpdatePending)
                        hideOverlayAfterFramePresent = true;
                }
            }
        }

        const FrameBuffer &frame = m_cachedFrame;
        if (!frame.empty() && ensureRenderSurface(frame)) {
            if (m_renderedFrameGeneration != m_cachedFrameGeneration) {
                copyFrameToRenderSurface(frame);
                m_renderedFrameGeneration = m_cachedFrameGeneration;
            }
            drawRenderSurface(dc.GetSafeHdc(), rect);
            if (hideOverlayAfterFramePresent)
                m_overlayText.Empty();
        } else if (!frame.empty()) {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = frame.width;
            bmi.bmiHeader.biHeight = -frame.height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            if (rect.Width() == frame.width && rect.Height() == frame.height) {
                SetDIBitsToDevice(dc.GetSafeHdc(),
                                  0, 0,
                                  static_cast<DWORD>(frame.width),
                                  static_cast<DWORD>(frame.height),
                                  0, 0,
                                  0,
                                  static_cast<UINT>(frame.height),
                                  frame.pixels.data(),
                                  &bmi,
                                  DIB_RGB_COLORS);
            } else {
                StretchDIBits(dc.GetSafeHdc(), 0, 0, rect.Width(), rect.Height(), 0, 0,
                              frame.width, frame.height, frame.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            }
            if (hideOverlayAfterFramePresent)
                m_overlayText.Empty();
        } else {
            dc.FillSolidRect(rect, RGB(17, 17, 17));
        }
    } else {
        dc.FillSolidRect(rect, RGB(17, 17, 17));
    }

    if (!m_overlayText.IsEmpty())
        drawOverlay(dc, rect);
}

void CRdpSessionView::drawOverlay(CDC &dc, const CRect &rect)
{
    HDC overlayDc = ::CreateCompatibleDC(dc.GetSafeHdc());
    HBITMAP overlayBitmap = ::CreateCompatibleBitmap(dc.GetSafeHdc(), rect.Width(), rect.Height());
    HGDIOBJ oldBitmap = nullptr;
    if (overlayDc && overlayBitmap) {
        oldBitmap = ::SelectObject(overlayDc, overlayBitmap);

        RECT overlayRect = { 0, 0, rect.Width(), rect.Height() };
        HBRUSH overlayBrush = ::CreateSolidBrush(RGB(12, 12, 12));
        ::FillRect(overlayDc, &overlayRect, overlayBrush);
        ::DeleteObject(overlayBrush);

        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 176;
        ::AlphaBlend(dc.GetSafeHdc(), 0, 0, rect.Width(), rect.Height(),
                     overlayDc, 0, 0, rect.Width(), rect.Height(), blend);
        ::SelectObject(overlayDc, oldBitmap);
    }

    if (overlayBitmap)
        ::DeleteObject(overlayBitmap);
    if (overlayDc)
        ::DeleteDC(overlayDc);

    CRect textRect = rect;
    textRect.DeflateRect(48, 48);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(RGB(230, 230, 230));
    CFont *oldFont = nullptr;
    if (m_overlayFont.GetSafeHandle())
        oldFont = dc.SelectObject(&m_overlayFont);

    CRect measuredRect = textRect;
    dc.DrawText(m_overlayText, &measuredRect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);

    CRect centeredRect = textRect;
    const int textWidth = std::min(textRect.Width(), measuredRect.Width());
    const int textHeight = std::min(textRect.Height(), measuredRect.Height());
    centeredRect.left = textRect.left + (textRect.Width() - textWidth) / 2;
    centeredRect.top = textRect.top + (textRect.Height() - textHeight) / 2;
    centeredRect.right = centeredRect.left + textWidth;
    centeredRect.bottom = centeredRect.top + textHeight;

    dc.DrawText(m_overlayText, &centeredRect, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

    if (oldFont)
        dc.SelectObject(oldFont);
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
        beginResolutionUpdate(size);
        return;
    }

    if (m_resizeBurstTracker.onResize(size)) {
        beginResolutionUpdate(size);
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
        beginResolutionUpdate(size);
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

void CRdpSessionView::OnMouseMove(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    const int parentHit = ParentResizeForwarder::hitTestParentFrame(this, screenPoint);
    if (parentHit) {
        ParentResizeForwarder::applyResizeCursor(parentHit);
        return;
    }

    syncMouseModifiers();
    if (!m_process)
        return;

    const auto immediate = m_mouseMoveCoalescer.onMouseMove(PointI{point.x, point.y});
    if (immediate)
        m_process->sendMouseMove(*immediate, currentViewSize());

    if (!m_mouseMoveTimerActive) {
        SetTimer(kMouseMoveTimerId, kMouseMoveCoalesceMs, nullptr);
        m_mouseMoveTimerActive = true;
    }
}

void CRdpSessionView::OnLButtonDown(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    if (m_process && m_process->state() == FreeRdpProcess::State::Finished) {
        if (m_reconnectRequested)
            m_reconnectRequested();
        return;
    }

    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Left, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnLButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Left, false, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnLButtonDblClk(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Left, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnRButtonDown(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Right, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnRButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Right, false, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnRButtonDblClk(UINT flags, CPoint point)
{
    OnRButtonDown(flags, point);
}

void CRdpSessionView::OnMButtonDown(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Middle, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnMButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers();
    if (m_process)
        m_process->sendMouseButton(MouseButton::Middle, false, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnMButtonDblClk(UINT flags, CPoint point)
{
    OnMButtonDown(flags, point);
}

BOOL CRdpSessionView::OnMouseWheel(UINT flags, short zDelta, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers();
    if (m_process) {
        CPoint clientPoint(point);
        ScreenToClient(&clientPoint);
        m_process->sendWheel(PointI{0, zDelta}, PointI{clientPoint.x, clientPoint.y}, currentViewSize());
    }
    return TRUE;
}

UINT CRdpSessionView::OnGetDlgCode()
{
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}

LRESULT CRdpSessionView::OnRdpStateChanged(WPARAM state, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)))
        return 0;

    onStateChanged(static_cast<FreeRdpProcess::State>(state));
    return 0;
}

LRESULT CRdpSessionView::OnRdpFrameUpdated(WPARAM, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)))
        return 0;

    // Drain any queued frame messages so we only paint the latest.
    MSG msg = {};
    while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_RDP_FRAME, WM_APP_RDP_FRAME, PM_REMOVE)) {
        // discarded stale frame notification
    }

    Invalidate(FALSE);
    return 0;
}

LRESULT CRdpSessionView::OnRdpCursorUpdated(WPARAM, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)))
        return 0;

    updateCursorFromProcess();
    return 0;
}

LRESULT CRdpSessionView::OnRdpCertRequest(WPARAM, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)) || !m_process)
        return 0;

    std::shared_ptr<FreeRdpProcess::CertificateChallenge> challenge;
    {
        std::scoped_lock lock(m_certMutex);
        challenge = std::move(m_pendingCert);
    }

    bool accept = false;
    if (challenge) {
        CString message;
        message.Format(L"%s\n\nHost: %s:%d\nCommon Name: %s\nSubject: %s\nIssuer: %s\nFingerprint: %s\n\nAccept this certificate?",
                       challenge->changed
                           ? L"The remote host's certificate has CHANGED since the previous connection."
                           : L"The remote host's certificate could not be verified.",
                       challenge->host.c_str(), challenge->port,
                       challenge->commonName.c_str(),
                       challenge->subject.c_str(),
                       challenge->issuer.c_str(),
                       challenge->fingerprint.c_str());
        const UINT icon = challenge->changed ? MB_ICONWARNING : MB_ICONQUESTION;
        accept = MessageBox(message, L"Verify Certificate", MB_YESNO | icon | MB_DEFBUTTON2) == IDYES;
    }

    m_process->resolveCertificateChallenge(accept);
    return 0;
}

LRESULT CRdpSessionView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_MOUSEACTIVATE) {
        setFocusToFreeRdp();
        return MA_ACTIVATE;
    }

    if (message == WM_IME_SETCONTEXT
        || message == WM_IME_STARTCOMPOSITION
        || message == WM_IME_COMPOSITION
        || message == WM_IME_ENDCOMPOSITION
        || message == WM_IME_NOTIFY
        || message == WM_IME_CHAR
        || message == WM_CHAR
        || message == WM_SYSCHAR
        || message == WM_UNICHAR
        || message == WM_DEADCHAR
        || message == WM_SYSDEADCHAR) {
        return 0;
    }

    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
        if (m_process
            && m_process->state() == FreeRdpProcess::State::Running
            && !isInTopLevelResizeBorder()) {
            ::SetCursor(m_cursorHandle);
            return TRUE;
        }
    }

    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
        forwardNativeKeyMessage(static_cast<std::uint32_t>(message),
                                static_cast<std::uintptr_t>(wParam),
                                static_cast<std::intptr_t>(lParam));

        const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
        m_modifierTracker.recordKeyState(static_cast<unsigned int>(wParam), down);
        return 0;
    }

    return CWnd::WindowProc(message, wParam, lParam);
}

bool CRdpSessionView::canCaptureSystemKeys() const
{
    return m_process
        && m_process->state() == FreeRdpProcess::State::Running
        && GetSafeHwnd()
        && IsWindowVisible()
        && ::GetFocus() == GetSafeHwnd();
}

void CRdpSessionView::forwardNativeKeyMessage(std::uint32_t message,
                                              std::uintptr_t wParam,
                                              std::intptr_t lParam)
{
    if (!m_process)
        return;

    m_process->sendKeyMessage(message, wParam, lParam);
}
