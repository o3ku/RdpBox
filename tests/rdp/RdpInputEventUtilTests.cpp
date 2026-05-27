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

    {
        const auto event = keyEventInfoFromMessage(WM_MOUSEMOVE, 0, 0);
        assert(!event.has_value());
    }

    {
        const auto key = keyIdentifierFromVirtualKey(0);
        assert(!key.has_value());
    }

    {
        const KeyIdentifier key{ 0x2A, false };
        const std::intptr_t lParam = makeKeyLParam(key, true, false);
        assert((static_cast<std::uint32_t>(lParam) & 0x80000000u) == 0);
        assert((static_cast<std::uint32_t>(lParam) & 0x40000000u) == 0);
        assert((static_cast<std::uint32_t>(lParam) & 0x01000000u) == 0);
        assert(((static_cast<std::uint32_t>(lParam) >> 16) & 0xFFu) == 0x2A);
    }

    {
        const KeyIdentifier key{ 0x53, true };
        const std::intptr_t lParam = makeKeyLParam(key, false, false);
        assert((static_cast<std::uint32_t>(lParam) & 0x80000000u) != 0);
        assert((static_cast<std::uint32_t>(lParam) & 0x01000000u) != 0);

        const auto event = keyEventInfoFromMessage(WM_KEYUP, VK_DELETE, lParam);
        assert(event.has_value());
        assert(event->key == key);
        assert(!event->down);
        assert(!event->wasDown);
    }

    return 0;
}
