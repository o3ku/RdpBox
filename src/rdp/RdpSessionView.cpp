#include "RdpSessionView.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <QBitmap>
#include <QCursor>
#include <QImage>
#include <QPixmap>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace
{
constexpr UINT_PTR kResizeTimerId = 1;

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

bool shouldCaptureLowLevelKey(const KBDLLHOOKSTRUCT *info)
{
    if (!info)
        return false;

    return isSystemKey(info->vkCode)
        || isAltKey(info->vkCode)
        || (info->vkCode == VK_TAB && (info->flags & LLKHF_ALTDOWN));
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION || !g_systemKeyTarget)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (!shouldCaptureLowLevelKey(info) || !g_systemKeyTarget->canCaptureSystemKeys())
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
    const bool sysContext = isAltKey(info->vkCode) || (info->flags & LLKHF_ALTDOWN);
    const quint32 message = keyUp
        ? (sysContext ? WM_SYSKEYUP : WM_KEYUP)
        : (sysContext ? WM_SYSKEYDOWN : WM_KEYDOWN);

    g_systemKeyTarget->forwardNativeKeyMessage(message, info->vkCode,
                                               static_cast<qintptr>((info->scanCode & 0xFFu) << 16)
                                               | (extended ? 0x01000000 : 0)
                                               | (keyUp ? 0xC0000000 : 0));
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

QImage imageFromQPixmap(const QPixmap &pixmap)
{
    return pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
}

HCURSOR createCursorFromImage(const QImage &image, const QPoint &hotspot)
{
    if (image.isNull())
        return nullptr;

    QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = argb.width();
    bi.bV5Height = -argb.height();
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void *bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP dib = CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                   &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib)
            DeleteObject(dib);
        if (memDc)
            DeleteDC(memDc);
        if (screenDc)
            ReleaseDC(nullptr, screenDc);
        return nullptr;
    }

    memcpy(bits, argb.bits(), static_cast<size_t>(argb.sizeInBytes()));
    HBITMAP mask = CreateBitmap(argb.width(), argb.height(), 1, 1, nullptr);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = FALSE;
    iconInfo.xHotspot = static_cast<DWORD>(std::clamp(hotspot.x(), 0, std::max(0, argb.width() - 1)));
    iconInfo.yHotspot = static_cast<DWORD>(std::clamp(hotspot.y(), 0, std::max(0, argb.height() - 1)));
    iconInfo.hbmColor = dib;
    iconInfo.hbmMask = mask;

    HCURSOR cursor = CreateIconIndirect(&iconInfo);

    if (mask)
        DeleteObject(mask);
    if (dib)
        DeleteObject(dib);
    if (memDc)
        DeleteDC(memDc);
    if (screenDc)
        ReleaseDC(nullptr, screenDc);

    return cursor;
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
    ON_WM_LBUTTONUP()
    ON_WM_RBUTTONDOWN()
    ON_WM_RBUTTONUP()
    ON_WM_MBUTTONDOWN()
    ON_WM_MBUTTONUP()
    ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

CRdpSessionView::CRdpSessionView() = default;

CRdpSessionView::~CRdpSessionView()
{
    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
    disconnectSignals();
    stopProcess();
    releaseCursorHandle();
}

bool CRdpSessionView::create(CWnd *parent, const CRect &rect)
{
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                                  nullptr);

    m_created = CreateEx(0, className, L"RdpSessionView",
                         WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                         rect, parent, 0) != FALSE;
    if (m_created)
        SetFocus();
    return m_created;
}

void CRdpSessionView::setReconnectRequestedCallback(std::function<void()> callback)
{
    m_reconnectRequested = std::move(callback);
}

void CRdpSessionView::connectToHost(const Profile &profile)
{
    m_profile = profile;
    stopProcess();
    disconnectSignals();
    m_process = std::make_unique<FreeRdpProcess>();
    connectSignals();
    startProcess();
}

void CRdpSessionView::reconnect()
{
    if (!m_profile.isValid())
        return;

    connectToHost(m_profile);
}

void CRdpSessionView::disconnect()
{
    stopProcess();
}

void CRdpSessionView::connectSignals()
{
    if (!m_process)
        return;

    m_stateConnection = QObject::connect(m_process.get(), &FreeRdpProcess::stateChanged,
        [this](FreeRdpProcess::State state) {
            onStateChanged(state);
        });

    m_frameConnection = QObject::connect(m_process.get(), &FreeRdpProcess::frameUpdated,
        [this]() {
            if (GetSafeHwnd())
                Invalidate(FALSE);
        });

    m_desktopConnection = QObject::connect(m_process.get(), &FreeRdpProcess::desktopResized,
        [this](const QSize &) {
            if (GetSafeHwnd())
                Invalidate(FALSE);
        });

    m_cursorConnection = QObject::connect(m_process.get(), &FreeRdpProcess::cursorUpdated,
        [this]() {
            updateCursorFromProcess();
        });
}

