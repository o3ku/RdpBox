#include "MainWindow.h"

#include "common/AppPaths.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/ConnectionListDialog.h"
#include "ui/MainWindowSessionBehavior.h"
#include "ui/MainWindowTabBehavior.h"
#include "ui/ProfileEditDialog.h"
#include "resources/resource.h"

namespace
{
void openProfileSession(SessionManager *sessionManager, const Profile &profile)
{
    if (sessionManager && shouldOpenProfileSession(profile))
        sessionManager->openSession(profile);
}

ui::MainWindowConnectionInfo mainWindowConnectionInfo(const FreeRdpProcess::ConnectionInfo &info)
{
    return ui::MainWindowConnectionInfo{info.codecName, info.rtt, info.rttAvailable};
}

BrowserTabBar::TabStatus tabStatusFromMainWindowStatus(ui::MainWindowTabStatus status)
{
    switch (status) {
    case ui::MainWindowTabStatus::ConnectedGood:
        return BrowserTabBar::TabStatus::ConnectedGood;
    case ui::MainWindowTabStatus::ConnectedWarn:
        return BrowserTabBar::TabStatus::ConnectedWarn;
    case ui::MainWindowTabStatus::ConnectedBad:
        return BrowserTabBar::TabStatus::ConnectedBad;
    case ui::MainWindowTabStatus::Inactive:
    default:
        return BrowserTabBar::TabStatus::Inactive;
    }
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

        const ui::MainWindowTabClosePlan plan =
            ui::tabClosePlanForSessionId(m_sessionManager->sessionIdByTabIndex(index));
        if (plan.closeSession)
            m_sessionManager->closeSession(plan.sessionId);
        if (plan.refreshTabStatuses)
            refreshTabStatuses();
    });

    m_tabBar.setTabReorderedCallback([this](int fromIndex, int toIndex) {
        if (!m_sessionManager)
            return;
        m_sessionManager->moveSession(fromIndex, toIndex);
    });

    m_tabBar.setTooltipCallback([this](int tabIndex) -> std::wstring {
        if (!m_sessionManager)
            return {};

        const auto info = m_sessionManager->connectionInfoForTab(tabIndex);
        if (info.codecName.empty())
            return {};

        return ui::tabTooltipText(mainWindowConnectionInfo(info));
    });

    if (!m_sessionHost.create(this, CRect(0, 0, 100, 100), 2))
        return -1;

    applyUiFont();
    m_sessionManager = std::make_unique<SessionManager>(&m_tabBar, &m_sessionHost, m_profileRepository.get());
    m_sessionManager->setSessionConnectedCallback([this](const std::string &, const Profile &profile) {
        const ui::MainWindowConnectionCompletedPlan plan =
            ui::connectionCompletedPlan(profile, m_isFullScreen);
        if (plan.enterFullScreen)
            setFullScreen(true);
        if (plan.refreshTabStatuses)
            refreshTabStatuses();
    });

    layoutChildren();
    refreshDwmFrame();
    SetTimer(kTabStatusTimerId, 2000, nullptr);
    if (!m_captionTooltip.GetSafeHwnd()) {
        m_captionTooltip.Create(this, TTS_NOPREFIX | TTS_ALWAYSTIP);
        m_captionTooltip.SetMaxTipWidth(500);
        m_captionTooltip.AddTool(this, L"");
        m_captionTooltip.Activate(TRUE);
    }
    SetTimer(kUpdateCheckTimerId, 24 * 60 * 60 * 1000u, nullptr);
    startBackgroundUpdateCheck();
    return 0;
}

