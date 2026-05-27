#include <cassert>

#include "rdp/RdpReservedShortcutTracker.h"

int main()
{
    RdpReservedShortcutTracker tracker;

    tracker.noteHandledKeyDown('P');
    assert(!tracker.consumeHandledKeyUp('O'));
    assert(tracker.consumeHandledKeyUp('P'));
    assert(!tracker.consumeHandledKeyUp('P'));

    tracker.noteHandledKeyDown('P');
    tracker.noteHandledKeyDown('P');
    assert(tracker.consumeHandledKeyUp('P'));
    assert(!tracker.consumeHandledKeyUp('P'));

    tracker.noteHandledKeyDown('N');
    tracker.reset();
    assert(!tracker.consumeHandledKeyUp('N'));

    tracker.noteHandledKeyDown('A');
    tracker.noteHandledKeyDown('B');
    assert(tracker.consumeHandledKeyUp('A'));
    assert(tracker.consumeHandledKeyUp('B'));
    assert(!tracker.consumeHandledKeyUp('A'));

    return 0;
}
