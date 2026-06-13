#include "qt/QtRdpSessionWidget.h"

#include "rdp/FreeRdpProcessNative.h"
#include "rdp/RdpCursorClassifier.h"
#include "rdp/RdpInputEventUtil.h"
#include "rdp/RdpInputModifiers.h"

#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>

#include <windows.h>

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

bool isVirtualKeyPhysicallyDown(int virtualKey)
{
    return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

unsigned int physicalKeyboardModifiers()
{
    unsigned int modifiers = ModifierNone;
    if (isVirtualKeyPhysicallyDown(VK_CONTROL))
        modifiers |= ModifierControl;
    if (isVirtualKeyPhysicallyDown(VK_SHIFT))
        modifiers |= ModifierShift;
    if (isVirtualKeyPhysicallyDown(VK_MENU))
        modifiers |= ModifierAlt;
    if (isVirtualKeyPhysicallyDown(VK_LWIN) || isVirtualKeyPhysicallyDown(VK_RWIN))
        modifiers |= ModifierWin;
    return modifiers;
}

RdpKeyboardPhysicalState physicalKeyboardState()
{
    RdpKeyboardPhysicalState state;
    state.modifiers = physicalKeyboardModifiers();
    if (isVirtualKeyPhysicallyDown(VK_RCONTROL))
        state.controlVirtualKey = VK_RCONTROL;
    else if (isVirtualKeyPhysicallyDown(VK_LCONTROL))
        state.controlVirtualKey = VK_LCONTROL;
    if (isVirtualKeyPhysicallyDown(VK_RSHIFT))
        state.shiftVirtualKey = VK_RSHIFT;
    else if (isVirtualKeyPhysicallyDown(VK_LSHIFT))
        state.shiftVirtualKey = VK_LSHIFT;
    if (isVirtualKeyPhysicallyDown(VK_RMENU))
        state.altVirtualKey = VK_RMENU;
    else if (isVirtualKeyPhysicallyDown(VK_LMENU))
        state.altVirtualKey = VK_LMENU;
    if (isVirtualKeyPhysicallyDown(VK_RWIN))
        state.winVirtualKey = VK_RWIN;
    else if (isVirtualKeyPhysicallyDown(VK_LWIN))
        state.winVirtualKey = VK_LWIN;
    return state;
}

std::uint32_t keyMessageFromQtEvent(const QKeyEvent *event, bool down)
{
    const unsigned int virtualKey = static_cast<unsigned int>(event->nativeVirtualKey());
    const bool altContext = virtualKey == VK_MENU
        || virtualKey == VK_LMENU
        || virtualKey == VK_RMENU
        || (physicalKeyboardModifiers() & ModifierAlt) != 0;
    if (down)
        return altContext ? WM_SYSKEYDOWN : WM_KEYDOWN;
    return altContext ? WM_SYSKEYUP : WM_KEYUP;
}

unsigned int mouseButtonBit(MouseButton button)
{
    switch (button) {
    case MouseButton::Left:
        return 1u << 0;
    case MouseButton::Right:
        return 1u << 1;
    case MouseButton::Middle:
        return 1u << 2;
    default:
        return 0;
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

    m_keyboardRouter.reset();
    m_pressedMouseButtons = 0;
    m_hasLastPointerPoint = false;
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

FreeRdpProcess::State QtRdpSessionWidget::state() const
{
    return m_state;
}

FreeRdpProcess::ConnectionInfo QtRdpSessionWidget::connectionInfo() const
{
    return m_process ? m_process->connectionInfo() : FreeRdpProcess::ConnectionInfo{};
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
    m_lastPointerPoint = pointFromMouseEvent(event);
    m_hasLastPointerPoint = true;
    const unsigned int mouseFlags = mouseFlagsFromEvent(event);
    if (rdp::shouldSynchronizeModifiersForMouseMove(mouseFlags))
        syncMouseModifiers(mouseFlags);
    if (m_process)
        m_process->sendMouseMove(m_lastPointerPoint, viewSize());
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
    m_lastPointerPoint = PointI{position.x(), position.y()};
    m_hasLastPointerPoint = true;
    unsigned int mouseFlags = ModifierNone;
    if ((event->modifiers() & Qt::ControlModifier) != 0)
        mouseFlags |= MK_CONTROL;
    if ((event->modifiers() & Qt::ShiftModifier) != 0)
        mouseFlags |= MK_SHIFT;
    syncMouseModifiers(mouseFlags);
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
    m_keyboardRouter.onFocusGained();
    if (m_process)
        m_process->sendFocusIn();
}

void QtRdpSessionWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    releasePressedMouseButtons();
    sendKeyboardActions(m_keyboardRouter.handleFocusLost(physicalKeyboardState()));
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

    releasePressedMouseButtons();
    releaseAllPressedKeys();
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

unsigned int QtRdpSessionWidget::mouseFlagsFromEvent(const QMouseEvent *event) const
{
    if (!event)
        return 0;

    unsigned int flags = 0;
    if ((event->modifiers() & Qt::ControlModifier) != 0)
        flags |= MK_CONTROL;
    if ((event->modifiers() & Qt::ShiftModifier) != 0)
        flags |= MK_SHIFT;
    if ((event->buttons() & Qt::LeftButton) != 0)
        flags |= MK_LBUTTON;
    if ((event->buttons() & Qt::RightButton) != 0)
        flags |= MK_RBUTTON;
    if ((event->buttons() & Qt::MiddleButton) != 0)
        flags |= MK_MBUTTON;
    if ((event->buttons() & Qt::BackButton) != 0)
        flags |= MK_XBUTTON1;
    if ((event->buttons() & Qt::ForwardButton) != 0)
        flags |= MK_XBUTTON2;
    return flags;
}

void QtRdpSessionWidget::syncMouseModifiers(unsigned int mouseFlags)
{
    if (!m_process)
        return;

    sendKeyboardActions(m_keyboardRouter.synchronizeMouseModifiers(
        mouseFlags,
        physicalKeyboardState(),
        hasFocus()));
}

void QtRdpSessionWidget::sendMouseButton(QMouseEvent *event, bool down)
{
    if (!m_process || !event)
        return;

    const MouseButton button = mouseButtonFromQt(event->button());
    if (button == MouseButton::None)
        return;

    m_lastPointerPoint = pointFromMouseEvent(event);
    m_hasLastPointerPoint = true;
    syncMouseModifiers(mouseFlagsFromEvent(event));
    m_process->sendMouseButton(button, down, m_lastPointerPoint, viewSize());

    const unsigned int bit = mouseButtonBit(button);
    if (bit == 0)
        return;

    if (down)
        m_pressedMouseButtons |= bit;
    else
        m_pressedMouseButtons &= ~bit;

    if (m_pressedMouseButtons != 0)
        grabMouse();
    else
        releaseMouse();
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

    const std::uint32_t message = keyMessageFromQtEvent(event, down);
    const std::intptr_t lParam = makeKeyLParam(*key, down, event->isAutoRepeat());
    const auto nativeEvent = keyEventInfoFromMessage(message, virtualKey, lParam);
    if (!nativeEvent) {
        event->ignore();
        return;
    }

    sendKeyboardActions(m_keyboardRouter.handleKeyMessage(message,
                                                          virtualKey,
                                                          *nativeEvent,
                                                          physicalKeyboardState(),
                                                          hasFocus()));
    event->accept();
}

void QtRdpSessionWidget::sendKeyboardAction(const RdpKeyboardInputRouter::KeyAction &action)
{
    if (!m_process)
        return;

    m_process->sendKey(action.key, action.down, action.wasDown);
}

void QtRdpSessionWidget::sendKeyboardActions(
    const std::vector<RdpKeyboardInputRouter::KeyAction> &actions)
{
    for (const auto &action : actions)
        sendKeyboardAction(action);
}

void QtRdpSessionWidget::releasePressedMouseButtons()
{
    const unsigned int pressedButtons = m_pressedMouseButtons;
    m_pressedMouseButtons = 0;
    releaseMouse();

    if (pressedButtons == 0)
        return;

    if (!m_process || m_process->state() != FreeRdpProcess::State::Running)
        return;

    const PointI point = m_hasLastPointerPoint ? m_lastPointerPoint : PointI{};
    if ((pressedButtons & (1u << 0)) != 0)
        m_process->sendMouseButton(MouseButton::Left, false, point, viewSize());
    if ((pressedButtons & (1u << 1)) != 0)
        m_process->sendMouseButton(MouseButton::Right, false, point, viewSize());
    if ((pressedButtons & (1u << 2)) != 0)
        m_process->sendMouseButton(MouseButton::Middle, false, point, viewSize());
}

void QtRdpSessionWidget::releaseAllPressedKeys()
{
    if (!m_process || m_process->state() != FreeRdpProcess::State::Running) {
        m_keyboardRouter.reset();
        return;
    }

    sendKeyboardActions(m_keyboardRouter.releaseAllPressedKeys());
}
