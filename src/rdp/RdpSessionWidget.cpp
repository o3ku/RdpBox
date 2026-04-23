#include "RdpSessionWidget.h"

#include <QLabel>
#include <QResizeEvent>
#include <QTimer>
#include <QDebug>
#include <QMouseEvent>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

RdpSessionWidget::RdpSessionWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
}

RdpSessionWidget::~RdpSessionWidget()
{
    if (m_process)
        m_process->stop();
}

void RdpSessionWidget::connectToHost(const QString &exePath,
                                      const QString &host,
                                      int port,
                                      const QString &username,
                                      const QString &password,
                                      bool clipboardEnabled,
                                      bool ignoreCertificate)
{
    if (m_process) {
        m_process->disconnect(this);
        delete m_process;
    }

    m_childWindow = nullptr;
    m_process = new FreeRdpProcess(this);

    connect(m_process, &FreeRdpProcess::stateChanged,
            this, &RdpSessionWidget::onStateChanged);

    m_process->start(exePath, host, port, username, password, winId(),
                     clipboardEnabled, ignoreCertificate);

    showOverlay("Connecting...");
}

void RdpSessionWidget::onStateChanged(FreeRdpProcess::State state)
{
    emit titleStateChanged(state);

    switch (state) {
    case FreeRdpProcess::State::Running:
        QTimer::singleShot(500, this, [this]() {
            m_childWindow = findChildWindow();
            if (m_childWindow) {
                resizeChildWindow();
                SetFocus(m_childWindow);
            } else {
                qDebug("RdpSessionWidget: failed to find child window");
            }
        });
        delete m_overlay;
        m_overlay = nullptr;
        break;
    case FreeRdpProcess::State::Finished:
        m_childWindow = nullptr;
        showOverlay("Disconnected - Click to Reconnect");
        break;
    default:
        break;
    }
}

HWND RdpSessionWidget::findChildWindow() const
{
    HWND result = nullptr;
    EnumChildWindows(reinterpret_cast<HWND>(winId()),
        [](HWND child, LPARAM lParam) -> BOOL {
            *reinterpret_cast<HWND*>(lParam) = child;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void RdpSessionWidget::resizeChildWindow()
{
    if (!m_childWindow)
        return;
    MoveWindow(m_childWindow, 0, 0, width(), height(), TRUE);
}

void RdpSessionWidget::showOverlay(const QString &text)
{
    if (!m_overlay) {
        m_overlay = new QLabel(text, this);
        m_overlay->setAlignment(Qt::AlignCenter);
        m_overlay->setStyleSheet(
            "QLabel { background: #1e1e1e; color: #cccccc; font-size: 18px; }");
    } else {
        m_overlay->setText(text);
    }
    m_overlay->setGeometry(0, 0, width(), height());
    m_overlay->show();
    m_overlay->raise();
}

void RdpSessionWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    resizeChildWindow();
    if (m_overlay)
        m_overlay->setGeometry(0, 0, width(), height());
}

void RdpSessionWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    if (m_childWindow)
        SetFocus(m_childWindow);
}

void RdpSessionWidget::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
    if (m_process && m_process->state() == FreeRdpProcess::State::Finished)
        emit reconnectRequested();
}