void MainWindow::OnDestroy()
{
    KillTimer(kUpdateCheckTimerId);
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

    if (!m_profileRepository->addProfile(dialog.profile())) {
        CString message;
        message.Format(L"Connection name \"%s\" already exists.", dialog.profile().name.c_str());
        MessageBox(message, L"Duplicate Connection Name", MB_OK | MB_ICONWARNING);
        return;
    }

    openProfileSession(m_sessionManager.get(), m_profileRepository->profileByName(dialog.profile().name));
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
    const ui::TabContextMenuState menuState =
        ui::tabContextMenuState(index, m_tabBar.selectedIndex(), hasTab, m_isFullScreen);
    menu.AppendMenu(MF_STRING | (menuState.fullScreenEnabled ? MF_ENABLED : MF_GRAYED),
                    ID_TAB_FULLSCREEN,
                    menuState.fullScreenText.c_str());
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (menuState.reconnectEnabled ? MF_ENABLED : MF_GRAYED),
                    ID_TAB_RECONNECT,
                    L"Reconnect");
    menu.AppendMenu(MF_STRING | (menuState.closeEnabled ? MF_ENABLED : MF_GRAYED),
                    ID_TAB_CLOSE,
                    L"Close");

    const UINT command = ::TrackPopupMenuEx(menu.GetSafeHmenu(),
                                            TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                            point.x, point.y,
                                            GetSafeHwnd(),
                                            nullptr);
    switch (ui::tabContextCommandForMenuId(command, hasTab)) {
    case ui::TabContextCommand::ToggleFullScreen:
        toggleFullScreen();
        break;
    case ui::TabContextCommand::Reconnect: {
        const ui::MainWindowTabReconnectPlan plan = ui::tabReconnectPlanForSessionId(sessionId);
        if (plan.reconnectSession)
            m_sessionManager->reconnectSession(plan.sessionId);
        break;
    }
    case ui::TabContextCommand::Close: {
        const ui::MainWindowTabClosePlan plan = ui::tabClosePlanForSessionId(sessionId);
        if (plan.closeSession)
            m_sessionManager->closeSession(plan.sessionId);
        if (plan.refreshTabStatuses)
            refreshTabStatuses();
        break;
    }
    case ui::TabContextCommand::None:
    default:
        break;
    }
}

LRESULT MainWindow::OnExitSizeMove(WPARAM, LPARAM)
{
    const ui::MainWindowExitSizeMovePlan plan =
        ui::exitSizeMovePlan(m_inMoveOrSizeLoop, static_cast<bool>(m_sessionManager));
    m_inMoveOrSizeLoop = false;

    if (m_sessionManager && plan.clearResizeSuppression)
        m_sessionManager->setResizeSuppressed(false);
    if (m_sessionManager && plan.flushPendingResize)
        m_sessionManager->flushPendingResize();

    if (plan.persistWindowState)
        saveWindowState();

    return 0;
}

LRESULT MainWindow::OnOpenConnectionsMessage(WPARAM, LPARAM)
{
    openConnectionDialog();
    return 0;
}

LRESULT MainWindow::OnOpenStartupConnectionsMessage(WPARAM, LPARAM)
{
    openConnectionsByName(m_startupConnectionNames);
    return 0;
}

LRESULT MainWindow::OnPowerBroadcast(WPARAM wParam, LPARAM)
{
    const ui::MainWindowPowerBroadcastPlan plan =
        ui::powerBroadcastPlan(static_cast<unsigned int>(wParam), static_cast<bool>(m_sessionManager));
    if (plan.handleHostResume)
        m_sessionManager->handleHostResume();

    return TRUE;
}

void MainWindow::openConnectionDialog()
{
    if (!m_profileRepository)
        return;

    const auto connectedIds = m_sessionManager
        ? m_sessionManager->connectedProfileNames()
        : std::vector<std::wstring>{};

    ConnectionListDialog dialog(m_profileRepository.get(), connectedIds, this);
    if (dialog.DoModal() != IDOK)
        return;

    const std::vector<std::wstring> profileNames = dialog.selectedProfileNames();
    openConnectionsByName(profileNames);
}

void MainWindow::openConnectionsByName(const std::vector<std::wstring> &connectionNames)
{
    if (!m_profileRepository)
        return;

    const ui::MainWindowOpenPlan plan =
        ui::openPlanForConnectionNames(connectionNames, m_profileRepository->profiles());
    for (const Profile &profile : plan.profilesToOpen)
        openProfileSession(m_sessionManager.get(), profile);
}

void MainWindow::refreshTabStatuses()
{
    if (!m_sessionManager)
        return;

    const int count = m_tabBar.tabCount();
    for (int i = 0; i < count; ++i) {
        BrowserTabBar::TabStatusInfo status;
        const auto info = m_sessionManager->connectionInfoForTab(i);
        status.status = tabStatusFromMainWindowStatus(
            ui::tabStatusForConnection(m_sessionManager->isTabConnected(i),
                                       mainWindowConnectionInfo(info)));

        m_tabBar.setTabStatus(i, status);
    }
}

void MainWindow::OnTimer(UINT_PTR timerId)
{
    if (timerId == kTabStatusTimerId) {
        refreshTabStatuses();
        return;
    }
    if (timerId == kUpdateCheckTimerId) {
        startBackgroundUpdateCheck();
        return;
    }

    CFrameWnd::OnTimer(timerId);
}
