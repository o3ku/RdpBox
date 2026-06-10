#pragma once

#include <cstdint>

namespace rdp::process_event
{
bool isCurrentGeneration(std::uintptr_t currentGeneration, std::uintptr_t eventGeneration);

bool shouldHandleFrameEvent(bool visible,
                            std::uintptr_t currentGeneration,
                            std::uintptr_t eventGeneration);

bool shouldHandleProcessEvent(std::uintptr_t currentGeneration,
                              std::uintptr_t eventGeneration);

bool shouldClearPendingCertificateRequest(bool hasPendingRequest,
                                          std::uintptr_t pendingGeneration,
                                          std::uintptr_t eventGeneration);

bool shouldShowCertificatePrompt(std::uintptr_t currentGeneration,
                                 std::uintptr_t eventGeneration,
                                 bool hasBinding,
                                 bool hasProcess,
                                 bool hasPendingRequest,
                                 std::uintptr_t pendingGeneration);
}
