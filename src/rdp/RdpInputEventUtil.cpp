#include "rdp/RdpInputEventUtil.h"

#include <windows.h>

namespace
{
bool isKeyDownMessage(std::uint32_t message)
{
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool isKeyUpMessage(std::uint32_t message)
{
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

bool isExtendedVirtualKey(unsigned int virtualKey)
{
    switch (virtualKey) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_CANCEL:
    case VK_SNAPSHOT:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
        return true;
    default:
        return false;
    }
}
}

std::optional<KeyIdentifier> keyIdentifierFromVirtualKey(unsigned int virtualKey)
{
    const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    if (scanCode == 0)
        return std::nullopt;

    return KeyIdentifier{
        static_cast<std::uint16_t>(scanCode & 0xFFu),
        isExtendedVirtualKey(virtualKey)
    };
}

std::optional<KeyEventInfo> keyEventInfoFromMessage(std::uint32_t message,
                                                    std::uintptr_t wParam,
                                                    std::intptr_t lParam)
{
    if (!isKeyDownMessage(message) && !isKeyUpMessage(message))
        return std::nullopt;

    KeyIdentifier key;
    key.scanCode = static_cast<std::uint16_t>((static_cast<std::uint32_t>(lParam) >> 16) & 0xFFu);
    key.extended = (static_cast<std::uint32_t>(lParam) & 0x01000000u) != 0;
    if (key.scanCode == 0) {
        const auto mapped = keyIdentifierFromVirtualKey(static_cast<unsigned int>(wParam));
        if (!mapped)
            return std::nullopt;
        key = *mapped;
    }

    return KeyEventInfo{
        key,
        isKeyDownMessage(message),
        (static_cast<std::uint32_t>(lParam) & 0x40000000u) != 0
    };
}

std::intptr_t makeKeyLParam(const KeyIdentifier &key, bool down, bool wasDown)
{
    std::intptr_t value = static_cast<std::intptr_t>(1)
        | (static_cast<std::intptr_t>(key.scanCode & 0xFFu) << 16);
    if (key.extended)
        value |= 0x01000000;
    if (wasDown)
        value |= 0x40000000;
    if (!down)
        value |= static_cast<std::intptr_t>(0x80000000u);
    return value;
}
