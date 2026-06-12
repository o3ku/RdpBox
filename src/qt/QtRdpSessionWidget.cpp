#include "qt/QtRdpSessionWidget.h"

#include "rdp/FreeRdpProcessNative.h"
#include "rdp/RdpCursorClassifier.h"
#include "rdp/RdpInputEventUtil.h"

#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>

namespace
{
QString stateText(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Idle:
        return QObject::tr("Disconnected");
    case FreeRdpProcess::State::Starting:
        return QObject::tr("Connecting");
    case FreeRdpProcess::State::Running:
        return QString();
    case FreeRdpProcess::State::Finished:
        return QObject::tr("Disconnected");
    }
    return QObject::tr("Disconnected");
}

MouseButton mouseButtonFromQt(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return MouseButton::Left;
    case Qt::RightButton:
        return MouseButton::Right;
    case Qt::MiddleButton:
        return MouseButton::Middle;
    case Qt::BackButton:
        return MouseButton::Back;
    case Qt::ForwardButton:
        return MouseButton::Forward;
    default:
        return MouseButton::None;
    }
}

Qt::CursorShape cursorShapeFromKind(CursorKind kind)
{
    switch (kind) {
    case CursorKind::Hidden:
        return Qt::BlankCursor;
    case CursorKind::IBeam:
        return Qt::IBeamCursor;
    case CursorKind::Cross:
        return Qt::CrossCursor;
    case CursorKind::Wait:
        return Qt::WaitCursor;
    case CursorKind::AppStarting:
        return Qt::BusyCursor;
    case CursorKind::Hand:
        return Qt::PointingHandCursor;
    case CursorKind::SizeWE:
        return Qt::SizeHorCursor;
    case CursorKind::SizeNS:
        return Qt::SizeVerCursor;
    case CursorKind::SizeNWSE:
        return Qt::SizeFDiagCursor;
    case CursorKind::SizeNESW:
        return Qt::SizeBDiagCursor;
    case CursorKind::SizeAll:
        return Qt::SizeAllCursor;
    case CursorKind::Arrow:
    case CursorKind::Custom:
    default:
        return Qt::ArrowCursor;
    }
}
}

QtRdpSessionWidget::QtRdpSessionWidget(Profile profile, QWidget *parent)
    : QWidget(parent),
      m_profile(std::move(profile)),
      m_process(std::make_shared<FreeRdpProcess>()),
      m_overlayText(stateText(FreeRdpProcess::State::Idle))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    bindProcessCallbacks();
}

QtRdpSessionWidget::~QtRdpSessionWidget()
{
    stopProcess();
}

void QtRdpSessionWidget::connectToHost()
{
    if (!m_process)
        return;

    m_frameGeneration = 0;
    m_frame = {};
    updateState(FreeRdpProcess::State::Starting);
    m_process->start(m_profile.host,
                     m_profile.port,
                     m_profile.username,
                     m_profile.password,
                     m_profile.domain,
                     std::max(width(), 640),
                     std::max(height(), 480),
                     m_profile.clipboardEnabled,
                     m_profile.ignoreCertificate);
}

void QtRdpSessionWidget::reconnect()
{
    stopProcess();
    m_process = std::make_shared<FreeRdpProcess>();
    bindProcessCallbacks();
    connectToHost();
}

bool QtRdpSessionWidget::isConnected() const
{
    return m_state == FreeRdpProcess::State::Running;
}

void QtRdpSessionWidget::setStateChangedCallback(std::function<void(FreeRdpProcess::State)> callback)
{
    m_stateChanged = std::move(callback);
}

void QtRdpSessionWidget::paintEvent(QPaintEvent *event)
{
    static_cast<void>(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 24, 39));

    if (!m_frame.empty()) {
        QImage image(m_frame.pixels.data(),
                     m_frame.width,
                     m_frame.height,
                     m_frame.stride,
                     QImage::Format_ARGB32);
        painter.drawImage(rect(), image);
    }

    if (!m_overlayText.isEmpty()) {
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(QColor(226, 232, 240));
        painter.drawText(rect(), Qt::AlignCenter, m_overlayText);
    }
}

void QtRdpSessionWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    requestResize();
}

void QtRdpSessionWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_process)
        m_process->sendMouseMove(pointFromMouseEvent(event), viewSize());
}

void QtRdpSessionWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    sendMouseButton(event, true);
}

void QtRdpSessionWidget::mouseReleaseEvent(QMouseEvent *event)
{
    sendMouseButton(event, false);
}

void QtRdpSessionWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_process)
        return;

    const QPoint position = event->position().toPoint();
    const QPoint delta = event->angleDelta();
    m_process->sendWheel(PointI{delta.x(), delta.y()},
                         PointI{position.x(), position.y()},
                         viewSize());
}

void QtRdpSessionWidget::keyPressEvent(QKeyEvent *event)
{
    sendKeyEvent(event, true);
}

void QtRdpSessionWidget::keyReleaseEvent(QKeyEvent *event)
{
    sendKeyEvent(event, false);
}

void QtRdpSessionWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    if (m_process)
        m_process->sendFocusIn();
}

void QtRdpSessionWidget::bindProcessCallbacks()
{
    if (!m_process)
        return;

    std::weak_ptr<FreeRdpProcess> weakProcess = m_process;
    m_process->setStateChangedCallback([this, weakProcess](FreeRdpProcess::State state) {
        QMetaObject::invokeMethod(this, [this, weakProcess, state]() {
            if (weakProcess.lock() != m_process)
                return;
            updateState(state);
        }, Qt::QueuedConnection);
    });
    m_process->setFrameUpdatedCallback([this, weakProcess]() {
        QMetaObject::invokeMethod(this, [this, weakProcess]() {
            if (weakProcess.lock() != m_process)
                return;
            consumeFrame();
        }, Qt::QueuedConnection);
    });
    m_process->setDesktopResizedCallback([this, weakProcess](const SizeI &) {
        QMetaObject::invokeMethod(this, [this, weakProcess]() {
            if (weakProcess.lock() != m_process)
                return;
            requestResize();
        }, Qt::QueuedConnection);
    });
    m_process->setCursorUpdatedCallback([this, weakProcess]() {
        QMetaObject::invokeMethod(this, [this, weakProcess]() {
            if (weakProcess.lock() != m_process)
                return;
            updateCursor();
        }, Qt::QueuedConnection);
    });
    m_process->setCertificateChallengeCallback([this, weakProcess](const FreeRdpProcess::CertificateChallenge &challenge) {
        bool accepted = false;
        QMetaObject::invokeMethod(this, [this, &accepted, challenge]() {
            accepted = confirmCertificate(challenge);
        }, Qt::BlockingQueuedConnection);
        if (auto process = weakProcess.lock())
            process->resolveCertificateChallenge(accepted);
    });
}

void QtRdpSessionWidget::clearProcessCallbacks()
{
    if (!m_process)
        return;

    m_process->setStateChangedCallback({});
    m_process->setFrameUpdatedCallback({});
    m_process->setDesktopResizedCallback({});
    m_process->setCursorUpdatedCallback({});
    m_process->setCertificateChallengeCallback({});
}

void QtRdpSessionWidget::stopProcess()
{
    if (!m_process)
        return;

    clearProcessCallbacks();
    m_process->stop();
}

void QtRdpSessionWidget::updateState(FreeRdpProcess::State state)
{
    m_state = state;
    m_overlayText = stateText(state);
    if (state == FreeRdpProcess::State::Finished && m_process) {
        const std::string error = m_process->lastDisconnectError();
        if (!error.empty())
            m_overlayText = QString::fromStdString(error);
    }
    if (m_stateChanged)
        m_stateChanged(state);
    update();
}

void QtRdpSessionWidget::consumeFrame()
{
    if (!m_process)
        return;

    if (m_process->consumeFrameIfNewer(m_frameGeneration, m_frame)) {
        if (m_state == FreeRdpProcess::State::Running)
            m_overlayText.clear();
        update();
    }
}

void QtRdpSessionWidget::updateCursor()
{
    if (!m_process)
        return;

    CursorInfo cursor = m_process->cursor();
    setCursor(cursorShapeFromKind(cursor.kind));
    destroyCursorInfo(cursor);
}

void QtRdpSessionWidget::requestResize()
{
    if (m_process && width() > 0 && height() > 0)
        m_process->requestResize(viewSize());
}

bool QtRdpSessionWidget::confirmCertificate(const FreeRdpProcess::CertificateChallenge &challenge)
{
    const QString text = tr("The remote certificate for %1:%2 could not be verified.")
        .arg(QString::fromStdWString(challenge.host))
        .arg(challenge.port);
    return QMessageBox::question(this,
                                  tr("Certificate"),
                                  text,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes;
}

SizeI QtRdpSessionWidget::viewSize() const
{
    return SizeI{std::max(width(), 1), std::max(height(), 1)};
}

PointI QtRdpSessionWidget::pointFromMouseEvent(const QMouseEvent *event) const
{
    if (!event)
        return {};

    const QPoint pos = event->pos();
    return PointI{pos.x(), pos.y()};
}

void QtRdpSessionWidget::sendMouseButton(QMouseEvent *event, bool down)
{
    if (!m_process || !event)
        return;

    const MouseButton button = mouseButtonFromQt(event->button());
    if (button == MouseButton::None)
        return;

    m_process->sendMouseButton(button, down, pointFromMouseEvent(event), viewSize());
}

void QtRdpSessionWidget::sendKeyEvent(QKeyEvent *event, bool down)
{
    if (!m_process || !event)
        return;

    const unsigned int virtualKey = static_cast<unsigned int>(event->nativeVirtualKey());
    const auto key = keyIdentifierFromVirtualKey(virtualKey);
    if (!key) {
        event->ignore();
        return;
    }

    m_process->sendKey(*key, down, event->isAutoRepeat());
    event->accept();
}
