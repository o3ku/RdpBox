#pragma once

#include <afxcmn.h>
#include <afxwin.h>

#include <memory>

class ProfileRepository;
class SessionManager;

class MainWindow : public CFrameWnd
{
    DECLARE_DYNAMIC(MainWindow)

public:
    static constexpr UINT WM_APP_OPEN_CONNECTIONS = WM_APP + 1;

    MainWindow();
    ~MainWindow() override;

    bool createShell();

protected:
    afx_msg int OnCreate(LPCREATESTRUCT createStruct);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg void OnNewConnection();
    afx_msg void OnOpenConnections();
    afx_msg void OnTabSelectionChanged(NMHDR *notify, LRESULT *result);
    afx_msg void OnContextMenu(CWnd *window, CPoint point);
    afx_msg LRESULT OnNcLButtonDown(WPARAM hitTest, LPARAM lParam);
    afx_msg LRESULT OnExitSizeMove(WPARAM, LPARAM);
    afx_msg LRESULT OnOpenConnectionsMessage(WPARAM selectionRequired, LPARAM);

    DECLARE_MESSAGE_MAP()

private:
    void layoutChildren();
    void openConnectionDialog(bool selectionRequired);
    bool hasOpenTabs() const;
    int tabIndexAtScreenPoint(CPoint point) const;
    void applyUiFont();

    CTabCtrl m_tabCtrl;
    CButton m_newButton;
    CButton m_connectionsButton;
    CWnd m_sessionHost;
    CFont m_uiFont;
    std::unique_ptr<SessionManager> m_sessionManager;
    std::unique_ptr<ProfileRepository> m_profileRepository;
    bool m_isClosing = false;
};
