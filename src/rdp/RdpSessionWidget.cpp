#include "RdpSessionWidget.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>
#include <QApplication>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
RdpSessionWidget *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;

bool isSystemKey(DWORD vkCode)
{
    return vkCode == VK_LWIN || vkCode == VK_RWIN;
}

bool isAltKey(DWORD vkCode)
{
    return vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU;
}

bool shouldCaptureLowLevelKey(const KBDLLHOOKSTRUCT *info)
{
    if (!info)
        return false;

    if (isSystemKey(info->vkCode) || isAltKey(info->vkCode))
        return true;

    return info->vkCode == VK_TAB && (info->flags & LLKHF_ALTDOWN);
}

LRESULT CALLBACK rdpSessionKeyboardHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION || !g_systemKeyTarget)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (!info || !shouldCaptureLowLevelKey(info) || !g_systemKeyTarget->canCaptureSystemKeys())
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const bool wasDown = false;
    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
    const bool sysContext = isAltKey(info->vkCode) || (info->flags & LLKHF_ALTDOWN);
    const quint32 message = keyUp
        ? (sysContext ? WM_SYSKEYUP : WM_KEYUP)
        : (sysContext ? WM_SYSKEYDOWN : WM_KEYDOWN);
    g_systemKeyTarget->forwardNativeKeyMessage(message, info->vkCode, info->scanCode, extended, wasDown);
    return 1;
}

void ensureKeyboardHook()
{
    if (!g_keyboardHook)
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, rdpSessionKeyboardHook, nullptr, 0);
}

void releaseKeyboardHookIfUnused()
{
    if (!g_systemKeyTarget && g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}
}

RdpSessionWidget::RdpSessionWidget(QWidget *parent)
    : QWidget(parent)
    , m_resizeTimer(new QTimer(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(300);
    connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
        if (m_connected && m_process)
            m_process->requestResize(size());
    });
}

RdpSessionWidget::~RdpSessionWidget()
{
    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();

    if (m_process)
        m_process->stop();
}

void RdpSessionWidget::connectToHost(const QString &host,
                                     int port,
                                     const QString &username,
                                     const QString &password,
                                     bool clipboardEnabled,
                                     bool ignoreCertificate)
{
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

    m_connected = false;
    m_process = new FreeRdpProcess(this);

    connect(m_process, &FreeRdpProcess::stateChanged,
            this, &RdpSessionWidget::onStateChanged);
    connect(m_process, &FreeRdpProcess::frameUpdated,
            this, qOverload<>(&RdpSessionWidget::update));
    connect(m_process, &FreeRdpProcess::desktopResized,
            this, [this](const QSize &) {
        update();
    });
    connect(m_process, &FreeRdpProcess::cursorUpdated,
            this, [this]() {
        if (m_process)
            setCursor(m_process->cursor());
    });

    m_process->start(host, port, username, password,
                     width(), height(),
                     clipboardEnabled, ignoreCertificate);

    showOverlay("Connecting...");
}

void RdpSessionWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (!m_process)
        return;

    const QImage frame = m_process->frame();
    if (frame.isNull())
        return;

    painter.drawImage(rect(), frame);
}

void RdpSessionWidget::onStateChanged(FreeRdpProcess::State state)
{
    emit titleStateChanged(state);

    switch (state) {
    case FreeRdpProcess::State::Running:
        m_connected = true;
        delete m_overlay;
        m_overlay = nullptr;
        update();
        if (m_process)
            setCursor(m_process->cursor());
        setFocusToFreeRdp();
        break;
    case FreeRdpProcess::State::Finished:
        m_connected = false;
        update();
        unsetCursor();
        showOverlay("Disconnected - Click to Reconnect");
        break;
    default:
        break;
    }
}

void RdpSessionWidget::setFocusToFreeRdp()
{
    if (!m_process)
        return;

    setFocus(Qt::OtherFocusReason);
    g_systemKeyTarget = this;
    ensureKeyboardHook();
    m_process->sendFocusIn();
}

void RdpSessionWidget::showOverlay(const QString &text)
{
    if (!m_overlay) {
        m_overlay = new QLabel(text, this);
        m_overlay->setAlignment(Qt::AlignCenter);
        m_overlay->setStyleSheet(
            "QLabel { background: rgba(30, 30, 30, 220); color: #cccccc; font-size: 18px; }");
    } else {
        m_overlay->setText(text);
    }

    m_overlay->setGeometry(rect());
    m_overlay->show();
    m_overlay->raise();
}

void RdpSessionWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (m_overlay)
        m_overlay->setGeometry(rect());

    if (m_connected)
        m_resizeTimer->start();
}

void RdpSessionWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocusToFreeRdp();
}

void RdpSessionWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    setFocusToFreeRdp();
}

void RdpSessionWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);

    if (g_systemKeyTarget == this)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
}

bool RdpSessionWidget::canCaptureSystemKeys() const
{
    return m_process
        && m_process->state() == FreeRdpProcess::State::Running
        && isVisible()
        && isActiveWindow()
        && hasFocus();
}

void RdpSessionWidget::forwardNativeKeyMessage(quint32 message, quint32 vkCode, quint32 scanCode,
                                               bool extended, bool wasDown)
{
    if (!m_process)
        return;

    quint32 lParam = (scanCode & 0xFFu) << 16;
    if (extended)
        lParam |= 0x01000000u;
    if (wasDown)
        lParam |= 0x40000000u;
    if (message == WM_KEYUP || message == WM_SYSKEYUP)
        lParam |= 0xC0000000u;

    m_process->sendKeyMessage(message, vkCode, lParam);
}

void RdpSessionWidget::keyPressEvent(QKeyEvent *event)
{
    if (m_process)
        forwardNativeKeyMessage(WM_KEYDOWN, event->nativeVirtualKey(), event->nativeScanCode(),
                                false, false);
    event->accept();
}

void RdpSessionWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (m_process)
        forwardNativeKeyMessage(WM_KEYUP, event->nativeVirtualKey(), event->nativeScanCode(),
                                false, true);
    event->accept();
}

void RdpSessionWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_process && m_process->state() == FreeRdpProcess::State::Finished) {
        emit reconnectRequested();
        return;
    }

    setFocusToFreeRdp();
    if (m_process)
        m_process->sendMouseButton(event->button(), true, event->pos(), size());
    event->accept();
}

void RdpSessionWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_process)
        m_process->sendMouseButton(event->button(), false, event->pos(), size());
    event->accept();
}

void RdpSessionWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_process)
        m_process->sendMouseMove(event->pos(), size());
    event->accept();
}

void RdpSessionWidget::wheelEvent(QWheelEvent *event)
{
    if (m_process)
        m_process->sendWheel(event->angleDelta(), event->position().toPoint(), size());
    event->accept();
}

bool RdpSessionWidget::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    Q_UNUSED(eventType);

    auto *msg = static_cast<MSG*>(message);
    if (msg && (msg->message == WM_MOUSEACTIVATE || msg->message == WM_SETFOCUS)) {
        setFocusToFreeRdp();
        if (result)
            *result = 0;
    }

    return QWidget::nativeEvent(eventType, message, result);
}
