#include "MainWindow.h"

#include "common/Win32String.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/ConnectionListDialog.h"
#include "ui/ProfileEditDialog.h"
#include "ui/Win10Theme.h"
#include "ui/WindowFrameMetrics.h"
#include "resources/resource.h"

#include <cjson/cJSON.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <cstdio>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

IMPLEMENT_DYNAMIC(MainWindow, CFrameWnd)

BEGIN_MESSAGE_MAP(MainWindow, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_ERASEBKGND()
    ON_WM_NCACTIVATE()
    ON_WM_PAINT()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_CONTEXTMENU()
    ON_COMMAND(ID_MAIN_NEW, &MainWindow::OnMainNew)
    ON_COMMAND(ID_MAIN_CONNECTIONS, &MainWindow::OnOpenConnections)
    ON_MESSAGE(WM_NCCALCSIZE, &MainWindow::OnNcCalcSize)
    ON_MESSAGE(WM_NCLBUTTONDOWN, &MainWindow::OnNcLButtonDown)
    ON_MESSAGE(WM_NCHITTEST, &MainWindow::OnNcHitTest)
    ON_MESSAGE(WM_NCPAINT, &MainWindow::OnNcPaint)
    ON_MESSAGE(WM_EXITSIZEMOVE, &MainWindow::OnExitSizeMove)
    ON_MESSAGE(WM_APP_OPEN_CONNECTIONS, &MainWindow::OnOpenConnectionsMessage)
    ON_MESSAGE(WM_DWMCOMPOSITIONCHANGED, &MainWindow::OnDwmCompositionChanged)
END_MESSAGE_MAP()

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
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;

void applyDwmExtension(HWND hwnd, const WindowFrameMetrics &metrics)
{
    if (!hwnd)
        return;

    MARGINS margins = { 0, 0, metrics.dwmTopInset, 0 };
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);

    ::DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorNone, sizeof(kDwmColorNone));
}
}

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() = default;

bool MainWindow::createShell()
{
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                                  AfxGetApp()->LoadIcon(IDI_APP_ICON));

    if (!CreateEx(0, className, L"RdpBox",
                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                  CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                  nullptr, nullptr)) {
        return false;
    }

    SetWindowPos(nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    SetIcon(AfxGetApp()->LoadIcon(IDI_APP_ICON), TRUE);
    SetIcon(AfxGetApp()->LoadIcon(IDI_APP_ICON), FALSE);

    restoreWindowState();
    return true;
}

int MainWindow::OnCreate(LPCREATESTRUCT createStruct)
{
    if (CFrameWnd::OnCreate(createStruct) == -1)
        return -1;

    ModifyStyle(WS_BORDER | WS_DLGFRAME, 0, 0);
    applyDwmExtension(GetSafeHwnd(), calculateWindowFrameMetrics(false, m_isFullScreen));

    wchar_t pathBuffer[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, pathBuffer)))
        return -1;

    std::wstring dataDir = std::wstring(pathBuffer) + L"\\RdpBox";
    CreateDirectoryW(dataDir.c_str(), nullptr);
    m_profileRepository = std::make_unique<ProfileRepository>(dataDir + L"\\profiles.json");

    if (!m_tabBar.create(this, CRect(0, 0, 100, 100), 1)) {
        return -1;
    }

    m_logoIcon = AfxGetApp()->LoadIcon(IDI_APP_ICON);

    m_tabBar.setSelectionChangedCallback([this](int index) {
        if (m_sessionManager)
            m_sessionManager->activateTab(index);
    });

    m_tabBar.setCloseRequestedCallback([this](int index) {
        if (!m_sessionManager)
            return;
        const std::string sessionId = m_sessionManager->sessionIdByTabIndex(index);
        if (sessionId.empty())
            return;
        m_sessionManager->closeSession(sessionId);
        if (!m_isClosing && !hasOpenTabs())
            PostMessage(WM_APP_OPEN_CONNECTIONS, TRUE, 0);
    });

    if (!m_sessionHost.create(this, CRect(0, 0, 100, 100), 2)) {
        return -1;
    }

    applyUiFont();
    m_sessionManager = std::make_unique<SessionManager>(&m_tabBar, &m_sessionHost, m_profileRepository.get());
    m_sessionManager->setSessionConnectedCallback([this](const std::string &, const Profile &profile) {
        if (profile.fullScreenOnConnect && !m_isFullScreen)
            setFullScreen(true);
    });
    layoutChildren();
    return 0;
}

