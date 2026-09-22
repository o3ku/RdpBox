#include "common/rdp/RdpResizeBurstTracker.h"

void RdpResizeBurstTracker::reset()
{
    m_lastSentSize = {};
    m_active = false;
}

bool RdpResizeBurstTracker::onResize(const SizeI &size)
{
    (void)size;
    // Pure trailing debounce: never send on the leading edge — the caller
    // arms its timer on the first resize of a burst and sends from the tick,
    // so transient intermediate resolutions are never requested.
    const bool wasInactive = !m_active;
    m_active = true;
    return wasInactive;
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
