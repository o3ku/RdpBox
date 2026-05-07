#include "MainWindow.h"

#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/Win10Theme.h"
#include "ui/WindowStateScaling.h"
#include "ui/WindowFrameMetrics.h"
#include "resources/resource.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>

namespace
{
constexpr int kCaptionHeight = 34;
constexpr int kLogoSize = 22;
constexpr int kLogoLeftPadding = 8;
constexpr int kLogoRightPadding = 8;
constexpr int kCaptionButtonWidth = 46;
constexpr int kSystemButtonReserve = kCaptionButtonWidth * 3;
constexpr int kResizeBorderTop = 6;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmcpDoNotRound = 1;
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;

void adjustMaximizedClientRect(RECT &rect)
{
    const int frameX = ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
    const int frameY = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
    rect.left += frameX;
    rect.right -= frameX;
    rect.top += frameY;
    rect.bottom -= frameY;
}

void applyDwmExtension(HWND hwnd, const WindowFrameMetrics &metrics)
{
    if (!hwnd)
        return;

    MARGINS margins = { 0, 0, metrics.dwmTopInset, 0 };
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    ::DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorNone, sizeof(kDwmColorNone));
    ::DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &kDwmcpDoNotRound, sizeof(kDwmcpDoNotRound));

    RECT windowRect = {};
    if (!::GetWindowRect(hwnd, &windowRect))
        return;

    const int width = std::max(1, static_cast<int>(windowRect.right - windowRect.left));
    const int height = std::max(1, static_cast<int>(windowRect.bottom - windowRect.top));
    HRGN region = ::CreateRectRgn(0, 0, width, height);
    if (!region)
        return;

    if (!::SetWindowRgn(hwnd, region, FALSE))
        ::DeleteObject(region);
}

CRect logoHoverRectFor(const WindowFrameMetrics &metrics)
{
    return CRect(0, metrics.clientEdgeInset,
                 kLogoLeftPadding + kLogoSize + kLogoRightPadding,
                 kCaptionHeight);
}

bool monitorWorkAreaForRect(const RECT &rect, RECT &workArea)
{
    const HMONITOR monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return false;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return false;

    workArea = info.rcWork;
    return true;
}

bool monitorInfoForRect(const RECT &rect, RECT &workArea, std::wstring &deviceName)
{
    const HMONITOR monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return false;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return false;

    workArea = info.rcWork;
    deviceName = info.szDevice;
    return true;
}

struct MonitorLookupContext
{
    const wchar_t *deviceName = nullptr;
    RECT workArea = {};
    bool found = false;
};

BOOL CALLBACK findMonitorByDeviceName(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
{
    auto *context = reinterpret_cast<MonitorLookupContext *>(userData);
    if (!context || !context->deviceName)
        return TRUE;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return TRUE;

    if (::wcscmp(info.szDevice, context->deviceName) != 0)
        return TRUE;

    context->workArea = info.rcWork;
    context->found = true;
    return FALSE;
}

bool monitorWorkAreaForDeviceName(const std::wstring &deviceName, RECT &workArea)
{
    if (deviceName.empty())
        return false;

    MonitorLookupContext context;
    context.deviceName = deviceName.c_str();
    ::EnumDisplayMonitors(nullptr, nullptr, &findMonitorByDeviceName, reinterpret_cast<LPARAM>(&context));
    if (!context.found)
        return false;

    workArea = context.workArea;
    return true;
}

bool activeMonitorWorkArea(RECT &workArea)
{
    POINT point = {};
    if (!::GetCursorPos(&point))
        point = POINT{0, 0};

    const HMONITOR monitor = ::MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
    if (!monitor)
        return false;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return false;

    workArea = info.rcWork;
    return true;
}
}

BOOL MainWindow::OnEraseBkgnd(CDC *dc)
{
    if (!dc)
        return FALSE;

    CRect clientRect;
    GetClientRect(&clientRect);

    const WindowFrameMetrics metrics = calculateWindowFrameMetrics(isMaximized(), m_isFullScreen);
    dc->FillSolidRect(clientRect, metrics.backgroundColor);
    return TRUE;
}

BOOL MainWindow::OnNcActivate(BOOL active)
{
    UNREFERENCED_PARAMETER(active);
    return TRUE;
}

