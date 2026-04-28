#include "rdp/RdpModifierSyncTracker.h"

#include "common/NativeTypes.h"

#include <windows.h>

void RdpModifierSyncTracker::reset()
{
    m_controlDown = false;
    m_shiftDown = false;
    m_altDown = false;
}

void RdpModifierSyncTracker::recordKeyState(unsigned int virtualKey, bool down)
{
    switch (virtualKey) {
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_CONTROL:
        m_controlDown = down;
        break;
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_SHIFT:
        m_shiftDown = down;
        break;
    case VK_LMENU:
    case VK_RMENU:
    case VK_MENU:
        m_altDown = down;
        break;
    default:
        break;
    }
}

void RdpModifierSyncTracker::syncModifier(std::vector<KeyAction> &actions,
                                          bool &currentDown,
                                          bool desiredDown,
                                          std::uint32_t keyDownMessage,
                                          std::uint32_t keyUpMessage,
                                          std::uint32_t virtualKey)
{
    if (currentDown == desiredDown)
        return;

    currentDown = desiredDown;
    actions.push_back(KeyAction{
        desiredDown ? keyDownMessage : keyUpMessage,
        virtualKey
    });
}

std::vector<RdpModifierSyncTracker::KeyAction>
RdpModifierSyncTracker::synchronize(unsigned int modifiers)
{
    std::vector<KeyAction> actions;

    const bool controlDown = (modifiers & ModifierControl) != 0;
    const bool shiftDown = (modifiers & ModifierShift) != 0;
    const bool altDown = (modifiers & ModifierAlt) != 0;

    syncModifier(actions, m_controlDown, controlDown, WM_KEYDOWN, WM_KEYUP, VK_CONTROL);
    syncModifier(actions, m_shiftDown, shiftDown, WM_KEYDOWN, WM_KEYUP, VK_SHIFT);
    syncModifier(actions, m_altDown, altDown, WM_SYSKEYDOWN, WM_SYSKEYUP, VK_MENU);

    return actions;
}
