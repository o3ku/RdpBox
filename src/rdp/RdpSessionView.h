#pragma once

#include <afxwin.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"
#include "rdp/RdpModifierSyncTracker.h"
#include "rdp/RdpMouseMoveCoalescer.h"
#include "rdp/RdpResizeBurstTracker.h"

class CRdpSessionView : public CWnd
{
    DECLARE_DYNAMIC(CRdpSessionView)

public:
    static constexpr UINT WM_APP_RDP_STATE = WM_APP + 101;
    static constexpr UINT WM_APP_RDP_FRAME = WM_APP + 102;
    static constexpr UINT WM_APP_RDP_CURSOR = WM_APP + 103;
    static constexpr UINT WM_APP_RDP_CERT = WM_APP + 104;

    CRdpSessionView();
    ~CRdpSessionView() override;

    bool create(CWnd *parent, const CRect &rect);
    void connectToHost(const Profile &profile);
    void reconnect();
    void disconnect();
    void setReconnectRequestedCallback(std::function<void()> callback);
    void setConnectedCallback(std::function<void()> callback);

    void setResizeSuppressed(bool suppressed);
    void flushPendingResize();

    bool canCaptureSystemKeys() const;
    void forwardNativeKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);

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
    afx_msg LRESULT OnRdpStateChanged(WPARAM state, LPARAM generation);
    afx_msg LRESULT OnRdpFrameUpdated(WPARAM, LPARAM generation);
    afx_msg LRESULT OnRdpCursorUpdated(WPARAM, LPARAM generation);
    afx_msg LRESULT OnRdpCertRequest(WPARAM, LPARAM generation);

    DECLARE_MESSAGE_MAP()

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void bindProcessCallbacks(std::uintptr_t generation);
    void clearProcessCallbacks();
    bool postProcessMessage(UINT message, WPARAM wParam, std::uintptr_t generation) const;
    bool isCurrentGeneration(std::uintptr_t generation) const;
    bool isInTopLevelResizeBorder() const;
    void disableLocalIme() const;
    void startProcess();
    void stopProcess(bool showDisconnectedOverlay = true);
    void onStateChanged(FreeRdpProcess::State state);
    void updateCursorFromProcess();
    void setFocusToFreeRdp();
    void showOverlay(const CString &text);
    void clearOverlay();
    void syncMouseModifiers(UINT flags);
    unsigned int currentModifiers() const;
    SizeI currentViewSize() const;
    void releaseCursorHandle();
    bool ensureRenderSurface(const FrameBuffer &frame);
    void releaseRenderSurface();
    void copyFrameToRenderSurface(const FrameBuffer &frame);
    void drawRenderSurface(HDC targetDc, const CRect &targetRect) const;
    void drawOverlay(CDC &dc, const CRect &rect);
    void flushPendingMouseMove();

    std::unique_ptr<FreeRdpProcess> m_process;
    std::uintptr_t m_processGeneration = 0;
    bool m_resizeSuppressed = false;
    SizeI m_pendingResize;
    bool m_hasPendingResize = false;
    std::function<void()> m_reconnectRequested;
    std::function<void()> m_connectedCallback;
    RdpModifierSyncTracker m_modifierTracker;
    RdpResizeBurstTracker m_resizeBurstTracker;
    RdpMouseMoveCoalescer m_mouseMoveCoalescer;
    bool m_mouseMoveTimerActive = false;
    Profile m_profile;
    CString m_overlayText;
    CFont m_overlayFont;
    bool m_reconnecting = false;
    uint64_t m_cachedFrameGeneration = 0;
    uint64_t m_renderedFrameGeneration = 0;
    FrameBuffer m_cachedFrame;
    HDC m_renderDc = nullptr;
    HBITMAP m_renderBitmap = nullptr;
    HGDIOBJ m_renderOldBitmap = nullptr;
    void *m_renderBits = nullptr;
    int m_renderWidth = 0;
    int m_renderHeight = 0;
    HCURSOR m_cursorHandle = nullptr;
    bool m_ownsCursorHandle = false;
    bool m_connected = false;
    bool m_created = false;

    mutable std::mutex m_certMutex;
    std::shared_ptr<FreeRdpProcess::CertificateChallenge> m_pendingCert;
};
