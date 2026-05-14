#include "rdp/RdpInputModifiers.h"

#include "common/NativeTypes.h"

namespace rdp
{
unsigned int mouseInputModifiers(UINT mouseFlags, unsigned int keyboardModifiers)
{
    unsigned int modifiers = keyboardModifiers & ModifierAlt;
    if ((mouseFlags & MK_CONTROL) != 0)
        modifiers |= ModifierControl;
    if ((mouseFlags & MK_SHIFT) != 0)
        modifiers |= ModifierShift;
    return modifiers;
}
}