void CRdpSessionView::disconnectSignals()
{
    if (m_stateConnection)
        QObject::disconnect(m_stateConnection);
    if (m_frameConnection)
        QObject::disconnect(m_frameConnection);
    if (m_desktopConnection)
        QObject::disconnect(m_desktopConnection);
    if (m_cursorConnection)
        QObject::disconnect(m_cursorConnection);

    m_stateConnection = {};
    m_frameConnection = {};
    m_desktopConnection = {};
    m_cursorConnection = {};
}

void CRdpSessionView::startProcess()
{
    if (!m_process || !GetSafeHwnd())
        return;

    CRect rect;
    GetClientRect(&rect);

    m_connected = false;
    m_resizeBurstTracker.reset();
    m_modifierTracker.reset();
    showOverlay(L"Connecting...");

    m_process->start(m_profile.host, m_profile.port,
                     m_profile.username, m_profile.password,
                     std::max(1, rect.Width()), std::max(1, rect.Height()),
                     m_profile.clipboardEnabled, m_profile.ignoreCertificate);
}

void CRdpSessionView::stopProcess()
{
    if (m_process)
        m_process->stop();

    m_connected = false;
    m_resizeBurstTracker.reset();
    m_modifierTracker.reset();
    showOverlay(L"Disconnected - Click to Reconnect");
    releaseCursorHandle();
}

void CRdpSessionView::onStateChanged(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Running:
        m_connected = true;
        clearOverlay();
        m_modifierTracker.reset();
        m_resizeBurstTracker.reset();
        updateCursorFromProcess();
        setFocusToFreeRdp();
        if (m_process) {
            CRect rect;
            GetClientRect(&rect);
            m_process->requestResize(QSize(std::max(1, rect.Width()), std::max(1, rect.Height())));
        }
        Invalidate(FALSE);
        break;
    case FreeRdpProcess::State::Finished:
        m_connected = false;
        showOverlay(L"Disconnected - Click to Reconnect");
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
    const QCursor cursor = m_process->cursor();
    m_cursorHandle = cursorHandleFromQtCursor(cursor);
    if (GetSafeHwnd())
        ::SetCursor(m_cursorHandle ? m_cursorHandle : ::LoadCursor(nullptr, IDC_ARROW));
}

