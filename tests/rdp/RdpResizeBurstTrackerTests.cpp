#include <cassert>

#include "rdp/RdpResizeBurstTracker.h"

int main()
{
    RdpResizeBurstTracker tracker;

    assert(tracker.onResize(SizeI{1280, 720}));
    assert(!tracker.onResize(SizeI{1366, 768}));
    assert(tracker.onTimeout(SizeI{1366, 768}));
    assert(!tracker.onTimeout(SizeI{1366, 768}));

    return 0;
}