LRESULT MainWindow::OnNcCalcSize(WPARAM wParam, LPARAM lParam)
{
    if (isMaximized()) {
        if (!lParam)
            return 0;

        if (!wParam) {
            auto *rect = reinterpret_cast<RECT *>(lParam);
            adjustMaximizedClientRect(*rect);
            return 0;
        }

        auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);
        adjustMaximizedClientRect(params->rgrc[0]);
    }

    return 0;
}

LRESULT MainWindow::OnNcLButtonDown(WPARAM hitTest, LPARAM)
{
    if (hitTest >= HTLEFT && hitTest <= HTBOTTOMRIGHT && m_sessionManager)
        m_sessionManager->setResizeSuppressed(true);

    return Default();
}

LRESULT MainWindow::OnNcHitTest(WPARAM, LPARAM lParam)
{
    if (m_isFullScreen)
        return HTCLIENT;

    POINT clientPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ScreenToClient(&clientPoint);

    CRect clientRect;
    GetClientRect(&clientRect);

    if (!isMaximized()) {
        const bool nearLeft = clientPoint.x < kResizeBorderTop;
        const bool nearRight = clientPoint.x >= clientRect.right - kResizeBorderTop;
        const bool nearTop = clientPoint.y < kResizeBorderTop;
        const bool nearBottom = clientPoint.y >= clientRect.bottom - kResizeBorderTop;

        if (nearTop && nearLeft)
            return HTTOPLEFT;
        if (nearTop && nearRight)
            return HTTOPRIGHT;
        if (nearBottom && nearLeft)
            return HTBOTTOMLEFT;
        if (nearBottom && nearRight)
            return HTBOTTOMRIGHT;
        if (nearTop)
            return HTTOP;
        if (nearBottom)
            return HTBOTTOM;
        if (nearLeft)
            return HTLEFT;
        if (nearRight)
            return HTRIGHT;
    }

    return HTCLIENT;
}

LRESULT MainWindow::OnDwmCompositionChanged(WPARAM, LPARAM)
{
    applyDwmExtension(GetSafeHwnd(), calculateWindowFrameMetrics(isMaximized(), m_isFullScreen));
    return 0;
}

LRESULT MainWindow::OnNcPaint(WPARAM wParam, LPARAM)
{
    if (!isMaximized() || m_isFullScreen)
        return Default();

    HDC hdc = ::GetWindowDC(GetSafeHwnd());
    if (!hdc)
        return 0;

    HBRUSH brush = ::CreateSolidBrush(Win10Theme::kCaptionBg);
    if (wParam && reinterpret_cast<HRGN>(wParam) != reinterpret_cast<HRGN>(1)) {
        ::FillRgn(hdc, reinterpret_cast<HRGN>(wParam), brush);
    } else {
        CRect winRect;
        CRect clientRect;
        GetWindowRect(&winRect);
        GetClientRect(&clientRect);

        POINT clientOrigin = { 0, 0 };
        ::ClientToScreen(GetSafeHwnd(), &clientOrigin);
        CRect clientInWindow(clientOrigin.x - winRect.left,
                             clientOrigin.y - winRect.top,
                             clientOrigin.x - winRect.left + clientRect.Width(),
                             clientOrigin.y - winRect.top + clientRect.Height());

        HRGN winRgn = ::CreateRectRgn(0, 0, winRect.Width(), winRect.Height());
        HRGN clientRgn = ::CreateRectRgnIndirect(&clientInWindow);
        HRGN frameRgn = ::CreateRectRgn(0, 0, 0, 0);
        ::CombineRgn(frameRgn, winRgn, clientRgn, RGN_DIFF);
        ::FillRgn(hdc, frameRgn, brush);
        ::DeleteObject(frameRgn);
        ::DeleteObject(clientRgn);
        ::DeleteObject(winRgn);
    }

    ::DeleteObject(brush);
    ::ReleaseDC(GetSafeHwnd(), hdc);
    return 0;
}

void MainWindow::OnLButtonDown(UINT flags, CPoint point)
{
    if (logoHitTest(point)) {
        showLogoMenu();
        return;
    }

    const int hit = captionButtonHitTest(point);
    if (hit != 0) {
        UINT command = SC_CLOSE;
        if (hit == HTMINBUTTON)
            command = SC_MINIMIZE;
        else if (hit == HTMAXBUTTON)
            command = isMaximized() ? SC_RESTORE : SC_MAXIMIZE;
        SendMessage(WM_SYSCOMMAND, command, 0);
        return;
    }

    if (point.y < kCaptionHeight && !m_isFullScreen) {
        ReleaseCapture();
        SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return;
    }

    CFrameWnd::OnLButtonDown(flags, point);
}

