#pragma once

#include <cstdint>
#include <optional>

struct KeyIdentifier
{
    std::uint16_t scanCode = 0;
    bool extended = false;

    friend bool operator==(const KeyIdentifier &, const KeyIdentifier &) = default;
};

struct KeyEventInfo
{
    KeyIdentifier key;
    bool down = false;
    bool wasDown = false;
};

#ifdef _WIN32
std::optional<KeyIdentifier> keyIdentifierFromVirtualKey(unsigned int virtualKey);
std::optional<KeyEventInfo> keyEventInfoFromMessage(std::uint32_t message,
                                                    std::uintptr_t wParam,
                                                    std::intptr_t lParam);
std::intptr_t makeKeyLParam(const KeyIdentifier &key, bool down, bool wasDown = false);
#else
// Non-Windows builds feed scan codes directly (Qt nativeScanCode); the
// virtual-key/message helpers have no platform meaning there.
inline std::optional<KeyIdentifier> keyIdentifierFromVirtualKey(unsigned int) { return std::nullopt; }
inline std::optional<KeyEventInfo> keyEventInfoFromMessage(std::uint32_t, std::uintptr_t, std::intptr_t) { return std::nullopt; }
inline std::intptr_t makeKeyLParam(const KeyIdentifier &, bool, bool = false) { return 0; }
#endif
