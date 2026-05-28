#pragma once

#include <afxcmn.h>
#include <afxwin.h>

#include <memory>

#include "common/UpdateClient.h"
#include "ui/BrowserTabBar.h"
#include "ui/MainWindowStatePersistence.h"
#include "ui/SessionHostWnd.h"

#include <cstdint>
#include <string>
#include <vector>

class ProfileRepository;
class SessionManager;
struct Profile;
class AboutDialog;

class MainWindow : public CFrameWnd
{
    DECLARE_DYNAMIC(MainWindow)

public:
    static constexpr UINT WM_APP_OPEN_CONNECTIONS = WM_APP + 1;
    static constexpr UINT WM_APP_OPEN_STARTUP_CONNECTIONS = WM_APP + 2;
    static constexpr UINT WM_APP_UPDATE_CHECK_COMPLETED = WM_APP + 3;
    static constexpr UINT WM_APP_UPDATE_DOWNLOAD_COMPLETED = WM_APP + 4;
    static constexpr UINT WM_APP_UPDATE_DOWNLOAD_PROGRESS = WM_APP + 5;

    MainWindow();
    ~MainWindow() override;

    bool createShell();
    void setStartupConnectionNames(std::vector<std::wstring> connectionNames);

    BOOL PreTranslateMessage(MSG *msg) override;

protected:
    afx_msg int OnCreate(LPCREATESTRUCT createStruct);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnActivate(UINT state, CWnd *otherWnd, BOOL minimized);
    afx_msg void OnPaint();
    afx_msg void OnOpenConnections();
    afx_msg void OnMainNew();
    afx_msg void OnMainAbout();
    afx_msg void OnContextMenu(CWnd *window, CPoint point);
    afx_msg BOOL OnEraseBkgnd(CDC *dc);
    afx_msg BOOL OnNcActivate(BOOL active);
    afx_msg void OnTimer(UINT_PTR timerId);
    afx_msg LRESULT OnNcCalcSize(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnNcLButtonDown(WPARAM hitTest, LPARAM lParam);
    afx_msg LRESULT OnNcHitTest(WPARAM, LPARAM lParam);
    afx_msg LRESULT OnNcPaint(WPARAM, LPARAM);
    afx_msg LRESULT OnExitSizeMove(WPARAM, LPARAM);
    afx_msg LRESULT OnOpenConnectionsMessage(WPARAM, LPARAM);
    afx_msg LRESULT OnOpenStartupConnectionsMessage(WPARAM, LPARAM);
    afx_msg LRESULT OnUpdateCheckCompleted(WPARAM, LPARAM);
    afx_msg LRESULT OnUpdateDownloadCompleted(WPARAM, LPARAM);
    afx_msg LRESULT OnUpdateDownloadProgress(WPARAM, LPARAM);
    afx_msg LRESULT OnDwmCompositionChanged(WPARAM, LPARAM);
    afx_msg LRESULT OnDpiChanged(WPARAM, LPARAM);
    afx_msg LRESULT OnPowerBroadcast(WPARAM, LPARAM);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnMouseLeave();

    DECLARE_MESSAGE_MAP()

private:
    void layoutChildren();
    void openConnectionDialog();
    void applyUiFont();
    void toggleFullScreen();
    void setFullScreen(bool enabled);
    void refreshTabStatuses();
    int captionButtonHitTest(CPoint clientPoint) const;
    CRect captionButtonRectFor(int hitCode) const;
    int captionButtonReserveWidth() const;
    void invalidateCaptionButtons();
    void drawCaptionButton(CDC &dc, const CRect &rect, int hitCode) const;
    CRect logoRect() const;
    bool logoHitTest(CPoint clientPoint) const;
    void showLogoMenu();
    void refreshDwmFrame();
    bool isMaximized() const;
    void saveWindowState() const;
    bool restoreWindowState();
    void openConnectionsByName(const std::vector<std::wstring> &connectionNames);
    bool shouldShowUpdateButton() const;
    CRect updateButtonRect() const;
    void invalidateUpdateButton();
    CString updateTooltipText() const;
    CString updateButtonText() const;
    void updateCaptionTooltip();
    void startBackgroundUpdateCheck();
    void startBackgroundUpdateDownload();
    std::wstring downloadedUpdatePath() const;
    bool launchDownloadedUpdate() const;

    enum class UpdateButtonState
    {
        Hidden,
        Available,
        Downloading,
        Downloaded,
    };

    static constexpr int kUpdateCaptionButtonHit = 0x4001;

    BrowserTabBar m_tabBar;
    HICON m_logoIcon = nullptr;
    SessionHostWnd m_sessionHost;
    CFont m_uiFont;
    std::unique_ptr<SessionManager> m_sessionManager;
    std::unique_ptr<ProfileRepository> m_profileRepository;
    bool m_isFullScreen = false;
    static constexpr UINT_PTR kTabStatusTimerId = 1;
    DWORD m_savedStyle = 0;
    DWORD m_savedExStyle = 0;
    CRect m_savedRect;
    int m_hoverCaptionButton = 0;
    bool m_trackingMouse = false;
    bool m_logoHovered = false;
    bool m_inMoveOrSizeLoop = false;
    std::vector<std::wstring> m_startupConnectionNames;
    CToolTipCtrl m_captionTooltip;
    CString m_captionTooltipText;
    UpdateButtonState m_updateButtonState = UpdateButtonState::Hidden;
    updater::ReleaseAsset m_updateRelease;
    bool m_updateCheckInFlight = false;
    bool m_updateDownloadInFlight = false;
    int m_updateDownloadProgress = -1;
    std::uint64_t m_updateCheckGeneration = 0;
    std::uint64_t m_updateDownloadGeneration = 0;
    static constexpr UINT_PTR kUpdateCheckTimerId = 2;
};