void MainWindow::OnLButtonDblClk(UINT flags, CPoint point)
{
    if (point.y < kCaptionHeight && !m_isFullScreen && captionButtonHitTest(point) == 0 && !logoHitTest(point)) {
        SendMessage(WM_SYSCOMMAND, isMaximized() ? SC_RESTORE : SC_MAXIMIZE, 0);
        return;
    }

    CFrameWnd::OnLButtonDblClk(flags, point);
}

void MainWindow::OnMouseMove(UINT flags, CPoint point)
{
    const int hit = captionButtonHitTest(point);
    if (hit != m_hoverCaptionButton) {
        m_hoverCaptionButton = hit;
        invalidateCaptionButtons();
    }

    const bool logoHovered = logoHitTest(point);
    if (logoHovered != m_logoHovered) {
        m_logoHovered = logoHovered;
        InvalidateRect(logoRect(), FALSE);
    }

    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = GetSafeHwnd();
        ::TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    CFrameWnd::OnMouseMove(flags, point);
}

void MainWindow::OnMouseLeave()
{
    m_trackingMouse = false;
    if (m_hoverCaptionButton != 0) {
        m_hoverCaptionButton = 0;
        invalidateCaptionButtons();
    }
    if (m_logoHovered) {
        m_logoHovered = false;
        InvalidateRect(logoRect(), FALSE);
    }
}

bool MainWindow::isMaximized() const
{
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!const_cast<MainWindow *>(this)->GetWindowPlacement(&placement))
        return false;
    return placement.showCmd == SW_SHOWMAXIMIZED;
}

void MainWindow::refreshDwmFrame()
{
    applyDwmExtension(GetSafeHwnd(), calculateWindowFrameMetrics(isMaximized(), m_isFullScreen));
}

void MainWindow::saveWindowState() const
{
    if (!GetSafeHwnd() || m_isFullScreen || !m_profileRepository)
        return;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!const_cast<MainWindow *>(this)->GetWindowPlacement(&placement))
        return;

    RECT workArea = {};
    std::wstring deviceName;
    if (!monitorInfoForRect(placement.rcNormalPosition, workArea, deviceName))
        return;

    WindowState state;
    if (!WindowStateScaling::saveToMonitorWorkArea(placement.rcNormalPosition,
                                                   workArea,
                                                   static_cast<int>(placement.showCmd),
                                                   state)) {
        return;
    }

    state.monitorDeviceName = deviceName;

    m_profileRepository->saveWindowState(state);
}

bool MainWindow::restoreWindowState()
{
    if (!m_profileRepository)
        return false;

    const WindowState state = m_profileRepository->loadWindowState();
    if (!state.valid)
        return false;

    RECT workArea = {};
    if (!monitorWorkAreaForDeviceName(state.monitorDeviceName, workArea)
        && !activeMonitorWorkArea(workArea)) {
        return false;
    }

    RECT restoredRect = {};
    if (!WindowStateScaling::restoreFromMonitorWorkArea(state, workArea, restoredRect))
        return false;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    placement.rcNormalPosition = restoredRect;
    placement.showCmd = state.showCmd;

    SetWindowPlacement(&placement);
    return true;
}

CRect MainWindow::captionButtonRectFor(int hitCode) const
{
    CRect clientRect;
    const_cast<MainWindow *>(this)->GetClientRect(&clientRect);

    int order = -1;
    if (hitCode == HTCLOSE)
        order = 0;
    else if (hitCode == HTMAXBUTTON)
        order = 1;
    else if (hitCode == HTMINBUTTON)
        order = 2;

    if (order < 0)
        return CRect();

    const int right = clientRect.right - order * kCaptionButtonWidth;
    const int left = right - kCaptionButtonWidth;
    return CRect(left, 0, right, kCaptionHeight);
}

int MainWindow::captionButtonHitTest(CPoint clientPoint) const
{
    if (clientPoint.y < 0 || clientPoint.y >= kCaptionHeight)
        return 0;

    if (captionButtonRectFor(HTCLOSE).PtInRect(clientPoint))
        return HTCLOSE;
    if (captionButtonRectFor(HTMAXBUTTON).PtInRect(clientPoint))
        return HTMAXBUTTON;
    if (captionButtonRectFor(HTMINBUTTON).PtInRect(clientPoint))
        return HTMINBUTTON;
    return 0;
}

