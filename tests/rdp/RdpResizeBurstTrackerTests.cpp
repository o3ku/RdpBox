#include <cassert>

#include "common/rdp/RdpResizeBurstTracker.h"

int main()
{
    RdpResizeBurstTracker tracker;

    // Trailing debounce: onResize only arms; onTimeout sends once settled.
    assert(!tracker.onTimeout(SizeI{1280, 720}));
    assert(tracker.onResize(SizeI{1280, 720}));            // burst start -> arm
    assert(!tracker.onResize(SizeI{1366, 768}));           // mid-burst
    assert(tracker.onTimeout(SizeI{1366, 768}));           // tick: sends latest
    assert(!tracker.onTimeout(SizeI{1366, 768}));          // settled -> done
    assert(tracker.onResize(SizeI{1366, 768}));            // new burst
    assert(!tracker.onTimeout(SizeI{1366, 768}));          // same size -> no send

    tracker.reset();
    assert(!tracker.onTimeout(SizeI{1600, 900}));
    assert(tracker.onResize(SizeI{1600, 900}));
    assert(tracker.onTimeout(SizeI{1600, 900}));
    assert(!tracker.onTimeout(SizeI{1600, 900}));
    assert(tracker.onResize(SizeI{1920, 1080}));
    assert(tracker.onTimeout(SizeI{1920, 1080}));          // changed -> send

    return 0;
}
