#pragma once

#include "common/rdp/RdpSessionViewBehavior.h"

#include <cstdint>
#include <utility>

namespace rdp::session_view
{
// Caller owns both buffers and serializes access to the pending frame.
inline bool consumeFrame(FrameBuffer &pending,
                         std::uint64_t generation,
                         std::uint64_t &lastSeenGeneration,
                         FrameBuffer &displayed,
                         FrameGateState &gate,
                         FrameArrivalDecision &decision)
{
    if (generation == lastSeenGeneration || pending.empty())
        return false;

    lastSeenGeneration = generation;
    decision = frameArrivalDecision(gate);
    gate = decision.state;
    if (decision.renderFrame)
        std::swap(displayed, pending);
    return true;
}
}
