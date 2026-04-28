#include "rdp/RdpMouseMoveCoalescer.h"

bool RdpMouseMoveCoalescer::sameAsLastSent(PointI point) const
{
    return point.x == m_lastSent.x && point.y == m_lastSent.y;
}

std::optional<PointI> RdpMouseMoveCoalescer::onMouseMove(PointI point)
{
    if (!m_active) {
        m_active = true;
        m_lastSent = point;
        m_hasPending = false;
        return point;
    }

    m_pending = point;
    m_hasPending = true;
    return std::nullopt;
}

std::optional<PointI> RdpMouseMoveCoalescer::onTimer()
{
    if (!m_active)
        return std::nullopt;

    if (!m_hasPending || sameAsLastSent(m_pending)) {
        m_active = false;
        m_hasPending = false;
        return std::nullopt;
    }

    m_lastSent = m_pending;
    m_hasPending = false;
    return m_lastSent;
}

std::optional<PointI> RdpMouseMoveCoalescer::flush()
{
    if (!m_active)
        return std::nullopt;

    if (!m_hasPending || sameAsLastSent(m_pending)) {
        m_active = false;
        m_hasPending = false;
        return std::nullopt;
    }

    m_lastSent = m_pending;
    m_hasPending = false;
    m_active = false;
    return m_lastSent;
}

void RdpMouseMoveCoalescer::reset()
{
    m_active = false;
    m_hasPending = false;
    m_lastSent = {};
    m_pending = {};
}
