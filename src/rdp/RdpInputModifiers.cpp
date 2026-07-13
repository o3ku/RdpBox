#include "rdp/RdpInputModifiers.h"

#include "common/NativeTypes.h"

namespace rdp
{
bool shouldSynchronizeModifiersForMouseMove(UINT mouseFlags)
{
    return (mouseFlags & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) != 0;
}

unsigned int keyboardModifierMaskForVirtualKey(unsigned int virtualKey)
{
    switch (virtualKey) {
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_CONTROL:
        return ModifierControl;
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_SHIFT:
        return ModifierShift;
    case VK_LMENU:
    case VK_RMENU:
    case VK_MENU:
        return ModifierAlt;
    case VK_LWIN:
    case VK_RWIN:
        return ModifierWin;
    default:
        return ModifierNone;
    }
}

bool isKeyboardModifierVirtualKey(unsigned int virtualKey)
{
    return keyboardModifierMaskForVirtualKey(virtualKey) != ModifierNone;
}

bool isToggleModifierVirtualKey(unsigned int virtualKey)
{
    switch (virtualKey) {
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
    case VK_KANA:
        return true;
    default:
        return false;
    }
}

bool shouldCaptureTabForSystemChord(unsigned int virtualKey,
                                    unsigned int lowLevelFlags,
                                    unsigned int keyboardModifiers)
{
    if (virtualKey != VK_TAB)
        return false;

    return (lowLevelFlags & LLKHF_ALTDOWN) != 0
        || (keyboardModifiers & ModifierAlt) != 0;
}

unsigned int mouseInputModifiers(UINT mouseFlags, unsigned int keyboardModifiers)
{
    unsigned int modifiers = keyboardModifiers;
    if ((mouseFlags & MK_CONTROL) != 0)
        modifiers |= ModifierControl;
    if ((mouseFlags & MK_SHIFT) != 0)
        modifiers |= ModifierShift;
    return modifiers;
}

unsigned int keyboardInputModifiersForKeyMessage(std::uint32_t message,
                                                 unsigned int virtualKey,
                                                 unsigned int keyboardModifiers)
{
    unsigned int modifiers = keyboardModifiers;
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
    const bool sysContext = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;

    if (!down && !up)
        return modifiers;

    // WM_SYSKEY* for a non-Alt key means Alt is part of the active chord,
    // even if GetKeyState/GetAsyncKeyState is transiently out of sync.
    if (down
        && sysContext
        && virtualKey != VK_LMENU
        && virtualKey != VK_RMENU
        && virtualKey != VK_MENU) {
        modifiers |= ModifierAlt;
    }

    const unsigned int mask = keyboardModifierMaskForVirtualKey(virtualKey);
    if (mask == ModifierNone)
        return modifiers;

    if (down)
        modifiers |= mask;
    else
        modifiers &= ~mask;

    return modifiers;
}
}
