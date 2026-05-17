#pragma once

#include <afxwin.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"
#include "rdp/RdpInputEventUtil.h"
#include "rdp/RdpModifierSyncTracker.h"
#include "rdp/RdpMouseMoveCoalescer.h"
#include "rdp/RdpReservedShortcutTracker.h"
#include "rdp/RdpResumeRecovery.h"
#include "rdp/RdpResizeBurstTracker.h"

class CRdpSessionView : public CWnd
{
    DECLARE_DYNAMIC(CRdpSessionView)

public:
    static constexpr UINT WM_APP_RDP_STATE = WM_APP + 101;
    static constexpr UINT WM_APP_RDP_FRAME = WM_APP + 102;
    static constexpr UINT WM_APP_RDP_CURSOR = WM_APP + 103;
    static constexpr UINT WM_APP_RDP_CERT = WM_APP + 104;
    static constexpr UINT_PTR kResumeRecoveryTimerId = 3;

    CRdpSessionView();
    ~CRdpSessionView() override;

    bool create(CWnd *parent, const CRect &rect);
    void connectToHost(const Profile &profile);
    void reconnect();
    void setReconnectRequestedCallback(std::function<void()> callback);
    void setConnectedCallback(std::function<void()> callback);

    FreeRdpProcess::ConnectionInfo connectionInfo() const;
    bool isConnected() const;

    void setResizeSuppressed(bool suppressed);
    void flushPendingResize();
    void handleHostResume();
    void handleBecameVisible();

    bool canCaptureSystemKeys() const;
    void noteConsumedLocalShortcutKey(unsigned int virtualKey);
    void forwardNativeKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam);

protected:
    afx_msg BOOL OnEraseBkgnd(CDC *dc);
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR timerId);
    afx_msg void OnSetFocus(CWnd *oldWnd);
    afx_msg void OnKillFocus(CWnd *newWnd);
    afx_msg void OnCancelMode();
    afx_msg void OnCaptureChanged(CWnd *wnd);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonDown(UINT flags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);
    afx_msg void OnRButtonDown(UINT flags, CPoint point);
    afx_msg void OnRButtonDblClk(UINT flags, CPoint point);
    afx_msg void OnRButtonUp(UINT flags, CPoint point);
    afx_msg void OnMButtonDown(UINT flags, CPoint point);
    afx_msg void OnMButtonDblClk(UINT flags, CPoint point);
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
    void disableLocalIme() const;
    void startProcess();
    void stopProcess(bool showDisconnectedOverlay = true);
    void onStateChanged(FreeRdpProcess::State state);
    void updateCursorFromProcess();
    void setFocusToFreeRdp();
    void showOverlay(const CString &text);
    void clearOverlay();
    void beginResolutionUpdate();
    void beginResumeRecovery();
    void requestDeferredResumeRefreshIfNeeded();
    void beginFrameCapture(const wchar_t *reason);
    void captureFrameIfRequested(const FrameBuffer &frame);
    void syncMouseModifiers(UINT mouseFlags);
    unsigned int currentModifiers() const;
    SizeI currentViewSize() const;
    SizeI fullScreenSize() const;
    void releaseCursorHandle();
    void drawOverlay(CDC &dc, const CRect &rect);
    void flushPendingMouseMove();
    void sendTrackedKey(const KeyIdentifier &key, bool down, bool wasDown = false);
    bool hasTrackedKey(const KeyIdentifier &key) const;
    void trackKeyState(const KeyIdentifier &key, bool down);
    void rememberReservedShortcutKey(unsigned int virtualKey);
    bool consumeReservedShortcutKey(unsigned int virtualKey);
    void sendSynchronizedModifier(unsigned int virtualKey, bool down);
    void sendTrackedMouseButton(MouseButton button, bool down, PointI point);
    void releasePressedMouseButtons();
    void releaseAllPressedKeys();
    void updatePointerPosition(PointI point);
    PointI currentPointerPosition() const;

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
    RdpResumeRecovery m_resumeRecovery;
    Profile m_profile;
    CString m_overlayText;
    CFont m_overlayFont;
    bool m_reconnecting = false;
    bool m_resolutionUpdatePending = false;
    bool m_waitingForFirstContentFrame = false;
    bool m_frameGateActive = false;
    int m_frameGateRemaining = 0;
    std::wstring m_captureDirectory;
    int m_captureFramesRemaining = 0;
    int m_captureFrameIndex = 0;
    uint64_t m_cachedFrameGeneration = 0;
    FrameBuffer m_cachedFrame;
    HCURSOR m_cursorHandle = nullptr;
    bool m_ownsCursorHandle = false;
    bool m_connected = false;
    bool m_created = false;
    std::vector<KeyIdentifier> m_pressedKeys;
    RdpReservedShortcutTracker m_reservedShortcutTracker;
    unsigned int m_pressedMouseButtons = 0;
    PointI m_lastPointerPoint{};
    bool m_hasLastPointerPoint = false;

    mutable std::mutex m_certMutex;
    std::optional<FreeRdpProcess::CertificateChallenge> m_pendingCert;
};