void MainWindow::invalidateCaptionButtons()
{
    CRect rect = captionButtonRectFor(HTMINBUTTON);
    rect.right = captionButtonRectFor(HTCLOSE).right;
    if (!rect.IsRectEmpty())
        InvalidateRect(rect, FALSE);
}

void MainWindow::drawCaptionButton(CDC &dc, const CRect &rect, int hitCode) const
{
    const bool hovered = (m_hoverCaptionButton == hitCode);
    COLORREF background = Win10Theme::kCaptionBg;
    COLORREF glyphColor = Win10Theme::kCaptionText;

    if (hovered) {
        if (hitCode == HTCLOSE) {
            background = Win10Theme::kCloseHover;
            glyphColor = Win10Theme::kCloseHoverText;
        } else {
            background = Win10Theme::kCaptionButtonHover;
        }
    }

    dc.FillSolidRect(rect, background);

    CPen pen(PS_SOLID, 1, glyphColor);
    CPen *oldPen = dc.SelectObject(&pen);
    const int cx = rect.left + rect.Width() / 2;
    const int cy = rect.top + rect.Height() / 2;
    constexpr int kGlyph = 5;

    if (hitCode == HTMINBUTTON) {
        dc.MoveTo(cx - kGlyph, cy);
        dc.LineTo(cx + kGlyph + 1, cy);
    } else if (hitCode == HTMAXBUTTON) {
        CBrush hollow;
        hollow.CreateStockObject(NULL_BRUSH);
        CBrush *oldBrush = dc.SelectObject(&hollow);
        if (isMaximized()) {
            CRect inner1(cx - kGlyph + 1, cy - kGlyph + 1, cx + kGlyph - 1, cy + kGlyph - 1);
            CRect inner2(cx - kGlyph + 3, cy - kGlyph - 1, cx + kGlyph + 1, cy + kGlyph - 3);
            dc.Rectangle(inner1);
            dc.Rectangle(inner2);
        } else {
            CRect inner(cx - kGlyph, cy - kGlyph, cx + kGlyph + 1, cy + kGlyph + 1);
            dc.Rectangle(inner);
        }
        dc.SelectObject(oldBrush);
    } else if (hitCode == HTCLOSE) {
        dc.MoveTo(cx - kGlyph, cy - kGlyph);
        dc.LineTo(cx + kGlyph + 1, cy + kGlyph + 1);
        dc.MoveTo(cx - kGlyph, cy + kGlyph);
        dc.LineTo(cx + kGlyph + 1, cy - kGlyph - 1);
    }

    dc.SelectObject(oldPen);
}

CRect MainWindow::logoRect() const
{
    return CRect(0, 0, kLogoLeftPadding + kLogoSize + kLogoRightPadding, kCaptionHeight);
}

bool MainWindow::logoHitTest(CPoint clientPoint) const
{
    return !m_isFullScreen && logoRect().PtInRect(clientPoint);
}

void MainWindow::showLogoMenu()
{
    CRect rect = logoRect();
    ClientToScreen(&rect);

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_MAIN_NEW, L"New\tCtrl+N");
    menu.AppendMenu(MF_STRING, ID_MAIN_CONNECTIONS, L"Connections\tCtrl+O");
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, ID_MAIN_ABOUT, L"About");
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN, rect.left, rect.bottom, this);
}

