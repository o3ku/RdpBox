#pragma once

#include <QWidget>
#include "FreeRdpProcess.h"

class QLabel;
class QPaintEvent;
class QTimer;
class QShowEvent;

class RdpSessionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RdpSessionWidget(QWidget *parent = nullptr);
    ~RdpSessionWidget();

    void connectToHost(const QString &host,
                       int port,
                       const QString &username,
                       const QString &password,
                       bool clipboardEnabled = true,
                       bool ignoreCertificate = true);
    bool canCaptureSystemKeys() const;
    void forwardNativeKeyMessage(quint32 message, quint32 vkCode, quint32 scanCode,
                                 bool extended, bool wasDown);

signals:
    void titleStateChanged(FreeRdpProcess::State state);
    void reconnectRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    virtual void setFocusToFreeRdp();

private:
    void onStateChanged(FreeRdpProcess::State state);
    void showOverlay(const QString &text);

    FreeRdpProcess *m_process = nullptr;
    QLabel *m_overlay = nullptr;
    QTimer *m_resizeTimer = nullptr;
    bool m_connected = false;

    QString m_host;
    int m_port = 3389;
    QString m_username;
    QString m_password;
    bool m_clipboardEnabled = true;
    bool m_ignoreCertificate = true;
};
