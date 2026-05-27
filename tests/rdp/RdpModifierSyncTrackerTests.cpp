#include <cassert>

#include <windows.h>

#include "common/NativeTypes.h"
#include "rdp/RdpModifierSyncTracker.h"

int main()
{
    RdpModifierSyncTracker tracker;

    {
        const std::vector<RdpModifierSyncTracker::KeyAction> actions =
            tracker.synchronize(ModifierControl);
        assert(actions.size() == 1);
        assert(actions[0].message == static_cast<std::uint32_t>(WM_KEYDOWN));
        assert(actions[0].virtualKey == static_cast<std::uint32_t>(VK_CONTROL));
        assert(tracker.synchronize(ModifierControl).empty());
    }

    tracker.reset();
    tracker.recordKeyState(VK_SHIFT, true);

    {
        const std::vector<RdpModifierSyncTracker::KeyAction> actions =
            tracker.synchronize(ModifierNone);
        assert(actions.size() == 1);
        assert(actions[0].message == static_cast<std::uint32_t>(WM_KEYUP));
        assert(actions[0].virtualKey == static_cast<std::uint32_t>(VK_SHIFT));
    }

    tracker.reset();
    tracker.recordKeyState(VK_LCONTROL, true);
    tracker.recordKeyState(VK_RMENU, true);
    tracker.recordKeyState(VK_LWIN, true);

    {
        const std::vector<RdpModifierSyncTracker::KeyAction> actions =
            tracker.synchronize(ModifierShift);
        assert(actions.size() == 4);
        assert(actions[0].message == static_cast<std::uint32_t>(WM_KEYUP));
        assert(actions[0].virtualKey == static_cast<std::uint32_t>(VK_CONTROL));
        assert(actions[1].message == static_cast<std::uint32_t>(WM_KEYDOWN));
        assert(actions[1].virtualKey == static_cast<std::uint32_t>(VK_SHIFT));
        assert(actions[2].message == static_cast<std::uint32_t>(WM_SYSKEYUP));
        assert(actions[2].virtualKey == static_cast<std::uint32_t>(VK_MENU));
        assert(actions[3].message == static_cast<std::uint32_t>(WM_KEYUP));
        assert(actions[3].virtualKey == static_cast<std::uint32_t>(VK_LWIN));
        assert(tracker.synchronize(ModifierShift).empty());
    }

    tracker.reset();

    {
        const std::vector<RdpModifierSyncTracker::KeyAction> actions =
            tracker.synchronize(ModifierControl | ModifierShift | ModifierAlt | ModifierWin);
        assert(actions.size() == 4);
        assert(actions[0].message == static_cast<std::uint32_t>(WM_KEYDOWN));
        assert(actions[0].virtualKey == static_cast<std::uint32_t>(VK_CONTROL));
        assert(actions[1].message == static_cast<std::uint32_t>(WM_KEYDOWN));
        assert(actions[1].virtualKey == static_cast<std::uint32_t>(VK_SHIFT));
        assert(actions[2].message == static_cast<std::uint32_t>(WM_SYSKEYDOWN));
        assert(actions[2].virtualKey == static_cast<std::uint32_t>(VK_MENU));
        assert(actions[3].message == static_cast<std::uint32_t>(WM_KEYDOWN));
        assert(actions[3].virtualKey == static_cast<std::uint32_t>(VK_LWIN));
    }

    return 0;
}
