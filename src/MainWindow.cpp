#include "MainWindow.h"

#include "common/Win32String.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/AboutDialog.h"
#include "ui/MainWindowActivation.h"
#include "ui/Win10Theme.h"
#include "resources/resource.h"

#include <cstdio>

IMPLEMENT_DYNAMIC(MainWindow, CFrameWnd)

BEGIN_MESSAGE_MAP(MainWindow, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_WM_ACTIVATE()
    ON_WM_ERASEBKGND()
    ON_WM_NCACTIVATE()
    ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_CONTEXTMENU()
    ON_COMMAND(ID_MAIN_NEW, &MainWindow::OnMainNew)
    ON_COMMAND(ID_MAIN_CONNECTIONS, &MainWindow::OnOpenConnections)
    ON_COMMAND(ID_MAIN_ABOUT, &MainWindow::OnMainAbout)
    ON_MESSAGE(WM_NCCALCSIZE, &MainWindow::OnNcCalcSize)
    ON_MESSAGE(WM_NCLBUTTONDOWN, &MainWindow::OnNcLButtonDown)
    ON_MESSAGE(WM_NCHITTEST, &MainWindow::OnNcHitTest)
    ON_MESSAGE(WM_NCPAINT, &MainWindow::OnNcPaint)
    ON_MESSAGE(WM_EXITSIZEMOVE, &MainWindow::OnExitSizeMove)
    ON_MESSAGE(WM_APP_OPEN_CONNECTIONS, &MainWindow::OnOpenConnectionsMessage)
    ON_MESSAGE(WM_DWMCOMPOSITIONCHANGED, &MainWindow::OnDwmCompositionChanged)
END_MESSAGE_MAP()

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() = default;

bool MainWindow::createShell()
{
    static HBRUSH s_shellBrush = ::CreateSolidBrush(Win10Theme::kCaptionBg);
    const CString className = AfxRegisterWndClass(CS_DBLCLKS,
                                                  ::LoadCursor(nullptr, IDC_ARROW),
                                                  s_shellBrush,
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

void MainWindow::OnSize(UINT type, int cx, int cy)
{
    CFrameWnd::OnSize(type, cx, cy);

    if (type == SIZE_MINIMIZED)
        return;

    layoutChildren();
    invalidateCaptionButtons();
}

void MainWindow::OnClose()
{
    KillTimer(kTabStatusTimerId);
    saveWindowState();
    DestroyWindow();
}

void MainWindow::OnActivate(UINT state, CWnd *otherWnd, BOOL minimized)
{
    CFrameWnd::OnActivate(state, otherWnd, minimized);

    if (!m_sessionManager)
        return;

    if (ui::shouldFocusActiveSessionOnActivate(state, minimized != FALSE))
        m_sessionManager->focusActiveSession();
}

void MainWindow::OnMainAbout()
{
    AboutDialog dialog(this);
    dialog.DoModal();
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
                openConnectionDialog();
                return TRUE;
            }
        }
    }

    return CFrameWnd::PreTranslateMessage(msg);
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
