#include "rdp/RdpSessionMouseState.h"

void RdpSessionMouseState::reset()
{
    m_pressedButtons = 0;
    m_lastPointerPoint = {};
    m_hasLastPointerPoint = false;
    m_moveCoalescer.reset();
    m_moveTimerActive = false;
}

void RdpSessionMouseState::notePointerPosition(PointI point)
{
    m_lastPointerPoint = point;
    m_hasLastPointerPoint = true;
}

PointI RdpSessionMouseState::currentPointerPosition() const
{
    return m_hasLastPointerPoint ? m_lastPointerPoint : PointI{};
}

std::optional<PointI> RdpSessionMouseState::onPointerMove(PointI point)
{
    notePointerPosition(point);
    return m_moveCoalescer.onMouseMove(point);
}

std::optional<PointI> RdpSessionMouseState::flushPendingPointerMove()
{
    return m_moveCoalescer.flush();
}

std::optional<PointI> RdpSessionMouseState::onPointerMoveTimer()
{
    return m_moveCoalescer.onTimer();
}

bool RdpSessionMouseState::pointerMoveTimerActive() const
{
    return m_moveTimerActive;
}

void RdpSessionMouseState::setPointerMoveTimerActive(bool active)
{
    m_moveTimerActive = active;
}

void RdpSessionMouseState::noteButton(MouseButton button, bool down)
{
    const unsigned int mask = buttonMask(button);
    if (mask == 0)
        return;

    if (down)
        m_pressedButtons |= mask;
    else
        m_pressedButtons &= ~mask;
}

unsigned int RdpSessionMouseState::pressedButtons() const
{
    return m_pressedButtons;
}

void RdpSessionMouseState::clearPressedButtons()
{
    m_pressedButtons = 0;
}

unsigned int RdpSessionMouseState::buttonMask(MouseButton button)
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
