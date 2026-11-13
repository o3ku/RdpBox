#undef NDEBUG
#include <cassert>
#include <algorithm>

#include "common/rdp/FrameBufferMemory.h"
#include "common/rdp/RdpFrameDelivery.h"

FrameBuffer frame(int width, int height, std::uint8_t value)
{
    FrameBuffer result;
    FrameBufferMemory::resize(result, width, height, width * 4,
                              static_cast<std::size_t>(width) * height * 4);
    std::fill(result.pixels.begin(), result.pixels.end(), value);
    return result;
}

int main()
{
    using namespace rdp::session_view;
    FrameBuffer displayed = frame(8, 4, 1);
    FrameBuffer pending;
    std::uint64_t seen = 0;
    FrameGateState gate{true, 3, true, true};
    FrameArrivalDecision decision;
    assert(!consumeFrame(pending, 1, seen, displayed, gate, decision));
    assert(seen == 0 && gate.remaining == 3);

    pending = frame(16, 8, 2);
    auto* displayPixels = displayed.pixels.data();
    auto* pendingPixels = pending.pixels.data();
    assert(consumeFrame(pending, 1, seen, displayed, gate, decision));
    assert(!decision.renderFrame && gate.remaining == 2);
    assert(displayed.pixels.data() == displayPixels && displayed.pixels.front() == 1);
    assert(pending.pixels.data() == pendingPixels);
    assert(!consumeFrame(pending, 1, seen, displayed, gate, decision));
    assert(gate.remaining == 2);
    assert(consumeFrame(pending, 2, seen, displayed, gate, decision));
    assert(!decision.renderFrame && gate.remaining == 1);
    assert(consumeFrame(pending, 3, seen, displayed, gate, decision));
    assert(decision.renderFrame && decision.hideOverlay && decision.resolutionFrameProgress);
    assert(!gate.active && !gate.waitingForFirstContentFrame);
    assert(displayed.pixels.data() == pendingPixels && displayed.width == 16);
    assert(pending.pixels.data() == displayPixels && pending.width == 8);

    // Steady state exchanges the same two allocations, including idle repaints.
    FrameBufferMemory::resize(pending, 16, 8, 64, 512);
    displayPixels = displayed.pixels.data();
    pendingPixels = pending.pixels.data();
    assert(consumeFrame(pending, 4, seen, displayed, gate, decision));
    assert(displayed.pixels.data() == pendingPixels);
    assert(pending.pixels.data() == displayPixels);
    assert(!consumeFrame(pending, 4, seen, displayed, gate, decision));
    FrameBufferMemory::release(pending);
    assert(pending.pixels.capacity() == 0 && !displayed.empty());
    assert(!consumeFrame(pending, 5, seen, displayed, gate, decision));
}