void MainWindow::OnPaint()
{
    PAINTSTRUCT ps = {};
    HDC hdc = ::BeginPaint(GetSafeHwnd(), &ps);

    CRect clientRect;
    GetClientRect(&clientRect);
    CRect captionRect(0, 0, clientRect.right, kCaptionHeight);

    HDC bufferedDc = nullptr;
    HPAINTBUFFER buffer = ::BeginBufferedPaint(hdc, &captionRect, BPBF_TOPDOWNDIB, nullptr, &bufferedDc);
    HDC targetDc = bufferedDc ? bufferedDc : hdc;
    {
        CDC dc;
        dc.Attach(targetDc);

        const WindowFrameMetrics metrics = calculateWindowFrameMetrics(isMaximized(), m_isFullScreen);
        dc.FillSolidRect(captionRect, Win10Theme::kCaptionBg);

        if (m_logoHovered) {
            CRect hoverRect = logoHoverRectFor(metrics);
            dc.FillSolidRect(hoverRect, Win10Theme::kCaptionButtonHover);
        }

        if (m_logoIcon) {
            const int logoY = (kCaptionHeight - kLogoSize) / 2;
            ::DrawIconEx(targetDc, kLogoLeftPadding, logoY, m_logoIcon, kLogoSize, kLogoSize, 0, nullptr, DI_NORMAL);
        }

        drawCaptionButton(dc, captionButtonRectFor(HTMINBUTTON), HTMINBUTTON);
        drawCaptionButton(dc, captionButtonRectFor(HTMAXBUTTON), HTMAXBUTTON);
        drawCaptionButton(dc, captionButtonRectFor(HTCLOSE), HTCLOSE);

        if (metrics.drawAccentBorder) {
            CPen borderPen(PS_SOLID, 1, Win10Theme::kBrandAccent);
            CPen *oldPen = dc.SelectObject(&borderPen);
            dc.MoveTo(0, 0);
            dc.LineTo(clientRect.right - 1, 0);
            dc.MoveTo(0, 0);
            dc.LineTo(0, kCaptionHeight);
            dc.MoveTo(clientRect.right - 1, 0);
            dc.LineTo(clientRect.right - 1, kCaptionHeight);
            dc.SelectObject(oldPen);
        }

        dc.Detach();
    }

    if (buffer) {
        ::BufferedPaintSetAlpha(buffer, &captionRect, 255);
        ::EndBufferedPaint(buffer, TRUE);
    }

    ::EndPaint(GetSafeHwnd(), &ps);
}

void MainWindow::layoutChildren()
{
    if (!GetSafeHwnd())
        return;

    CRect clientRect;
    GetClientRect(&clientRect);

    if (m_isFullScreen) {
        if (m_sessionHost.GetSafeHwnd())
            m_sessionHost.MoveWindow(0, 0, clientRect.Width(), clientRect.Height());
        if (m_sessionManager)
            m_sessionManager->layoutSessions();
        return;
    }

    const WindowFrameMetrics metrics = calculateWindowFrameMetrics(isMaximized(), false);
    const int inset = metrics.clientEdgeInset;
    const int tabLeft = kLogoLeftPadding + kLogoSize + kLogoRightPadding;
    const int tabRight = std::max(tabLeft, static_cast<int>(clientRect.right) - kSystemButtonReserve);

    if (m_tabBar.GetSafeHwnd()) {
        m_tabBar.SetWindowPos(nullptr, tabLeft, inset,
                              std::max(0, tabRight - tabLeft), kCaptionHeight - inset,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_sessionHost.GetSafeHwnd()) {
        m_sessionHost.SetWindowPos(nullptr, inset, kCaptionHeight,
                                   std::max(0, clientRect.Width() - 2 * inset),
                                   std::max(0, clientRect.Height() - kCaptionHeight - inset),
                                   SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_sessionManager)
        m_sessionManager->layoutSessions();

    if (m_sessionHost.GetSafeHwnd()) {
        m_sessionHost.RedrawWindow(nullptr, nullptr,
                                   RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
    }
}

void MainWindow::toggleFullScreen()
{
    setFullScreen(!m_isFullScreen);
}

void MainWindow::setFullScreen(bool enabled)
{
    if (enabled == m_isFullScreen || !GetSafeHwnd())
        return;

    if (enabled) {
        m_savedStyle = GetStyle();
        m_savedExStyle = GetExStyle();
        GetWindowRect(&m_savedRect);

        HMONITOR monitor = ::MonitorFromWindow(GetSafeHwnd(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = {};
        info.cbSize = sizeof(info);
        if (!::GetMonitorInfoW(monitor, &info))
            return;

        m_isFullScreen = true;
        ModifyStyle(WS_OVERLAPPEDWINDOW, WS_POPUP);
        ModifyStyleEx(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE, 0);
        if (m_tabBar.GetSafeHwnd())
            m_tabBar.ShowWindow(SW_HIDE);

        SetWindowPos(nullptr,
                     info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        m_isFullScreen = false;
        ModifyStyle(WS_POPUP, m_savedStyle & WS_OVERLAPPEDWINDOW);
        ModifyStyleEx(0, m_savedExStyle);
        if (m_tabBar.GetSafeHwnd())
            m_tabBar.ShowWindow(SW_SHOW);

        SetWindowPos(nullptr,
                     m_savedRect.left, m_savedRect.top,
                     m_savedRect.Width(), m_savedRect.Height(),
                     SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    layoutChildren();
    applyDwmExtension(GetSafeHwnd(), calculateWindowFrameMetrics(isMaximized(), m_isFullScreen));
}