void MainWindow::OnSize(UINT type, int cx, int cy)
{
    CFrameWnd::OnSize(type, cx, cy);

    if (type == SIZE_MINIMIZED)
        return;

    layoutChildren();
    applyDwmExtension(GetSafeHwnd(), calculateWindowFrameMetrics(isMaximized(), m_isFullScreen));

    if (GetSafeHwnd()) {
        CRect captionRect(0, 0, cx, kCaptionHeight);
        InvalidateRect(captionRect, FALSE);
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

void MainWindow::OnClose()
{
    saveWindowState();
    m_isClosing = true;
    if (m_sessionManager)
        m_sessionManager->closeAllSessions();
    DestroyWindow();
}

void MainWindow::OnDestroy()
{
    if (m_sessionManager) {
        m_sessionManager->closeAllSessions();
        m_sessionManager.reset();
    }
    m_profileRepository.reset();
    CFrameWnd::OnDestroy();
    PostQuitMessage(0);
}

void MainWindow::OnOpenConnections()
{
    openConnectionDialog(false);
}

void MainWindow::OnMainNew()
{
    if (!m_profileRepository)
        return;
    ProfileEditDialog dialog(this);
    if (dialog.DoModal() == IDOK) {
        m_profileRepository->addProfile(dialog.profile());
        const Profile profile = m_profileRepository->profileById(dialog.profile().id);
        if (profile.isValid() && m_sessionManager)
            m_sessionManager->openSession(profile);
    }
}

void MainWindow::OnContextMenu(CWnd *window, CPoint point)
{
    if (!window || window->GetSafeHwnd() != m_tabBar.GetSafeHwnd()) {
        CFrameWnd::OnContextMenu(window, point);
        return;
    }

    const int index = m_tabBar.hitTestTabAtScreenPoint(point);
    if (index < 0 || !m_sessionManager)
        return;

    const auto sessionId = m_sessionManager->sessionIdByTabIndex(index);
    const bool hasTab = index >= 0;

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING | (hasTab ? MF_ENABLED : MF_GRAYED), ID_TAB_RECONNECT, L"Reconnect");
    menu.AppendMenu(MF_STRING | (hasTab ? MF_ENABLED : MF_GRAYED), ID_TAB_CLOSE, L"Close");

    const UINT command = ::TrackPopupMenuEx(menu.GetSafeHmenu(),
                                            TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                            point.x, point.y,
                                            GetSafeHwnd(),
                                            nullptr);
    if (!hasTab || sessionId.empty())
        return;

    if (command == ID_TAB_RECONNECT) {
        m_sessionManager->reconnectSession(sessionId);
    } else if (command == ID_TAB_CLOSE) {
        m_sessionManager->closeSession(sessionId);
        if (!m_isClosing && !hasOpenTabs())
            PostMessage(WM_APP_OPEN_CONNECTIONS, TRUE, 0);
    }
}

LRESULT MainWindow::OnNcCalcSize(WPARAM wParam, LPARAM lParam)
{
    if (!wParam)
        return 0;

    NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lParam);

    if (isMaximized()) {
        const int frameX = ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
        const int frameY = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
        params->rgrc[0].left += frameX;
        params->rgrc[0].right -= frameX;
        params->rgrc[0].bottom -= frameY;
    }

    return 0;
}

