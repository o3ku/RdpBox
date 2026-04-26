#include "rdp/RdpResizeBurstTracker.h"

void RdpResizeBurstTracker::reset()
{
    m_lastSentSize = {};
    m_active = false;
}

bool RdpResizeBurstTracker::onResize(const SizeI &size)
{
    if (m_active)
        return false;

    m_active = true;
    m_lastSentSize = size;
    return true;
}

bool RdpResizeBurstTracker::onTimeout(const SizeI &size)
{
    if (!m_active)
        return false;

    if (size == m_lastSentSize) {
        m_active = false;
        return false;
    }

    m_lastSentSize = size;
    return true;
}
