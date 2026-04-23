#pragma once

#include <QWidget>
#include "FreeRdpProcess.h"

class QLabel;

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

private:
    void onStateChanged(FreeRdpProcess::State state);
    HWND findChildWindow() const;
    void resizeChildWindow();
    void showOverlay(const QString &text);

    FreeRdpProcess *m_process = nullptr;
    QLabel *m_overlay = nullptr;
    HWND m_childWindow = nullptr;
};
