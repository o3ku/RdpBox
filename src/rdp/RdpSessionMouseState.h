#pragma once

#include "common/NativeTypes.h"
#include "rdp/RdpMouseMoveCoalescer.h"

#include <optional>

class RdpSessionMouseState
{
public:
    void reset();
    void notePointerPosition(PointI point);
    PointI currentPointerPosition() const;

    std::optional<PointI> onPointerMove(PointI point);
    std::optional<PointI> flushPendingPointerMove();
    std::optional<PointI> onPointerMoveTimer();
    bool pointerMoveTimerActive() const;
    void setPointerMoveTimerActive(bool active);

    void noteButton(MouseButton button, bool down);
    unsigned int pressedButtons() const;
    void clearPressedButtons();

private:
    static unsigned int buttonMask(MouseButton button);

    unsigned int m_pressedButtons = 0;
    PointI m_lastPointerPoint{};
    bool m_hasLastPointerPoint = false;
    RdpMouseMoveCoalescer m_moveCoalescer;
    bool m_moveTimerActive = false;
};
