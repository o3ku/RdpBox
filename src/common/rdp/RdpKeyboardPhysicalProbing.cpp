#include "common/rdp/RdpKeyboardPhysicalProbing.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{

// Internal: aggregate-only view of the modifiers. Not exposed - callers that
// need left/right detail go through physicalKeyboardState().
unsigned int physicalKeyboardModifiers()
{
    unsigned int modifiers = ModifierNone;
    if (rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_CONTROL))
        modifiers |= ModifierControl;
    if (rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_SHIFT))
        modifiers |= ModifierShift;
    if (rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_MENU))
        modifiers |= ModifierAlt;
    if (rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_LWIN)
        || rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_RWIN))
        modifiers |= ModifierWin;
    return modifiers;
}

} // namespace

namespace rdp::physical_keyboard
{

bool isVirtualKeyPhysicallyDown(unsigned int virtualKey)
{
#ifdef _WIN32
    return (::GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
#else
    static_cast<void>(virtualKey);
    return false;
#endif
}

RdpKeyboardPhysicalState physicalKeyboardState()
{
    RdpKeyboardPhysicalState state;
    state.modifiers = physicalKeyboardModifiers();
    if (isVirtualKeyPhysicallyDown(VK_RCONTROL))
        state.controlVirtualKey = VK_RCONTROL;
    else if (isVirtualKeyPhysicallyDown(VK_LCONTROL))
        state.controlVirtualKey = VK_LCONTROL;
    if (isVirtualKeyPhysicallyDown(VK_RSHIFT))
        state.shiftVirtualKey = VK_RSHIFT;
    else if (isVirtualKeyPhysicallyDown(VK_LSHIFT))
        state.shiftVirtualKey = VK_LSHIFT;
    if (isVirtualKeyPhysicallyDown(VK_RMENU))
        state.altVirtualKey = VK_RMENU;
    else if (isVirtualKeyPhysicallyDown(VK_LMENU))
        state.altVirtualKey = VK_LMENU;
    if (isVirtualKeyPhysicallyDown(VK_RWIN))
        state.winVirtualKey = VK_RWIN;
    else if (isVirtualKeyPhysicallyDown(VK_LWIN))
        state.winVirtualKey = VK_LWIN;
    return state;
}

} // namespace rdp::physical_keyboard
