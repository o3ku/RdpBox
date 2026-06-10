#pragma once

#include "rdp/FreeRdpProcess.h"

#include <cstdint>
#include <optional>

namespace rdp::process_event_queue
{
struct PendingCertificateRequest
{
    std::uintptr_t generation = 0;
    FreeRdpProcess::CertificateChallenge challenge;
};

class EventTarget
{
public:
    virtual ~EventTarget() = default;

    virtual bool canPost() const = 0;
    virtual void postStateChanged(FreeRdpProcess::State state, std::uintptr_t generation) = 0;
    virtual void postFrameUpdated(std::uintptr_t generation) = 0;
    virtual void postCursorUpdated(std::uintptr_t generation) = 0;
    virtual void postCertificateRequest(std::uintptr_t generation) = 0;
};

bool postStateChanged(EventTarget &target, FreeRdpProcess::State state, std::uintptr_t generation);
bool postFrameUpdated(EventTarget &target, std::uintptr_t generation);
bool postCursorUpdated(EventTarget &target, std::uintptr_t generation);
bool postCertificateRequest(EventTarget &target, std::uintptr_t generation);

void storePendingCertificateRequest(std::optional<PendingCertificateRequest> &pendingRequest,
                                    std::uintptr_t generation,
                                    const FreeRdpProcess::CertificateChallenge &challenge);
}
