#include <cassert>

#include "rdp/RdpResizeBurstTracker.h"

int main()
{
    RdpResizeBurstTracker tracker;

    assert(!tracker.onTimeout(SizeI{1280, 720}));
    assert(tracker.onResize(SizeI{1280, 720}));
    assert(!tracker.onResize(SizeI{1366, 768}));
    assert(tracker.onTimeout(SizeI{1366, 768}));
    assert(!tracker.onTimeout(SizeI{1366, 768}));
    assert(tracker.onResize(SizeI{1366, 768}));

    tracker.reset();
    assert(!tracker.onTimeout(SizeI{1600, 900}));
    assert(tracker.onResize(SizeI{1600, 900}));
    assert(!tracker.onTimeout(SizeI{1600, 900}));
    assert(tracker.onResize(SizeI{1920, 1080}));

    return 0;
}
