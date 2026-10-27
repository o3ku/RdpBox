#pragma once

#include <chrono>

// How long stop() waits for a worker to let go of its session, and what it says
// about having waited. An attempt that never got a handshake through is the
// common case (a refused connection, a server that is off): it is released
// quietly in two seconds, because giving up on it is not news and the overlay
// already carries what freerdp said. A session that did come up and still will
// not let go is news, and the only thing on this path a user can act on.
namespace rdp::stop_grace
{
enum class Plan
{
    Starting,  // the attempt published a state but never got a handshake through
    Connected, // the attempt got a handshake through, or published a frame
    Failed,    // the attempt never said anything at all
};

inline std::chrono::seconds kQuietGrace{2};
inline std::chrono::seconds kStopGrace{20};

inline bool quiet(Plan plan)
{
    return plan != Plan::Connected;
}

inline std::chrono::seconds pollBudget(Plan plan)
{
    return quiet(plan) ? kQuietGrace : kStopGrace;
}

inline bool report(Plan plan)
{
    return plan == Plan::Connected;
}

inline Plan plan(bool publishedAnyState, bool handshakeCompleted)
{
    if (handshakeCompleted)
        return Plan::Connected;
    return publishedAnyState ? Plan::Starting : Plan::Failed;
}
} // namespace rdp::stop_grace
