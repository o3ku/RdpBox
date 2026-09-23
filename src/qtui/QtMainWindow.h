#pragma once

#include "common/profiles/Profile.h"
#include "common/profiles/ProfileRepository.h"
#include "common/UpdateClient.h"
#include <map>

#include "common/rdp/FreeRdpProcess.h"
#include "common/ui/MainWindowUpdateBehavior.h"

#include <QMainWindow>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPoint;
class QPushButton;
class QShortcut;
class QToolButton;
class QTabBar;
class QTabWidget;
class QTimer;
class QSplitter;
class QVBoxLayout;
class QWidget;
class QtRdpSessionWidget;

class QtMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                          QWidget *parent = nullptr);
    ~QtMainWindow() override;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildTitleBar(QVBoxLayout *rootLayout);
    void installShortcuts();
    void applyShortcutSettings();
    void refreshProfileList(bool allowSelectionFallback = true);
    void refreshActions();
    void refreshUpdateButton();
    void refreshWindowControls();
    void saveWindowState() const;
    bool restoreWindowState();
    int nativeHitTestForPoint(const QPoint &windowPoint) const;
    void addProfile(bool connectAfterAdd = false);
    void editSelectedProfile();
    void duplicateSelectedProfile();
    void deleteSelectedProfile();
    bool moveSelectedProfileBy(int delta);
    void moveProfileByDrop(int sourceRow, int insertIndex);
    void closeSessionTab(int index);
    void touchLastConnectedAt(const Profile &profile);
    void toggleFullScreen();
    void setFullScreen(bool enabled);
    void updateTabBarOffset();
    void showSettingsDialog();
    void retranslateUi();
    void rethemeCaptionIcons();
    void handleUpdateButtonClicked();
    ui::UpdateUiState updateUiState() const;
    void startBackgroundUpdateCheck(bool userInitiated = false);
    void startBackgroundUpdateDownload();
    void handleUpdateCheckCompleted(std::uint64_t generation,
                                    bool userInitiated,
                                    bool success,
                                    const std::wstring &errorMessage,
                                    const updater::ReleaseAsset &release,
                                    bool hasUpdate);
    void handleUpdateDownloadProgress(std::uint64_t generation, int progress);
    void handleUpdateDownloadCompleted(std::uint64_t generation,
                                       bool success,
                                       const std::wstring &errorMessage);
    std::wstring downloadedUpdatePath() const;
    std::vector<std::wstring> openProfileNames() const;
    std::vector<std::wstring> connectedProfileNames() const;
    bool confirmLaunchDownloadedUpdate();
    bool launchDownloadedUpdate() const;
    void showTabContextMenu(const QPoint &tabBarPoint);
    void reconnectSessionTab(int index);
    void refreshSessionTabStatuses();
    void handleHostResume();
    void connectSelectedProfiles();
    void openConnectionsByName(const std::vector<std::wstring> &connectionNames);
    std::wstring selectedProfileName() const;
    std::vector<int> selectedProfileRows() const;
    std::vector<std::wstring> selectedProfileNames() const;
    std::vector<Profile> currentVisibleProfiles() const;
    void selectProfileByName(const std::wstring &profileName);
    void addSessionTab(const Profile &profile);
    void updateSessionTabState(const std::wstring &profileName, FreeRdpProcess::State state);
    void migrateSessionIdentity(const std::wstring &oldName, const std::wstring &newName);
    FreeRdpProcess::State sessionStateForProfile(const QString &profileName) const;
    int sessionTabIndexForProfileName(const std::wstring &profileName) const;
    QtRdpSessionWidget *sessionWidgetForTab(int index) const;
    std::wstring sessionProfileNameForWidget(const QtRdpSessionWidget *widget) const;
    void warnProfilePersistFailed();
    QWidget *createSessionPage(const Profile &profile);
    std::vector<QRect> captionExclusionRects() const;

    ProfileRepository m_repository;
    std::vector<std::wstring> m_startupConnectionNames;
    QWidget *m_titleBar = nullptr;
    QWidget *m_connectionsHeader = nullptr;
    QLabel *m_connectionsTitle = nullptr;
    int m_collapsedSidebarWidth = 220;
    QSplitter *m_splitter = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_logoButton = nullptr;
    QTabBar *m_tabBar = nullptr;
    QToolButton *m_updateButton = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QToolButton *m_infoButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_profileList = nullptr;
    QWidget *m_sidebar = nullptr;
    QTabWidget *m_tabs = nullptr;
    QShortcut *m_newConnectionShortcut = nullptr;
    QShortcut *m_openConnectionsShortcut = nullptr;
    QShortcut *m_fullScreenShortcut = nullptr;
    QShortcut *m_exitFullScreenShortcut = nullptr;
    std::map<std::wstring, FreeRdpProcess::State> m_sessionStates;
    QTimer *m_updateCheckTimer = nullptr;
    QTimer *m_tabStatusTimer = nullptr;
    updater::ReleaseAsset m_updateRelease;
    ui::UpdateUiState m_updateState = ui::UpdateUiState::Hidden;
    bool m_updateCheckInFlight = false;
    bool m_updateDownloadInFlight = false;
    int m_updateDownloadProgress = -1;
    std::uint64_t m_updateCheckGeneration = 0;
    std::uint64_t m_updateDownloadGeneration = 0;
    std::thread m_updateCheckThread;
    std::thread m_updateDownloadThread;
    bool m_persistWarningShown = false;
    bool m_isFullScreen = false;
    bool m_wasMaximizedBeforeFullScreen = false;
};
