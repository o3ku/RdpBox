#pragma once

#include <bitset>

// Pairs low-level capture decisions per key: a release is captured only if the
// matching press was. Deciding each half on its own leaves a modifier stuck
// down on whichever side (host or session) never saw its other half.
class RdpKeyboardHookState
{
public:
    bool consumeKey(unsigned int virtualKey, bool down, bool captureRequested, bool physicallyDown = false)
    {
        if (virtualKey >= m_pressed.size())
            return false;

        if (down) {
            // Auto-repeat keeps the decision the first press made.
            if (!m_pressed[virtualKey]) {
                m_pressed.set(virtualKey);
                // WH_KEYBOARD_LL runs before async state changes; already-down means a pre-hook repeat.
                m_captured.set(virtualKey, captureRequested && !physicallyDown);
            }
            return m_captured[virtualKey];
        }

        // No record means the press predates this hook: it went to Windows,
        // so its release has to go there too.
        const bool captured = m_pressed[virtualKey] && m_captured[virtualKey];
        m_pressed.reset(virtualKey);
        m_captured.reset(virtualKey);
        m_detached.reset(virtualKey);
        return captured;
    }

    bool hasCapturedKeys() const
    {
        return m_captured.any();
    }

    bool isDetached(unsigned int virtualKey) const
    {
        return virtualKey < m_detached.size() && m_detached[virtualKey];
    }

    void detachCapturedKeys()
    {
        m_detached |= m_captured;
    }

    void reset()
    {
        m_pressed.reset();
        m_captured.reset();
        m_detached.reset();
    }

private:
    std::bitset<256> m_pressed;
    std::bitset<256> m_captured;
    std::bitset<256> m_detached;
};
