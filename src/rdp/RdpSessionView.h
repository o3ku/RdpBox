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
#include "rdp/RdpProcessEventQueueBehavior.h"
#include "rdp/RdpResolutionRecovery.h"
#include "rdp/RdpResumeRecovery.h"
#include "rdp/RdpSessionMouseState.h"
#include "rdp/RdpSessionKeyboardState.h"
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
    void handleHostResume(bool autoReconnect);
    void handleBecameVisible();

    bool canCaptureSystemKeys() const;
    unsigned int activeKeyboardModifiers() const;
    bool shouldCaptureLowLevelKey(const RdpLowLevelKeyEvent &event,
                                  const RdpKeyboardPhysicalState &physical) const;
    std::uint32_t messageForLowLevelKey(const RdpLowLevelKeyEvent &event,
                                        const RdpKeyboardPhysicalState &physical) const;
    void noteConsumedLocalShortcutKey(unsigned int virtualKey);
    bool consumeReservedShortcutKey(unsigned int virtualKey);
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
    static constexpr UINT_PTR kMouseMoveTimerId = 2;

    struct ProcessBinding
    {
        HWND hwnd = nullptr;
        std::uintptr_t generation = 0;
        std::mutex certMutex;
        std::optional<rdp::process_event_queue::PendingCertificateRequest> pendingCert;
    };

    void bindProcessCallbacks(std::uintptr_t generation);
    void clearProcessCallbacks();
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
    void syncRecoveryTimer();
    void beginFrameCapture(const wchar_t *reason);
    void captureFrameIfRequested(const FrameBuffer &frame);
    void syncMouseModifiers(UINT mouseFlags);
    RdpKeyboardPhysicalState currentKeyboardPhysicalState() const;
    SizeI currentViewSize() const;
    SizeI fullScreenSize() const;
    void releaseCursorHandle();
    void drawOverlay(CDC &dc, const CRect &rect);
    void flushPendingMouseMove();
    void handleMouseButtonDown(UINT flags, CPoint point, MouseButton button, bool allowReconnect);
    void handleMouseButtonUp(UINT flags, CPoint point, MouseButton button);
    void sendKeyboardAction(const RdpSessionKeyboardState::KeyAction &action);
    void sendKeyboardActions(const std::vector<RdpSessionKeyboardState::KeyAction> &actions);
    void releaseKeyboardTargetIfInactive();
    void sendTrackedMouseButton(MouseButton button, bool down, PointI point);
    void releasePressedMouseButtons();
    void releaseAllPressedKeys();

    std::shared_ptr<FreeRdpProcess> m_process;
    std::shared_ptr<ProcessBinding> m_processBinding;
    std::uintptr_t m_processGeneration = 0;
    bool m_resizeSuppressed = false;
    SizeI m_pendingResize;
    bool m_hasPendingResize = false;
    std::function<void()> m_reconnectRequested;
    std::function<void()> m_connectedCallback;
    RdpSessionKeyboardState m_keyboardState;
    RdpResizeBurstTracker m_resizeBurstTracker;
    RdpResolutionRecovery m_resolutionRecovery;
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
    RdpSessionMouseState m_mouseState;
};
