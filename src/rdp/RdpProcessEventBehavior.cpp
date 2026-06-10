#include "RdpProcessEventBehavior.h"

namespace rdp::process_event
{
bool isCurrentGeneration(std::uintptr_t currentGeneration, std::uintptr_t eventGeneration)
{
    return currentGeneration == eventGeneration;
}

bool shouldHandleFrameEvent(bool visible,
                            std::uintptr_t currentGeneration,
                            std::uintptr_t eventGeneration)
{
    return visible && isCurrentGeneration(currentGeneration, eventGeneration);
}

bool shouldHandleProcessEvent(std::uintptr_t currentGeneration,
                              std::uintptr_t eventGeneration)
{
    return isCurrentGeneration(currentGeneration, eventGeneration);
}

bool shouldClearPendingCertificateRequest(bool hasPendingRequest,
                                          std::uintptr_t pendingGeneration,
                                          std::uintptr_t eventGeneration)
{
    return hasPendingRequest && pendingGeneration == eventGeneration;
}

bool shouldShowCertificatePrompt(std::uintptr_t currentGeneration,
                                 std::uintptr_t eventGeneration,
                                 bool hasBinding,
                                 bool hasProcess,
                                 bool hasPendingRequest,
                                 std::uintptr_t pendingGeneration)
{
    return isCurrentGeneration(currentGeneration, eventGeneration)
        && hasBinding
        && hasProcess
        && hasPendingRequest
        && pendingGeneration == eventGeneration;
}
}
