#pragma once

#include <cstdint>
#include <windows.h>

namespace rdp
{
unsigned int mouseInputModifiers(UINT mouseFlags, unsigned int keyboardModifiers);
bool isKeyboardModifierVirtualKey(unsigned int virtualKey);
bool isToggleModifierVirtualKey(unsigned int virtualKey);
unsigned int keyboardInputModifiersForKeyMessage(std::uint32_t message,
                                                 unsigned int virtualKey,
                                                 unsigned int keyboardModifiers);
}
