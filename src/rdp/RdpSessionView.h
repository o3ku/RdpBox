#pragma once

#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"
#include "rdp/RdpModifierSyncTracker.h"
#include "rdp/RdpResizeBurstTracker.h"

#include <afxwin.h>

#include <functional>
#include <memory>

class CRdpSessionView : public CWnd
{
    DECLARE_DYNAMIC(CRdpSessionView)

public:
    CRdpSessionView();
    ~CRdpSessionView() override;

    bool create(CWnd *parent, const CRect &rect);
    void connectToHost(const Profile &profile);
    void reconnect();
    void disconnect();
    void setReconnectRequestedCallback(std::function<void()> callback);

    bool canCaptureSystemKeys() const;
    void forwardNativeKeyMessage(quint32 message, quintptr wParam, qintptr lParam);

protected:
    afx_msg BOOL OnEraseBkgnd(CDC *dc);
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR timerId);
    afx_msg void OnSetFocus(CWnd *oldWnd);
    afx_msg void OnKillFocus(CWnd *newWnd);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg void OnRButtonDown(UINT flags, CPoint point);
    afx_msg void OnRButtonUp(UINT flags, CPoint point);
    afx_msg void OnMButtonDown(UINT flags, CPoint point);
    afx_msg void OnMButtonUp(UINT flags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT flags, short zDelta, CPoint point);
    afx_msg UINT OnGetDlgCode();

    DECLARE_MESSAGE_MAP()

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void connectSignals();
    void disconnectSignals();
    void startProcess();
    void stopProcess();
    void onStateChanged(FreeRdpProcess::State state);
    void updateCursorFromProcess();
    void setFocusToFreeRdp();
    void showOverlay(const CString &text);
    void clearOverlay();
    void syncMouseModifiers(UINT flags);
    Qt::KeyboardModifiers currentModifiers() const;
    QSize currentViewSize() const;
    HCURSOR cursorHandleFromQtCursor(const QCursor &cursor);
    HCURSOR cursorHandleFromPixmap(const QPixmap &pixmap, const QPoint &hotspot);
    void releaseCursorHandle();

    std::unique_ptr<FreeRdpProcess> m_process;
    QMetaObject::Connection m_stateConnection;
    QMetaObject::Connection m_frameConnection;
    QMetaObject::Connection m_desktopConnection;
    QMetaObject::Connection m_cursorConnection;
    std::function<void()> m_reconnectRequested;
    RdpModifierSyncTracker m_modifierTracker;
    RdpResizeBurstTracker m_resizeBurstTracker;
    Profile m_profile;
    CString m_overlayText;
    HCURSOR m_cursorHandle = nullptr;
    bool m_ownsCursorHandle = false;
    bool m_connected = false;
    bool m_created = false;
};
