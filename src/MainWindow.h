#pragma once

#include <afxcmn.h>
#include <afxwin.h>

#include <memory>

#include "ui/BrowserTabBar.h"
#include "ui/SessionHostWnd.h"

#include <string>

class ProfileRepository;
class SessionManager;
struct Profile;
class AboutDialog;

class MainWindow : public CFrameWnd
{
    DECLARE_DYNAMIC(MainWindow)

public:
    static constexpr UINT WM_APP_OPEN_CONNECTIONS = WM_APP + 1;

    MainWindow();
    ~MainWindow() override;

    bool createShell();

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
    afx_msg LRESULT OnDwmCompositionChanged(WPARAM, LPARAM);
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
    void invalidateCaptionButtons();
    void drawCaptionButton(CDC &dc, const CRect &rect, int hitCode) const;
    CRect logoRect() const;
    bool logoHitTest(CPoint clientPoint) const;
    void showLogoMenu();
    bool isMaximized() const;
    void saveWindowState() const;
    bool restoreWindowState();

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
};
