#include <cassert>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
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

    return 0;
}
