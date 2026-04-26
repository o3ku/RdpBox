#include "MainWindow.h"

#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/ConnectionListDialog.h"
#include "ui/ProfileEditDialog.h"
#include "resources/resource.h"

#include <shlobj.h>

#include "common/Win32String.h"

IMPLEMENT_DYNAMIC(MainWindow, CFrameWnd)

BEGIN_MESSAGE_MAP(MainWindow, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_BN_CLICKED(ID_MAIN_NEW, &MainWindow::OnNewConnection)
    ON_BN_CLICKED(ID_MAIN_CONNECTIONS, &MainWindow::OnOpenConnections)
    ON_NOTIFY(TCN_SELCHANGE, 1, &MainWindow::OnTabSelectionChanged)
    ON_WM_CONTEXTMENU()
    ON_MESSAGE(WM_APP_OPEN_CONNECTIONS, &MainWindow::OnOpenConnectionsMessage)
END_MESSAGE_MAP()

namespace
{
constexpr int kTopStripHeight = 34;
constexpr int kButtonWidth = 100;
constexpr int kButtonHeight = 26;
constexpr int kMargin = 4;
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
                  WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
                  CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                  nullptr, nullptr)) {
        return false;
    }

    SetIcon(AfxGetApp()->LoadIcon(IDI_APP_ICON), TRUE);
    SetIcon(AfxGetApp()->LoadIcon(IDI_APP_ICON), FALSE);
    return true;
}

int MainWindow::OnCreate(LPCREATESTRUCT createStruct)
{
    if (CFrameWnd::OnCreate(createStruct) == -1)
        return -1;

    wchar_t pathBuffer[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, pathBuffer)))
        return -1;

    std::wstring dataDir = std::wstring(pathBuffer) + L"\\RdpBox";
    CreateDirectoryW(dataDir.c_str(), nullptr);
    m_profileRepository = std::make_unique<ProfileRepository>(dataDir + L"\\profiles.json");

    CRect clientRect;
    GetClientRect(&clientRect);

    if (!m_tabCtrl.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_SINGLELINE,
                          CRect(0, 0, 100, 100), this, 1)) {
        return -1;
    }

    if (!m_newButton.Create(L"New", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                            CRect(0, 0, 100, 24), this, ID_MAIN_NEW)) {
        return -1;
    }

    if (!m_connectionsButton.Create(L"Connections", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    CRect(0, 0, 100, 24), this, ID_MAIN_CONNECTIONS)) {
        return -1;
    }

    if (!m_sessionHost.Create(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW),
                                                  reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr),
                              L"SessionHost",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              CRect(0, 0, 100, 100), this, 2)) {
        return -1;
    }

    m_sessionManager = std::make_unique<SessionManager>(&m_tabCtrl, &m_sessionHost);
    layoutChildren();
    return 0;
}

void MainWindow::OnSize(UINT type, int cx, int cy)
{
    CFrameWnd::OnSize(type, cx, cy);
    layoutChildren();
}

void MainWindow::OnClose()
{
    m_isClosing = true;
    if (m_sessionManager)
        m_sessionManager->closeAllSessions();
    CFrameWnd::OnClose();
}

void MainWindow::OnDestroy()
{
    if (m_sessionManager)
        m_sessionManager->closeAllSessions();
    CFrameWnd::OnDestroy();
}

void MainWindow::OnNewConnection()
{
    if (!m_profileRepository)
        return;

    ProfileEditDialog dialog(this);
    if (dialog.DoModal() != IDOK)
        return;

    Profile profile = dialog.profile();
    if (profile.id.empty())
        profile.id = createGuidString();

    m_profileRepository->addProfile(profile);
    if (m_sessionManager)
        m_sessionManager->openSession(profile);
}

void MainWindow::OnOpenConnections()
{
    openConnectionDialog(false);
}

void MainWindow::OnTabSelectionChanged(NMHDR *notify, LRESULT *result)
{
    UNREFERENCED_PARAMETER(notify);

    if (m_sessionManager)
        m_sessionManager->activateTab(m_tabCtrl.GetCurSel());

    if (result)
        *result = 0;
}

void MainWindow::OnContextMenu(CWnd *window, CPoint point)
{
    if (!window || window->GetSafeHwnd() != m_tabCtrl.GetSafeHwnd()) {
        CFrameWnd::OnContextMenu(window, point);
        return;
    }

    const int index = tabIndexAtScreenPoint(point);
    if (index < 0 || !m_sessionManager)
        return;

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_TAB_RECONNECT, L"Reconnect");
    menu.AppendMenu(MF_STRING, ID_TAB_CLOSE, L"Close");

    const UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             point.x, point.y, this);
    const auto sessionId = m_sessionManager->sessionIdByTabIndex(index);
    if (sessionId.isEmpty())
        return;

    if (command == ID_TAB_RECONNECT) {
        m_sessionManager->reconnectSession(sessionId);
    } else if (command == ID_TAB_CLOSE) {
        m_sessionManager->closeSession(sessionId);
        if (!m_isClosing && !hasOpenTabs())
            PostMessage(WM_APP_OPEN_CONNECTIONS, TRUE, 0);
    }
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

    const int buttonTop = kMargin;
    const int buttonLeft2 = clientRect.right - kMargin - kButtonWidth;
    const int buttonLeft1 = buttonLeft2 - kMargin - kButtonWidth;

    if (m_newButton.GetSafeHwnd())
        m_newButton.MoveWindow(buttonLeft1, buttonTop, kButtonWidth, kButtonHeight);
    if (m_connectionsButton.GetSafeHwnd())
        m_connectionsButton.MoveWindow(buttonLeft2, buttonTop, kButtonWidth, kButtonHeight);

    if (m_tabCtrl.GetSafeHwnd())
        m_tabCtrl.MoveWindow(kMargin, 0, std::max(0, buttonLeft1 - (kMargin * 2)), kTopStripHeight);

    if (m_sessionHost.GetSafeHwnd())
        m_sessionHost.MoveWindow(0, kTopStripHeight, clientRect.Width(), std::max(0, clientRect.Height() - kTopStripHeight));

    if (m_sessionManager)
        m_sessionManager->layoutSessions();
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
    if (!m_tabCtrl.GetSafeHwnd())
        return -1;

    CPoint clientPoint(point);
    m_tabCtrl.ScreenToClient(&clientPoint);

    TCHITTESTINFO hitTest = {};
    hitTest.pt = clientPoint;
    return m_tabCtrl.HitTest(&hitTest);
}