void CRdpSessionView::setFocusToFreeRdp()
{
    if (!m_process)
        return;

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

void CRdpSessionView::syncMouseModifiers(UINT flags)
{
    UNREFERENCED_PARAMETER(flags);
    if (!m_process)
        return;

    const QVector<RdpModifierSyncTracker::KeyAction> actions = m_modifierTracker.synchronize(currentModifiers());
    for (const auto &action : actions)
        m_process->sendKeyMessage(action.message, action.virtualKey, 0);
}

Qt::KeyboardModifiers CRdpSessionView::currentModifiers() const
{
    Qt::KeyboardModifiers modifiers;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        modifiers |= Qt::ControlModifier;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        modifiers |= Qt::ShiftModifier;
    if (GetKeyState(VK_MENU) & 0x8000)
        modifiers |= Qt::AltModifier;
    return modifiers;
}

QSize CRdpSessionView::currentViewSize() const
{
    CRect rect;
    GetClientRect(&rect);
    return QSize(std::max(1, rect.Width()), std::max(1, rect.Height()));
}

HCURSOR CRdpSessionView::cursorHandleFromPixmap(const QPixmap &pixmap, const QPoint &hotspot)
{
    return createCursorFromImage(imageFromQPixmap(pixmap), hotspot);
}

HCURSOR CRdpSessionView::cursorHandleFromQtCursor(const QCursor &cursor)
{
    if (cursor.shape() == Qt::BlankCursor)
        return ::LoadCursor(nullptr, IDC_ARROW);

    switch (cursor.shape()) {
    case Qt::ArrowCursor: return ::LoadCursor(nullptr, IDC_ARROW);
    case Qt::IBeamCursor: return ::LoadCursor(nullptr, IDC_IBEAM);
    case Qt::CrossCursor: return ::LoadCursor(nullptr, IDC_CROSS);
    case Qt::WaitCursor: return ::LoadCursor(nullptr, IDC_WAIT);
    case Qt::BusyCursor: return ::LoadCursor(nullptr, IDC_APPSTARTING);
    case Qt::PointingHandCursor: return ::LoadCursor(nullptr, IDC_HAND);
    case Qt::SizeHorCursor: return ::LoadCursor(nullptr, IDC_SIZEWE);
    case Qt::SizeVerCursor: return ::LoadCursor(nullptr, IDC_SIZENS);
    case Qt::SizeFDiagCursor: return ::LoadCursor(nullptr, IDC_SIZENWSE);
    case Qt::SizeBDiagCursor: return ::LoadCursor(nullptr, IDC_SIZENESW);
    case Qt::SizeAllCursor: return ::LoadCursor(nullptr, IDC_SIZEALL);
    case Qt::BitmapCursor:
        return cursorHandleFromPixmap(cursor.pixmap(), cursor.hotSpot());
    default:
        if (!cursor.pixmap().isNull())
            return cursorHandleFromPixmap(cursor.pixmap(), cursor.hotSpot());
        return ::LoadCursor(nullptr, IDC_ARROW);
    }
}

void CRdpSessionView::releaseCursorHandle()
{
    if (m_cursorHandle && m_ownsCursorHandle)
        DestroyCursor(m_cursorHandle);

    m_cursorHandle = nullptr;
    m_ownsCursorHandle = false;
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
    dc.FillSolidRect(rect, RGB(17, 17, 17));

    if (m_process) {
        const QImage frame = m_process->frame();
        if (!frame.isNull()) {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = frame.width();
            bmi.bmiHeader.biHeight = -frame.height();
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc.GetSafeHdc(), 0, 0, rect.Width(), rect.Height(), 0, 0,
                          frame.width(), frame.height(), frame.constBits(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        }
    }

    if (!m_overlayText.IsEmpty()) {
        CRect overlayRect = rect;
        overlayRect.DeflateRect(20, 20);
        dc.FillSolidRect(overlayRect, RGB(30, 30, 30));
        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(204, 204, 204));
        dc.DrawText(m_overlayText, &overlayRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_WORDBREAK);
    }
}

void CRdpSessionView::OnSize(UINT type, int cx, int cy)
{
    CWnd::OnSize(type, cx, cy);

    if (!m_connected || !m_process)
        return;

    if (m_resizeBurstTracker.onResize(QSize(std::max(1, cx), std::max(1, cy)))) {
        m_process->requestResize(QSize(std::max(1, cx), std::max(1, cy)));
        SetTimer(kResizeTimerId, 50, nullptr);
    }
}

void CRdpSessionView::OnTimer(UINT_PTR timerId)
{
    if (timerId != kResizeTimerId) {
        CWnd::OnTimer(timerId);
        return;
    }

    if (!m_connected || !m_process) {
        KillTimer(kResizeTimerId);
        m_resizeBurstTracker.reset();
        return;
    }

    const QSize size = currentViewSize();
    if (m_resizeBurstTracker.onTimeout(size)) {
        m_process->requestResize(size);
        return;
    }

    KillTimer(kResizeTimerId);
}

void CRdpSessionView::OnSetFocus(CWnd *oldWnd)
{
    CWnd::OnSetFocus(oldWnd);
    setFocusToFreeRdp();
}

void CRdpSessionView::OnKillFocus(CWnd *newWnd)
{
    CWnd::OnKillFocus(newWnd);
    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
}

void CRdpSessionView::OnMouseMove(UINT flags, CPoint point)
{
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseMove(QPoint(point.x, point.y), currentViewSize());
}

void CRdpSessionView::OnLButtonDown(UINT flags, CPoint point)
{
    if (m_process && m_process->state() == FreeRdpProcess::State::Finished) {
        if (m_reconnectRequested)
            m_reconnectRequested();
        return;
    }

    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(Qt::LeftButton, true, QPoint(point.x, point.y), currentViewSize());
}

void CRdpSessionView::OnLButtonUp(UINT flags, CPoint point)
{
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(Qt::LeftButton, false, QPoint(point.x, point.y), currentViewSize());
}

void CRdpSessionView::OnRButtonDown(UINT flags, CPoint point)
{
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(Qt::RightButton, true, QPoint(point.x, point.y), currentViewSize());
}

void CRdpSessionView::OnRButtonUp(UINT flags, CPoint point)
{
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(Qt::RightButton, false, QPoint(point.x, point.y), currentViewSize());
}

void CRdpSessionView::OnMButtonDown(UINT flags, CPoint point)
{
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(Qt::MiddleButton, true, QPoint(point.x, point.y), currentViewSize());
}

void CRdpSessionView::OnMButtonUp(UINT flags, CPoint point)
{
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(Qt::MiddleButton, false, QPoint(point.x, point.y), currentViewSize());
}

BOOL CRdpSessionView::OnMouseWheel(UINT flags, short zDelta, CPoint point)
{
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendWheel(QPoint(0, zDelta), QPoint(point.x, point.y), currentViewSize());
    return TRUE;
}

UINT CRdpSessionView::OnGetDlgCode()
{
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}

LRESULT CRdpSessionView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_MOUSEACTIVATE) {
        setFocusToFreeRdp();
        return MA_ACTIVATE;
    }

    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
        forwardNativeKeyMessage(static_cast<quint32>(message), wParam, lParam);
        const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
        switch (wParam) {
        case VK_CONTROL:
            m_modifierTracker.recordKeyState(Qt::Key_Control, down);
            break;
        case VK_SHIFT:
            m_modifierTracker.recordKeyState(Qt::Key_Shift, down);
            break;
        case VK_MENU:
            m_modifierTracker.recordKeyState(Qt::Key_Alt, down);
            break;
        default:
            break;
        }
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

void CRdpSessionView::forwardNativeKeyMessage(quint32 message, quintptr wParam, qintptr lParam)
{
    if (!m_process)
        return;

    m_process->sendKeyMessage(message, wParam, lParam);
}
