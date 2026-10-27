#pragma once

#include "common/NativeTypes.h"

class RdpResizeBurstTracker
{
public:
    void reset();

    // Returns true when the caller should arm its trailing-debounce timer.
    // A size already asked for is not a new burst: the tiniest layout jitter
    // must not restart the debounce.
    bool onResize(const SizeI &size);

    // The debounced size to send, when this tick ends the burst with a size
    // the session does not have yet. Returns false once the burst settles on
    // geometry that was already requested, so the caller never re-enters its
    // "resolution is updating" state for a size the remote desktop already has.
    bool onTimeout(const SizeI &size, SizeI &requestedSize);

private:
    SizeI m_lastRequestedSize;
    SizeI m_pendingSize;
    bool m_active = false;
};
