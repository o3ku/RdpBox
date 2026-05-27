#pragma once

#include <cstddef>
#include <cstdint>

namespace rdp::trace
{
void resetSystemChordTrace();
bool shouldTraceSystemChordVirtualKey(unsigned int virtualKey);
void logSystemChordEvent(const wchar_t *stage,
                         unsigned int virtualKey,
                         std::uint32_t message,
                         std::intptr_t lParam,
                         unsigned int flags,
                         unsigned int keyboardModifiers,
                         bool hasFocus,
                         bool captureWithoutFocus,
                         std::size_t pressedKeyCount);
void logSystemChordNote(const wchar_t *stage,
                        unsigned int keyboardModifiers,
                        bool hasFocus,
                        bool captureWithoutFocus,
                        std::size_t pressedKeyCount);
void logSystemChordSyncAction(const wchar_t *stage,
                              unsigned int sourceVirtualKey,
                              unsigned int actionVirtualKey,
                              bool down,
                              unsigned int desiredModifiers,
                              unsigned int keyboardModifiers,
                              bool hasFocus,
                              bool captureWithoutFocus,
                              std::size_t pressedKeyCount);
}
