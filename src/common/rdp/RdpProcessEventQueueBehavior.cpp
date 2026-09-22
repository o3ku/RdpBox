#include "RdpProcessEventQueueBehavior.h"

namespace rdp::process_event_queue
{
bool postStateChanged(EventTarget &target, FreeRdpProcess::State state, std::uintptr_t generation)
{
    if (!target.canPost())
        return false;

    target.postStateChanged(state, generation);
    return true;
}

bool postFrameUpdated(EventTarget &target, std::uintptr_t generation)
{
    if (!target.canPost())
        return false;

    target.postFrameUpdated(generation);
    return true;
}

bool postCursorUpdated(EventTarget &target, std::uintptr_t generation)
{
    if (!target.canPost())
        return false;

    target.postCursorUpdated(generation);
    return true;
}

bool postCertificateRequest(EventTarget &target, std::uintptr_t generation)
{
    if (!target.canPost())
        return false;

    target.postCertificateRequest(generation);
    return true;
}

void storePendingCertificateRequest(std::optional<PendingCertificateRequest> &pendingRequest,
                                    std::uintptr_t generation,
                                    const FreeRdpProcess::CertificateChallenge &challenge)
{
    pendingRequest = PendingCertificateRequest{ generation, challenge };
}
}