LRESULT MainWindow::OnNcLButtonDown(WPARAM hitTest, LPARAM lParam)
{
    if (hitTest >= HTLEFT && hitTest <= HTBOTTOMRIGHT) {
        if (m_sessionManager)
            m_sessionManager->setResizeSuppressed(true);
    }

    return Default();
}

LRESULT MainWindow::OnNcHitTest(WPARAM, LPARAM lParam)
{
    if (m_isFullScreen)
        return HTCLIENT;

    POINT cursorPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    POINT clientPoint = cursorPoint;
    ScreenToClient(&clientPoint);

    CRect clientRect;
    GetClientRect(&clientRect);

    if (!isMaximized()) {
        const int border = kResizeBorderTop;
        const bool nearLeft = clientPoint.x < border;
        const bool nearRight = clientPoint.x >= clientRect.right - border;
        const bool nearTop = clientPoint.y < border;
        const bool nearBottom = clientPoint.y >= clientRect.bottom - border;

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

    if (reinterpret_cast<HRGN>(wParam) != reinterpret_cast<HRGN>(1)) {
        ::FillRgn(hdc, reinterpret_cast<HRGN>(wParam), brush);
    } else {
        CRect winRect, clientRect;
        GetWindowRect(&winRect);
        GetClientRect(&clientRect);
        ::MapWindowPoints(HWND_DESKTOP, GetSafeHwnd(),
                          reinterpret_cast<LPPOINT>(&clientRect), 2);
        HRGN winRgn = ::CreateRectRgn(0, 0, winRect.Width(), winRect.Height());
        HRGN clientRgn = ::CreateRectRgnIndirect(&clientRect);
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
    if (hit) {
        UINT command = SC_CLOSE;
        if (hit == HTMINBUTTON)
            command = SC_MINIMIZE;
        else if (hit == HTMAXBUTTON)
            command = isMaximized() ? SC_RESTORE : SC_MAXIMIZE;
        else if (hit == HTCLOSE)
            command = SC_CLOSE;
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

    const bool logoHit = logoHitTest(point);
    if (logoHit != m_logoHovered) {
        m_logoHovered = logoHit;
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
    if (m_hoverCaptionButton) {
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

std::wstring MainWindow::windowStateFilePath() const
{
    wchar_t pathBuffer[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr,
                                SHGFP_TYPE_CURRENT, pathBuffer)))
        return {};

    std::wstring dir = std::wstring(pathBuffer) + L"\\RdpBox";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\window-state.json";
}

void MainWindow::saveWindowState() const
{
    if (!GetSafeHwnd() || m_isFullScreen)
        return;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!const_cast<MainWindow *>(this)->GetWindowPlacement(&placement))
        return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "left", placement.rcNormalPosition.left);
    cJSON_AddNumberToObject(root, "top", placement.rcNormalPosition.top);
    cJSON_AddNumberToObject(root, "right", placement.rcNormalPosition.right);
    cJSON_AddNumberToObject(root, "bottom", placement.rcNormalPosition.bottom);
    cJSON_AddNumberToObject(root, "showCmd", static_cast<int>(placement.showCmd));

    char *json = cJSON_Print(root);
    if (json) {
        const std::wstring path = windowStateFilePath();
        if (!path.empty()) {
            std::FILE *file = nullptr;
            if (_wfopen_s(&file, path.c_str(), L"wb") == 0 && file) {
                std::fwrite(json, 1, std::strlen(json), file);
                std::fclose(file);
            }
        }
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

bool MainWindow::restoreWindowState()
{
    const std::wstring path = windowStateFilePath();
    if (path.empty())
        return false;

    std::FILE *file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        return false;

    std::string contents;
    char buffer[4096];
    while (const size_t read = std::fread(buffer, 1, sizeof(buffer), file))
        contents.append(buffer, read);
    std::fclose(file);

    if (contents.empty())
        return false;

    cJSON *root = cJSON_Parse(contents.c_str());
    if (!root)
        return false;

    const cJSON *left = cJSON_GetObjectItemCaseSensitive(root, "left");
    const cJSON *top = cJSON_GetObjectItemCaseSensitive(root, "top");
    const cJSON *right = cJSON_GetObjectItemCaseSensitive(root, "right");
    const cJSON *bottom = cJSON_GetObjectItemCaseSensitive(root, "bottom");
    const cJSON *showCmd = cJSON_GetObjectItemCaseSensitive(root, "showCmd");

    bool ok = false;
    if (cJSON_IsNumber(left) && cJSON_IsNumber(top) &&
        cJSON_IsNumber(right) && cJSON_IsNumber(bottom)) {
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        placement.rcNormalPosition.left = left->valueint;
        placement.rcNormalPosition.top = top->valueint;
        placement.rcNormalPosition.right = right->valueint;
        placement.rcNormalPosition.bottom = bottom->valueint;
        placement.showCmd = cJSON_IsNumber(showCmd) ? showCmd->valueint : SW_SHOWNORMAL;

        const int width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
        const int height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
        if (width >= 400 && height >= 300) {
            HMONITOR monitor = ::MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONULL);
            if (monitor) {
                SetWindowPlacement(&placement);
                ok = true;
            }
        }
    }

    cJSON_Delete(root);
    return ok;
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
        if (isMaximized()) {
            CRect inner1(cx - kGlyph + 1, cy - kGlyph + 1, cx + kGlyph - 1, cy + kGlyph - 1);
            CRect inner2(cx - kGlyph + 3, cy - kGlyph - 1, cx + kGlyph + 1, cy + kGlyph - 3);
            CBrush hollow;
            hollow.CreateStockObject(NULL_BRUSH);
            CBrush *oldBrush = dc.SelectObject(&hollow);
            dc.Rectangle(inner1);
            dc.Rectangle(inner2);
            dc.SelectObject(oldBrush);
        } else {
            CRect inner(cx - kGlyph, cy - kGlyph, cx + kGlyph + 1, cy + kGlyph + 1);
            CBrush hollow;
            hollow.CreateStockObject(NULL_BRUSH);
            CBrush *oldBrush = dc.SelectObject(&hollow);
            dc.Rectangle(inner);
            dc.SelectObject(oldBrush);
        }
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
    if (m_isFullScreen)
        return false;
    return logoRect().PtInRect(clientPoint);
}

void MainWindow::showLogoMenu()
{
    CRect rect = logoRect();
    ClientToScreen(&rect);

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_MAIN_NEW, L"New\tCtrl+N");
    menu.AppendMenu(MF_STRING, ID_MAIN_CONNECTIONS, L"Connections\tCtrl+O");

    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN,
                        rect.left, rect.bottom, this);
}

void MainWindow::OnPaint()
{
    PAINTSTRUCT ps = {};
    HDC hdc = ::BeginPaint(GetSafeHwnd(), &ps);

    CRect clientRect;
    GetClientRect(&clientRect);

    CRect captionRect(0, 0, clientRect.right, kCaptionHeight);

    HDC bufferedDc = nullptr;
    HPAINTBUFFER buffer = ::BeginBufferedPaint(hdc, &captionRect, BPBF_TOPDOWNDIB,
                                               nullptr, &bufferedDc);

    HDC targetDc = bufferedDc ? bufferedDc : hdc;
    {
        CDC dc;
        dc.Attach(targetDc);
        const WindowFrameMetrics metrics = calculateWindowFrameMetrics(isMaximized(), m_isFullScreen);

        dc.FillSolidRect(captionRect, Win10Theme::kCaptionBg);

        if (m_logoHovered) {
            CRect hoverRect = logoRect();
            hoverRect.DeflateRect(2, 3);
            dc.FillSolidRect(hoverRect, Win10Theme::kCaptionButtonHover);
        }

        if (m_logoIcon) {
            const int logoY = (kCaptionHeight - kLogoSize) / 2;
            ::DrawIconEx(targetDc, kLogoLeftPadding, logoY, m_logoIcon,
                         kLogoSize, kLogoSize, 0, nullptr, DI_NORMAL);
        }

        drawCaptionButton(dc, captionButtonRectFor(HTMINBUTTON), HTMINBUTTON);
        drawCaptionButton(dc, captionButtonRectFor(HTMAXBUTTON), HTMAXBUTTON);
        drawCaptionButton(dc, captionButtonRectFor(HTCLOSE), HTCLOSE);

        if (metrics.drawAccentBorder) {
            CPen borderPen(PS_SOLID, 1, Win10Theme::kAccent);
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

LRESULT MainWindow::OnExitSizeMove(WPARAM, LPARAM)
{
    if (m_sessionManager) {
        m_sessionManager->setResizeSuppressed(false);
        m_sessionManager->flushPendingResize();
    }

    return 0;
}

LRESULT MainWindow::OnOpenConnectionsMessage(WPARAM selectionRequired, LPARAM)
{
    openConnectionDialog(selectionRequired != FALSE);
    return 0;
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
    const int b = metrics.clientEdgeInset;

    const int tabLeft = kLogoLeftPadding + kLogoSize + kLogoRightPadding;
    const int tabRight = std::max(tabLeft, static_cast<int>(clientRect.right) - kSystemButtonReserve);
    if (m_tabBar.GetSafeHwnd())
        m_tabBar.MoveWindow(tabLeft, b, std::max(0, tabRight - tabLeft), kCaptionHeight - b);

    if (m_sessionHost.GetSafeHwnd())
        m_sessionHost.MoveWindow(b, kCaptionHeight,
                                 std::max(0, clientRect.Width() - 2 * b),
                                 std::max(0, clientRect.Height() - kCaptionHeight - b));

    if (m_sessionManager)
        m_sessionManager->layoutSessions();
}

BOOL MainWindow::PreTranslateMessage(MSG *msg)
{
    if (msg && msg->message == WM_KEYDOWN) {
        if (msg->wParam == VK_F11) {
            toggleFullScreen();
            return TRUE;
        }
        if (msg->wParam == VK_ESCAPE && m_isFullScreen) {
            setFullScreen(false);
            return TRUE;
        }
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            if (msg->wParam == 'N') {
                SendMessage(WM_COMMAND, ID_MAIN_NEW, 0);
                return TRUE;
            }
            if (msg->wParam == 'O') {
                openConnectionDialog(false);
                return TRUE;
            }
        }
    }

    return CFrameWnd::PreTranslateMessage(msg);
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

void MainWindow::openConnectionDialog(bool selectionRequired)
{
    if (!m_profileRepository)
        return;

    ConnectionListDialog dialog(m_profileRepository.get(), this);
    dialog.setSelectionRequired(selectionRequired || !hasOpenTabs());

    if (dialog.DoModal() == IDOK) {
        const std::vector<std::string> profileIds = dialog.selectedProfileIds();
        for (const std::string &profileId : profileIds) {
            const Profile profile = m_profileRepository->profileById(profileId);
            if (profile.isValid() && m_sessionManager)
                m_sessionManager->openSession(profile);
        }
    } else if (!m_isClosing && !hasOpenTabs()) {
        PostMessage(WM_APP_OPEN_CONNECTIONS, TRUE, 0);
    }
}

bool MainWindow::hasOpenTabs() const
{
    return m_sessionManager && m_sessionManager->hasOpenSessions();
}

int MainWindow::tabIndexAtScreenPoint(CPoint point) const
{
    return const_cast<MainWindow *>(this)->m_tabBar.hitTestTabAtScreenPoint(point);
}

void MainWindow::applyUiFont()
{
    if (m_uiFont.GetSafeHandle())
        m_uiFont.DeleteObject();

    HFONT raw = Win10Theme::createUiFont(9);
    if (!raw)
        return;

    m_uiFont.Attach(raw);

    if (m_tabBar.GetSafeHwnd())
        m_tabBar.SetFont(&m_uiFont);
}
