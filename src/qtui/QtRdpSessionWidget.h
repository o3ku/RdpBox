#pragma once

#include "common/NativeTypes.h"
#include "common/profiles/Profile.h"
#include "common/rdp/FreeRdpProcess.h"
#include "common/rdp/RdpKeyboardInputRouter.h"
#include "common/rdp/RdpSessionKeyboardHook.h"
#include "common/rdp/RdpMouseMoveCoalescer.h"
#include "common/rdp/RdpResizeBurstTracker.h"
#include "common/rdp/RdpResolutionRecovery.h"

#include <QWidget>
#include <QPointer>

#include <functional>
#include <memory>
#include <vector>

class QLabel;
class QMessageBox;
class QTimer;

class QtRdpSessionWidget : public QWidget, public rdp::session_view_input::RdpSystemKeyTarget
{
    Q_OBJECT

public:
    explicit QtRdpSessionWidget(Profile profile, QWidget *parent = nullptr);
    ~QtRdpSessionWidget() override;

    void connectToHost();
    void reconnect();
    void handleHostResume(bool autoReconnect);
    void handleBecameVisible();
    bool isConnected() const;
    FreeRdpProcess::State state() const;
    FreeRdpProcess::ConnectionInfo connectionInfo() const;
    void setStateChangedCallback(std::function<void(FreeRdpProcess::State)> callback);

protected:
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

    // RdpSystemKeyTarget: WH_KEYBOARD_LL hook entry points (Win/Alt+Tab/Esc...).
    bool canCaptureSystemKeys() const override;
    bool hasWindowFocus() const override;
    bool shouldCaptureLowLevelKey(const RdpLowLevelKeyEvent &event,
                                  const RdpKeyboardPhysicalState &physical) const override;
    std::uint32_t messageForLowLevelKey(const RdpLowLevelKeyEvent &event,
                                        const RdpKeyboardPhysicalState &physical) const override;
    void forwardNativeKeyMessage(std::uint32_t message,
                                 std::uintptr_t wParam,
                                 std::intptr_t lParam) override;
    void releaseKeyboardInputForTargetTransfer() override;

private:
    void bindProcessCallbacks();
    void clearProcessCallbacks();
    void stopProcess(bool showDisconnectedOverlay = false);
    void updateState(FreeRdpProcess::State state);
    void consumeFrame();
    void updateCursor();
    void beginResolutionUpdate();
    void syncRecoveryTimer();
    void handleRecoveryTimer();
    void requestResize();
    void handleResizeTimer();
    void flushPendingMouseMove();
    void handleMouseMoveTimer();
    void promptCertificateChallenge(const FreeRdpProcess::CertificateChallenge &challenge,
                                    std::weak_ptr<FreeRdpProcess> weakProcess);
    SizeI viewSize() const;
    PointI pointFromMouseEvent(const QMouseEvent *event) const;
    unsigned int mouseFlagsFromEvent(const QMouseEvent *event) const;
    void syncMouseModifiers(unsigned int mouseFlags);
    void sendMouseButton(QMouseEvent *event, bool down);
    void sendKeyEvent(QKeyEvent *event, bool down);
    void sendKeyboardAction(const RdpKeyboardInputRouter::KeyAction &action);
    void sendKeyboardActions(const std::vector<RdpKeyboardInputRouter::KeyAction> &actions);
    void releasePressedMouseButtons();
    void releaseAllPressedKeys();

    Profile m_profile;
    std::shared_ptr<FreeRdpProcess> m_process;
    FreeRdpProcess::State m_state = FreeRdpProcess::State::Idle;
    FrameBuffer m_frame;
    uint64_t m_frameGeneration = 0;
    QString m_overlayText;
    std::function<void(FreeRdpProcess::State)> m_stateChanged;
    RdpKeyboardInputRouter m_keyboardRouter;
    RdpMouseMoveCoalescer m_mouseMoveCoalescer;
    RdpResizeBurstTracker m_resizeBurstTracker;
    RdpResolutionRecovery m_resolutionRecovery;
    QTimer *m_mouseMoveTimer = nullptr;
    QTimer *m_resizeTimer = nullptr;
    QTimer *m_recoveryTimer = nullptr;
    QPointer<QMessageBox> m_certPrompt;
    unsigned int m_pressedMouseButtons = 0;
    PointI m_lastPointerPoint;
    bool m_hasLastPointerPoint = false;
    bool m_reconnecting = false;
    bool m_frameGateActive = false;
    int m_frameGateRemaining = 0;
    bool m_waitingForFirstContentFrame = false;
    bool m_resolutionUpdatePending = false;
};
