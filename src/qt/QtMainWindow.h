#pragma once

#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "common/UpdateClient.h"
#include "ui/MainWindowUpdateBehavior.h"

#include <QMainWindow>

#include <cstdint>
#include <string>
#include <vector>

namespace QWK
{
class WidgetWindowAgent;
}

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPoint;
class QPushButton;
class QToolButton;
class QTabWidget;
class QTimer;
class QVBoxLayout;
class QWidget;
class QtRdpSessionWidget;

class QtMainWindow : public QMainWindow
{
public:
    explicit QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                          QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildTitleBar(QVBoxLayout *rootLayout);
    void installShortcuts();
    void refreshProfileList();
    void refreshActions();
    void refreshUpdateButton();
    void refreshWindowControls();
    void configureHomeTab();
    void saveWindowState() const;
    bool restoreWindowState();
    int nativeHitTestForPoint(const QPoint &windowPoint) const;
    void configureWindowChrome();
    void addProfile();
    void editSelectedProfile();
    void duplicateSelectedProfile();
    void deleteSelectedProfile();
    void moveSelectedProfileBy(int delta);
    void moveProfileByDrop(int sourceRow, int insertIndex);
    void closeSessionTab(int index);
    void touchLastConnectedAt(const Profile &profile);
    void toggleFullScreen();
    void setFullScreen(bool enabled);
    void focusConnections();
    void showLogoMenu();
    void showAboutDialog();
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
    bool confirmLaunchDownloadedUpdate();
    bool launchDownloadedUpdate() const;
    void handleTabMoved(int fromIndex, int toIndex);
    void showTabContextMenu(const QPoint &tabBarPoint);
    void reconnectSessionTab(int index);
    void connectSelectedProfiles();
    void openConnectionsByName(const std::vector<std::wstring> &connectionNames);
    Profile selectedProfile() const;
    std::wstring selectedProfileName() const;
    std::vector<int> selectedProfileRows() const;
    std::vector<std::wstring> selectedProfileNames() const;
    std::vector<Profile> currentVisibleProfiles() const;
    void selectProfileByName(const std::wstring &profileName);
    void addSessionTab(const Profile &profile);
    int sessionTabIndexForProfileName(const std::wstring &profileName) const;
    QtRdpSessionWidget *sessionWidgetForTab(int index) const;
    QWidget *createHomePage() const;
    QWidget *createSessionPage(const Profile &profile);
    std::vector<QRect> captionExclusionRects() const;

    ProfileRepository m_repository;
    std::vector<std::wstring> m_startupConnectionNames;
    QWidget *m_titleBar = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_updateButton = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
#ifdef RDPBOX_USE_QWINDOWKIT
    QWK::WidgetWindowAgent *m_windowAgent = nullptr;
#endif
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_profileList = nullptr;
    QWidget *m_sidebar = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_moveUpButton = nullptr;
    QPushButton *m_moveDownButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_updateCheckTimer = nullptr;
    updater::ReleaseAsset m_updateRelease;
    ui::UpdateUiState m_updateState = ui::UpdateUiState::Hidden;
    bool m_updateCheckInFlight = false;
    bool m_updateDownloadInFlight = false;
    int m_updateDownloadProgress = -1;
    std::uint64_t m_updateCheckGeneration = 0;
    std::uint64_t m_updateDownloadGeneration = 0;
    bool m_restoringWindowState = false;
    bool m_adjustingTabMove = false;
    bool m_isFullScreen = false;
    bool m_wasMaximizedBeforeFullScreen = false;
};
