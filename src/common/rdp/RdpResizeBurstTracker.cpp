#include "common/rdp/RdpResizeBurstTracker.h"

void RdpResizeBurstTracker::reset()
{
    m_lastRequestedSize = {};
    m_pendingSize = {};
    m_active = false;
}

bool RdpResizeBurstTracker::onResize(const SizeI &size)
{
    // Pure trailing debounce: never send on the leading edge — the caller
    // arms its timer on the first resize of a burst and sends from the tick,
    // so transient intermediate resolutions are never requested.
    if (size.width <= 0 || size.height <= 0) {
        m_active = false;
        m_pendingSize = {};
        return false;
    }

    if (m_active) {
        m_pendingSize = size;
        return false;
    }

    // Geometry the session was already asked for is not a new burst: layout
    // jitter (scrollbars, dock widgets re-laying out an unchanged pane) must
    // not re-enter the resolution-update state.
    if (size == m_lastRequestedSize)
        return false;

    m_active = true;
    m_pendingSize = size;
    return true;
}

bool RdpResizeBurstTracker::onTimeout(const SizeI &size, SizeI &requestedSize)
{
    if (!m_active)
        return false;

    m_active = false;
    const SizeI debounced = size.width > 0 && size.height > 0 ? size : m_pendingSize;
    m_pendingSize = {};
    if (debounced.width <= 0 || debounced.height <= 0 || debounced == m_lastRequestedSize)
        return false;

    m_lastRequestedSize = debounced;
    requestedSize = debounced;
    return true;
}
