#include <cassert>

#include <windows.h>

#include "rdp/RdpInputEventUtil.h"

int main()
{
    {
        const KeyIdentifier key{ 0x1D, true };
        const std::intptr_t lParam = makeKeyLParam(key, false, true);
        const auto event = keyEventInfoFromMessage(WM_KEYUP, VK_RCONTROL, lParam);
        assert(event.has_value());
        assert(event->key == key);
        assert(!event->down);
        assert(event->wasDown);
    }

    {
        const auto key = keyIdentifierFromVirtualKey(VK_DELETE);
        assert(key.has_value());
        assert(key->extended);
        assert(key->scanCode != 0);
    }

    {
        const auto event = keyEventInfoFromMessage(WM_SYSKEYDOWN, VK_MENU, 0);
        assert(event.has_value());
        assert(event->down);
        assert(event->key.scanCode != 0);
    }

    return 0;
}
