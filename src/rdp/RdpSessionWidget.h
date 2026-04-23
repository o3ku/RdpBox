#pragma once

#include <QWidget>
#include "FreeRdpProcess.h"

class QLabel;
class QTimer;

class RdpSessionWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RdpSessionWidget(QWidget *parent = nullptr);
    ~RdpSessionWidget();

    void connectToHost(const QString &exePath,
                       const QString &host,
                       int port,
                       const QString &username,
                       const QString &password,
                       bool clipboardEnabled = true,
                       bool ignoreCertificate = true);

signals:
    void titleStateChanged(FreeRdpProcess::State state);
    void reconnectRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;

private:
    void onStateChanged(FreeRdpProcess::State state);
    HWND findFreeRdpWindow() const;
    void setFocusToFreeRdp();
    void scheduleReconnectWithSize(int w, int h);
    void showOverlay(const QString &text);

    FreeRdpProcess *m_process = nullptr;
    QLabel *m_overlay = nullptr;
    HWND m_childWindow = nullptr;

    // Reconnect-on-resize state
    QTimer *m_resizeTimer = nullptr;
    bool m_connected = false;

    // Connection params for reconnect
    QString m_exePath;
    QString m_host;
    int m_port = 3389;
    QString m_username;
    QString m_password;
    bool m_clipboardEnabled = true;
    bool m_ignoreCertificate = true;
};
