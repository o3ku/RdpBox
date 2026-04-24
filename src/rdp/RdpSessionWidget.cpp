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
    , m_resizeTimer(new QTimer(this))
{
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::ClickFocus);

    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(1000);
    connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
        if (m_connected)
            scheduleReconnectWithSize(width(), height());
    });
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
    m_exePath = exePath;
    m_host = host;
    m_port = port;
    m_username = username;
    m_password = password;
    m_clipboardEnabled = clipboardEnabled;
    m_ignoreCertificate = ignoreCertificate;

    if (m_process) {
        m_process->disconnect(this);
        delete m_process;
    }

    m_childWindow = nullptr;
    m_connected = false;
    m_process = new FreeRdpProcess(this);

    connect(m_process, &FreeRdpProcess::stateChanged,
            this, &RdpSessionWidget::onStateChanged);

    m_process->start(exePath, host, port, username, password, winId(),
                     width(), height(),
                     clipboardEnabled, ignoreCertificate);

    showOverlay("Connecting...");
}

void RdpSessionWidget::onStateChanged(FreeRdpProcess::State state)
{
    emit titleStateChanged(state);

    switch (state) {
    case FreeRdpProcess::State::Running:
        m_connected = true;
        {
            auto *timer = new QTimer(this);
            int attempts = 0;
            connect(timer, &QTimer::timeout, this, [this, timer, attempts]() mutable {
                attempts++;
                m_childWindow = findFreeRdpWindow();
                if (m_childWindow) {
                    qDebug() << "RdpSessionWidget: found FreeRDP window after" << attempts << "attempts";
                    // Bring child window to top and size it
                    SetWindowPos(m_childWindow, HWND_TOP, 0, 0, width(), height(), SWP_SHOWWINDOW);
                    setFocusToFreeRdp();
                    timer->stop();
                    timer->deleteLater();
                } else if (attempts >= 20) {
                    qDebug("RdpSessionWidget: failed to find FreeRDP window");
                    timer->stop();
                    timer->deleteLater();
                }
            });
            timer->start(300);
        }
        delete m_overlay;
        m_overlay = nullptr;
        break;
    case FreeRdpProcess::State::Finished:
        m_childWindow = nullptr;
        m_connected = false;
        showOverlay("Disconnected - Click to Reconnect");
        break;
    default:
        break;
    }
}

HWND RdpSessionWidget::findFreeRdpWindow() const
{
    HWND result = nullptr;
    EnumChildWindows(reinterpret_cast<HWND>(winId()),
        [](HWND child, LPARAM lParam) -> BOOL {
            wchar_t className[256];
            GetClassNameW(child, className, 256);
            if (_wcsicmp(className, L"FreeRDP") == 0) {
                *reinterpret_cast<HWND*>(lParam) = child;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void RdpSessionWidget::setFocusToFreeRdp()
{
    if (!m_childWindow)
        return;
    const HWND thisWindow = reinterpret_cast<HWND>(winId());
    const DWORD childThreadId = GetWindowThreadProcessId(m_childWindow, nullptr);
    const DWORD currentThreadId = GetCurrentThreadId();

    SetForegroundWindow(thisWindow);

    bool attached = false;
    if (childThreadId != 0 && childThreadId != currentThreadId)
        attached = AttachThreadInput(currentThreadId, childThreadId, TRUE) != FALSE;

    SetFocus(m_childWindow);

    if (attached)
        AttachThreadInput(currentThreadId, childThreadId, FALSE);
}

void RdpSessionWidget::scheduleReconnectWithSize(int w, int h)
{
    if (m_process)
        m_process->stop();

    m_childWindow = nullptr;
    m_connected = false;
    showOverlay("Connecting...");

    // Delay before reconnecting: give the RDP server time to release
    // the old session after wfreerdp.exe was killed.
    QTimer::singleShot(1500, this, [this, w, h]() {
        m_process = new FreeRdpProcess(this);
        connect(m_process, &FreeRdpProcess::stateChanged,
                this, &RdpSessionWidget::onStateChanged);

        m_process->start(m_exePath, m_host, m_port, m_username, m_password,
                         winId(), w, h,
                         m_clipboardEnabled, m_ignoreCertificate);
    });
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
    if (m_overlay)
        m_overlay->setGeometry(0, 0, width(), height());
    if (m_connected)
        m_resizeTimer->start();
}

void RdpSessionWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    setFocusToFreeRdp();
}

void RdpSessionWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_process && m_process->state() == FreeRdpProcess::State::Finished) {
        emit reconnectRequested();
        return;
    }
    setFocusToFreeRdp();
}

void RdpSessionWidget::mouseReleaseEvent(QMouseEvent *event)
{
    setFocusToFreeRdp();
}

bool RdpSessionWidget::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (m_childWindow) {
        auto *msg = static_cast<MSG*>(message);
        if (msg->message == WM_MOUSEACTIVATE) {
            setFocusToFreeRdp();
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}
