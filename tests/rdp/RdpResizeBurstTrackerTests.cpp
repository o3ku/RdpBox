#include <cassert>

#include "common/rdp/RdpResizeBurstTracker.h"

int main()
{
    RdpResizeBurstTracker tracker;

    // Trailing debounce: onResize only arms; onTimeout sends once settled.
    SizeI requested{};
    assert(!tracker.onTimeout(SizeI{1280, 720}, requested));
    assert(tracker.onResize(SizeI{1280, 720}));            // burst start -> arm
    assert(!tracker.onResize(SizeI{1366, 768}));           // mid-burst
    requested = {};
    assert(tracker.onTimeout(SizeI{1366, 768}, requested)); // tick: sends latest
    assert(requested.width == 1366 && requested.height == 768);
    assert(!tracker.onTimeout(SizeI{1366, 768}, requested)); // settled -> done

    // Geometry the session already has is not a new burst: layout jitter on an
    // unchanged pane must not re-enter the resolution-update state.
    assert(!tracker.onResize(SizeI{1366, 768}));
    assert(!tracker.onTimeout(SizeI{1366, 768}, requested));

    // A fresh tracker (reconnect) has not asked for anything yet.
    tracker.reset();
    assert(!tracker.onTimeout(SizeI{1600, 900}, requested));
    assert(tracker.onResize(SizeI{1600, 900}));
    requested = {};
    assert(tracker.onTimeout(SizeI{1600, 900}, requested));
    assert(requested.width == 1600 && requested.height == 900);
    assert(!tracker.onTimeout(SizeI{1600, 900}, requested));
    assert(!tracker.onResize(SizeI{1600, 900}));
    assert(tracker.onResize(SizeI{1920, 1080}));           // changed -> arm
    requested = {};
    assert(tracker.onTimeout(SizeI{1920, 1080}, requested)); // changed -> send
    assert(requested.width == 1920 && requested.height == 1080);

    // Degenerate sizes never arm or send, and they cancel a burst in flight.
    tracker.reset();
    assert(!tracker.onResize(SizeI{0, 0}));
    assert(!tracker.onResize(SizeI{1280, 0}));
    assert(!tracker.onTimeout(SizeI{0, 0}, requested));

    // A burst interrupted by a degenerate size settles on the last valid one.
    tracker.reset();
    assert(tracker.onResize(SizeI{1440, 900}));
    assert(!tracker.onResize(SizeI{0, 0}));
    assert(!tracker.onTimeout(SizeI{0, 0}, requested));
    assert(tracker.onResize(SizeI{1440, 900}));
    requested = {};
    assert(tracker.onTimeout(SizeI{1440, 900}, requested));
    assert(requested.width == 1440 && requested.height == 900);

    return 0;
}
