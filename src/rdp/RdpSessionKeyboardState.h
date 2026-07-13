#pragma once

#include "rdp/RdpKeyboardInputRouter.h"
#include "rdp/RdpReservedShortcutTracker.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class RdpSessionKeyboardState
{
public:
    using KeyAction = RdpKeyboardInputRouter::KeyAction;

    void reset();

    unsigned int activeKeyboardModifiers() const;
    std::size_t pressedKeyCount() const;

    bool shouldCaptureLowLevelKey(const RdpLowLevelKeyEvent &event,
                                  const RdpKeyboardPhysicalState &physical) const;
    std::uint32_t messageForLowLevelKey(const RdpLowLevelKeyEvent &event,
                                        const RdpKeyboardPhysicalState &physical) const;

    std::vector<KeyAction> handleKeyMessage(std::uint32_t message,
                                            unsigned int virtualKey,
                                            const KeyEventInfo &event,
                                            const RdpKeyboardPhysicalState &physical,
                                            bool hasWindowFocus);
    std::vector<KeyAction> synchronizeMouseModifiers(UINT mouseFlags,
                                                     const RdpKeyboardPhysicalState &physical,
                                                     bool hasWindowFocus);
    std::vector<KeyAction> releaseAllPressedKeys();

    void noteConsumedLocalShortcutKey(unsigned int virtualKey);
    bool consumeReservedShortcutKey(unsigned int virtualKey);

private:
    RdpKeyboardInputRouter m_keyboardRouter;
    RdpReservedShortcutTracker m_reservedShortcutTracker;
};
