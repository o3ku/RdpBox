#pragma once

#include "common/NativeTypes.h"
#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"

#include <QWidget>

#include <functional>
#include <memory>

class QLabel;

class QtRdpSessionWidget : public QWidget
{
public:
    explicit QtRdpSessionWidget(Profile profile, QWidget *parent = nullptr);
    ~QtRdpSessionWidget() override;

    void connectToHost();
    void reconnect();
    bool isConnected() const;
    void setStateChangedCallback(std::function<void(FreeRdpProcess::State)> callback);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    void bindProcessCallbacks();
    void clearProcessCallbacks();
    void stopProcess();
    void updateState(FreeRdpProcess::State state);
    void consumeFrame();
    void updateCursor();
    void requestResize();
    bool confirmCertificate(const FreeRdpProcess::CertificateChallenge &challenge);
    SizeI viewSize() const;
    PointI pointFromMouseEvent(const QMouseEvent *event) const;
    void sendMouseButton(QMouseEvent *event, bool down);
    void sendKeyEvent(QKeyEvent *event, bool down);

    Profile m_profile;
    std::shared_ptr<FreeRdpProcess> m_process;
    FreeRdpProcess::State m_state = FreeRdpProcess::State::Idle;
    FrameBuffer m_frame;
    uint64_t m_frameGeneration = 0;
    QString m_overlayText;
    std::function<void(FreeRdpProcess::State)> m_stateChanged;
};
