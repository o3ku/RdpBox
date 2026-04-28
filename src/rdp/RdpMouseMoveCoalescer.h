#pragma once

#include "common/NativeTypes.h"

#include <optional>

class RdpMouseMoveCoalescer
{
public:
    std::optional<PointI> onMouseMove(PointI point);
    std::optional<PointI> onTimer();
    std::optional<PointI> flush();
    void reset();

private:
    bool sameAsLastSent(PointI point) const;

    bool m_active = false;
    bool m_hasPending = false;
    PointI m_lastSent{};
    PointI m_pending{};
};
