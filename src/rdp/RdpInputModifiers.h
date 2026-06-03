#pragma once

#include <cstdint>
#include <windows.h>

namespace rdp
{
unsigned int mouseInputModifiers(UINT mouseFlags, unsigned int keyboardModifiers);
bool shouldSynchronizeModifiersForMouseMove(UINT mouseFlags);
unsigned int keyboardModifierMaskForVirtualKey(unsigned int virtualKey);
bool isKeyboardModifierVirtualKey(unsigned int virtualKey);
bool isToggleModifierVirtualKey(unsigned int virtualKey);
bool shouldDeferKeyReleaseOnFocusLoss(unsigned int keyboardModifiers);
bool shouldCaptureTabForSystemChord(unsigned int virtualKey,
                                    unsigned int lowLevelFlags,
                                    unsigned int keyboardModifiers);
unsigned int keyboardInputModifiersForKeyMessage(std::uint32_t message,
                                                 unsigned int virtualKey,
                                                 unsigned int keyboardModifiers);
}
