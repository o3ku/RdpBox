#include <windows.h>

#include "common/NativeTypes.h"
#include "rdp/RdpInputModifiers.h"

int main()
{
    using rdp::mouseInputModifiers;
    using rdp::shouldSynchronizeModifiersForMouseMove;
    using rdp::isKeyboardModifierVirtualKey;
    using rdp::isToggleModifierVirtualKey;
    using rdp::keyboardModifierMaskForVirtualKey;
    using rdp::shouldCaptureTabForSystemChord;
    using rdp::shouldDeferKeyReleaseOnFocusLoss;
    using rdp::keyboardInputModifiersForKeyMessage;

    auto applyKeyMessage = [](unsigned int &modifiers, UINT message, unsigned int virtualKey) {
        modifiers = keyboardInputModifiersForKeyMessage(message, virtualKey, modifiers);
        return modifiers;
    };

    if (mouseInputModifiers(MK_CONTROL, ModifierNone) != ModifierControl)
        return 1;
    if (mouseInputModifiers(MK_SHIFT, ModifierNone) != ModifierShift)
        return 1;
    if (mouseInputModifiers(MK_CONTROL | MK_SHIFT, ModifierAlt | ModifierWin)
        != (ModifierControl | ModifierShift | ModifierAlt | ModifierWin)) {
        return 1;
    }
    if (mouseInputModifiers(0, ModifierControl | ModifierShift | ModifierAlt | ModifierWin)
        != (ModifierControl | ModifierShift | ModifierAlt | ModifierWin)) {
        return 1;
    }
    if (shouldSynchronizeModifiersForMouseMove(0))
        return 1;
    if (shouldSynchronizeModifiersForMouseMove(MK_CONTROL | MK_SHIFT))
        return 1;
    if (!shouldSynchronizeModifiersForMouseMove(MK_LBUTTON))
        return 1;
    if (!shouldSynchronizeModifiersForMouseMove(MK_RBUTTON | MK_CONTROL))
        return 1;
    if (!shouldSynchronizeModifiersForMouseMove(MK_XBUTTON1))
        return 1;
    if (!isKeyboardModifierVirtualKey(VK_MENU))
        return 1;
    if (!isKeyboardModifierVirtualKey(VK_LWIN))
        return 1;
    if (!isKeyboardModifierVirtualKey(VK_LSHIFT))
        return 1;
    if (!isKeyboardModifierVirtualKey(VK_RCONTROL))
        return 1;
    if (isKeyboardModifierVirtualKey('2'))
        return 1;
    if (keyboardModifierMaskForVirtualKey(VK_LCONTROL) != ModifierControl)
        return 1;
    if (keyboardModifierMaskForVirtualKey(VK_RSHIFT) != ModifierShift)
        return 1;
    if (keyboardModifierMaskForVirtualKey(VK_RMENU) != ModifierAlt)
        return 1;
    if (keyboardModifierMaskForVirtualKey(VK_RWIN) != ModifierWin)
        return 1;
    if (keyboardModifierMaskForVirtualKey('2') != ModifierNone)
        return 1;
    if (!isToggleModifierVirtualKey(VK_CAPITAL))
        return 1;
    if (isToggleModifierVirtualKey(VK_MENU))
        return 1;
    if (!shouldDeferKeyReleaseOnFocusLoss(ModifierAlt))
        return 1;
    if (!shouldDeferKeyReleaseOnFocusLoss(ModifierWin))
        return 1;
    if (shouldDeferKeyReleaseOnFocusLoss(ModifierControl | ModifierShift))
        return 1;
    if (!shouldCaptureTabForSystemChord(VK_TAB, LLKHF_ALTDOWN, ModifierNone))
        return 1;
    if (!shouldCaptureTabForSystemChord(VK_TAB, 0, ModifierAlt))
        return 1;
    if (shouldCaptureTabForSystemChord(VK_TAB, 0, ModifierNone))
        return 1;
    if (shouldCaptureTabForSystemChord('A', LLKHF_ALTDOWN, ModifierAlt))
        return 1;
    if (keyboardInputModifiersForKeyMessage(WM_SYSKEYDOWN, VK_MENU, ModifierNone) != ModifierAlt)
        return 1;
    if (keyboardInputModifiersForKeyMessage(WM_SYSKEYUP, VK_MENU, ModifierAlt) != ModifierNone)
        return 1;
    if (keyboardInputModifiersForKeyMessage(WM_SYSKEYDOWN, '1', ModifierNone) != ModifierAlt)
        return 1;
    if (keyboardInputModifiersForKeyMessage(WM_SYSKEYUP, '1', ModifierNone) != ModifierAlt)
        return 1;
    if (keyboardInputModifiersForKeyMessage(WM_KEYDOWN, VK_LWIN, ModifierAlt)
        != (ModifierAlt | ModifierWin)) {
        return 1;
    }
    if (keyboardInputModifiersForKeyMessage(WM_KEYUP, VK_RWIN, ModifierAlt | ModifierWin)
        != ModifierAlt) {
        return 1;
    }
    if (keyboardInputModifiersForKeyMessage(WM_KEYDOWN, VK_LCONTROL, ModifierShift)
        != (ModifierShift | ModifierControl)) {
        return 1;
    }
    if (keyboardInputModifiersForKeyMessage(WM_KEYUP, VK_RSHIFT, ModifierControl | ModifierShift)
        != ModifierControl) {
        return 1;
    }
    if (keyboardInputModifiersForKeyMessage(WM_MOUSEMOVE, VK_MENU, ModifierAlt) != ModifierAlt)
        return 1;
    if (keyboardInputModifiersForKeyMessage(WM_KEYDOWN, '2', ModifierAlt) != ModifierAlt)
        return 1;

    {
        unsigned int modifiers = ModifierNone;
        if (applyKeyMessage(modifiers, WM_SYSKEYDOWN, VK_MENU) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_KEYDOWN, '1', modifiers) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_KEYUP, '1', modifiers) != ModifierAlt)
            return 1;
        if (applyKeyMessage(modifiers, WM_SYSKEYUP, VK_MENU) != ModifierNone)
            return 1;
    }

    {
        unsigned int modifiers = ModifierNone;
        if (applyKeyMessage(modifiers, WM_SYSKEYDOWN, VK_MENU) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_KEYDOWN, '1', modifiers) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_KEYUP, '1', modifiers) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_KEYDOWN, '2', modifiers) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_KEYUP, '2', modifiers) != ModifierAlt)
            return 1;
        if (applyKeyMessage(modifiers, WM_SYSKEYUP, VK_MENU) != ModifierNone)
            return 1;
    }

    {
        unsigned int modifiers = ModifierNone;
        if (applyKeyMessage(modifiers, WM_SYSKEYDOWN, VK_MENU) != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_SYSKEYDOWN, VK_TAB, modifiers) != ModifierAlt)
            return 1;
        if (modifiers != ModifierAlt)
            return 1;
        if (keyboardInputModifiersForKeyMessage(WM_SYSKEYUP, VK_TAB, modifiers) != ModifierAlt)
            return 1;
        if (modifiers != ModifierAlt)
            return 1;
        if (applyKeyMessage(modifiers, WM_SYSKEYUP, VK_MENU) != ModifierNone)
            return 1;
    }

    return 0;
}
