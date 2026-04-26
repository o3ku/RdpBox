#pragma once

#include <cstdint>
#include <vector>

class RdpModifierSyncTracker
{
public:
    struct KeyAction
    {
        std::uint32_t message = 0;
        std::uint32_t virtualKey = 0;
    };

    void reset();
    void recordKeyState(unsigned int virtualKey, bool down);
    std::vector<KeyAction> synchronize(unsigned int modifiers);

private:
    void syncModifier(std::vector<KeyAction> &actions,
                      bool &currentDown,
                      bool desiredDown,
                      std::uint32_t keyDownMessage,
                      std::uint32_t keyUpMessage,
                      std::uint32_t virtualKey);

    bool m_controlDown = false;
    bool m_shiftDown = false;
    bool m_altDown = false;
};
