#pragma once

#include "common/NativeTypes.h"
#include "common/rdp/RdpWinKeyCodes.h"

#include <vector>

// Physical (async) keyboard snapshot: aggregate modifiers plus the concrete
// left/right virtual key for each modifier family. Leaf header so
// RdpKeyboardInputRouter and RdpKeyboardPhysicalProbing can share it without
// pulling each other in.
struct RdpKeyboardPhysicalState
{
    unsigned int modifiers = ModifierNone;
    unsigned int controlVirtualKey = VK_CONTROL;
    unsigned int shiftVirtualKey = VK_SHIFT;
    unsigned int altVirtualKey = VK_MENU;
    unsigned int winVirtualKey = VK_LWIN;
    bool knowsNonModifierKeys = false;
    std::vector<unsigned int> pressedVirtualKeys;
};
