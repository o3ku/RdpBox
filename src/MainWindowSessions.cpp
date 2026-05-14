#include "MainWindow.h"

#include "common/AppPaths.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/ConnectionListDialog.h"
#include "ui/ProfileEditDialog.h"
#include "resources/resource.h"

namespace
{
void openProfileSession(SessionManager *sessionManager, const Profile &profile)
{
    if (sessionManager && profile.isValid())
        sessionManager->openSession(profile);
}
}

int MainWindow::OnCreate(LPCREATESTRUCT createStruct)
{
    if (CFrameWnd::OnCreate(createStruct) == -1)
        return -1;

    const std::wstring profilesPath = AppPaths::profilesFilePath();
    if (profilesPath.empty())
        return -1;

    m_profileRepository = std::make_unique<ProfileRepository>(profilesPath);

    if (!m_tabBar.create(this, CRect(0, 0, 100, 100), 1))
        return -1;

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
        refreshTabStatuses();
    });

    m_tabBar.setTooltipCallback([this](int tabIndex) -> std::wstring {
        if (!m_sessionManager)
            return {};

        const auto info = m_sessionManager->connectionInfoForTab(tabIndex);
        if (info.codecName.empty())
            return {};

        CString text;
        if (info.rttAvailable)
            text.Format(L"Codec: %S, Delay: %u ms", info.codecName.c_str(), info.rtt);
        else
            text.Format(L"Codec: %S", info.codecName.c_str());
        return text.GetString();
    });

    if (!m_sessionHost.create(this, CRect(0, 0, 100, 100), 2))
        return -1;

    applyUiFont();
    m_sessionManager = std::make_unique<SessionManager>(&m_tabBar, &m_sessionHost, m_profileRepository.get());
    m_sessionManager->setSessionConnectedCallback([this](const std::string &, const Profile &profile) {
        if (profile.fullScreenOnConnect && !m_isFullScreen)
            setFullScreen(true);
        refreshTabStatuses();
    });

    layoutChildren();
    refreshDwmFrame();
    SetTimer(kTabStatusTimerId, 2000, nullptr);
    return 0;
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
    openConnectionDialog();
}

void MainWindow::OnMainNew()
{
    if (!m_profileRepository)
        return;

    ProfileEditDialog dialog(this);
    if (dialog.DoModal() != IDOK)
        return;

    m_profileRepository->addProfile(dialog.profile());
    openProfileSession(m_sessionManager.get(), m_profileRepository->profileById(dialog.profile().id));
}

void MainWindow::OnContextMenu(CWnd *window, CPoint point)
{
    if (!window || window->GetSafeHwnd() != m_tabBar.GetSafeHwnd()) {
        CFrameWnd::OnContextMenu(window, point);
        return;
    }

    if (!m_sessionManager)
        return;

    const int index = m_tabBar.hitTestTabAtScreenPoint(point);
    if (index < 0)
        return;

    const std::string sessionId = m_sessionManager->sessionIdByTabIndex(index);
    const bool hasTab = !sessionId.empty();

    CMenu menu;
    menu.CreatePopupMenu();
    const bool isActive = index == m_tabBar.selectedIndex();
    CString fullScreenText = m_isFullScreen ? L"Exit Full Screen" : L"Full Screen";
    menu.AppendMenu(MF_STRING | (isActive ? MF_ENABLED : MF_GRAYED), ID_TAB_FULLSCREEN, fullScreenText);
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (hasTab ? MF_ENABLED : MF_GRAYED), ID_TAB_RECONNECT, L"Reconnect");
    menu.AppendMenu(MF_STRING | (hasTab ? MF_ENABLED : MF_GRAYED), ID_TAB_CLOSE, L"Close");

    const UINT command = ::TrackPopupMenuEx(menu.GetSafeHmenu(),
                                            TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                            point.x, point.y,
                                            GetSafeHwnd(),
                                            nullptr);
    if (!hasTab)
        return;

    if (command == ID_TAB_FULLSCREEN) {
        toggleFullScreen();
    } else if (command == ID_TAB_RECONNECT) {
        m_sessionManager->reconnectSession(sessionId);
    } else if (command == ID_TAB_CLOSE) {
        m_sessionManager->closeSession(sessionId);
    }
}

LRESULT MainWindow::OnExitSizeMove(WPARAM, LPARAM)
{
    const bool shouldPersist = ui::shouldPersistWindowStateOnExitSizeMove(m_inMoveOrSizeLoop);
    m_inMoveOrSizeLoop = false;

    if (m_sessionManager) {
        m_sessionManager->setResizeSuppressed(false);
        m_sessionManager->flushPendingResize();
    }

    if (shouldPersist)
        saveWindowState();

    return 0;
}

LRESULT MainWindow::OnOpenConnectionsMessage(WPARAM, LPARAM)
{
    openConnectionDialog();
    return 0;
}

LRESULT MainWindow::OnPowerBroadcast(WPARAM wParam, LPARAM)
{
    if ((wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) && m_sessionManager)
        m_sessionManager->handleHostResume();

    return TRUE;
}

void MainWindow::openConnectionDialog()
{
    if (!m_profileRepository)
        return;

    const auto connectedIds = m_sessionManager
        ? m_sessionManager->connectedProfileIds()
        : std::vector<std::string>{};

    ConnectionListDialog dialog(m_profileRepository.get(), connectedIds, this);
    if (dialog.DoModal() != IDOK)
        return;

    const std::vector<std::string> profileIds = dialog.selectedProfileIds();
    for (const std::string &profileId : profileIds)
        openProfileSession(m_sessionManager.get(), m_profileRepository->profileById(profileId));
}

void MainWindow::refreshTabStatuses()
{
    if (!m_sessionManager)
        return;

    const int count = m_tabBar.tabCount();
    for (int i = 0; i < count; ++i) {
        BrowserTabBar::TabStatusInfo status;
        if (m_sessionManager->isTabConnected(i)) {
            const auto info = m_sessionManager->connectionInfoForTab(i);
            if (info.rttAvailable) {
                if (info.rtt <= 100)
                    status.status = BrowserTabBar::TabStatus::ConnectedGood;
                else if (info.rtt <= 300)
                    status.status = BrowserTabBar::TabStatus::ConnectedWarn;
                else
                    status.status = BrowserTabBar::TabStatus::ConnectedBad;
            } else {
                status.status = BrowserTabBar::TabStatus::ConnectedGood;
            }
        }

        m_tabBar.setTabStatus(i, status);
    }
}

void MainWindow::OnTimer(UINT_PTR timerId)
{
    if (timerId == kTabStatusTimerId) {
        refreshTabStatuses();
        return;
    }

    CFrameWnd::OnTimer(timerId);
}
