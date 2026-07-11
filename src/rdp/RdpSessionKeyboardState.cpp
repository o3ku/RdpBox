#include "rdp/RdpSessionKeyboardState.h"

void RdpSessionKeyboardState::reset()
{
    m_keyboardRouter.reset();
    m_reservedShortcutTracker.reset();
}

void RdpSessionKeyboardState::onFocusGained()
{
    m_keyboardRouter.onFocusGained();
}

unsigned int RdpSessionKeyboardState::activeKeyboardModifiers() const
{
    return m_keyboardRouter.activeKeyboardModifiers();
}

bool RdpSessionKeyboardState::captureSystemKeysWithoutFocus() const
{
    return m_keyboardRouter.captureSystemKeysWithoutFocus();
}

std::size_t RdpSessionKeyboardState::pressedKeyCount() const
{
    return m_keyboardRouter.pressedKeyCount();
}

bool RdpSessionKeyboardState::shouldCaptureLowLevelKey(
    const RdpLowLevelKeyEvent &event,
    const RdpKeyboardPhysicalState &physical) const
{
    return m_keyboardRouter.shouldCaptureLowLevelKey(event, physical);
}

std::uint32_t RdpSessionKeyboardState::messageForLowLevelKey(
    const RdpLowLevelKeyEvent &event,
    const RdpKeyboardPhysicalState &physical) const
{
    return m_keyboardRouter.messageForLowLevelKey(event, physical);
}

std::vector<RdpSessionKeyboardState::KeyAction>
RdpSessionKeyboardState::handleKeyMessage(std::uint32_t message,
                                          unsigned int virtualKey,
                                          const KeyEventInfo &event,
                                          const RdpKeyboardPhysicalState &physical,
                                          bool hasWindowFocus)
{
    return m_keyboardRouter.handleKeyMessage(message, virtualKey, event, physical, hasWindowFocus);
}

std::vector<RdpSessionKeyboardState::KeyAction>
RdpSessionKeyboardState::synchronizeMouseModifiers(UINT mouseFlags,
                                                   const RdpKeyboardPhysicalState &physical,
                                                   bool hasWindowFocus)
{
    return m_keyboardRouter.synchronizeMouseModifiers(mouseFlags, physical, hasWindowFocus);
}

std::vector<RdpSessionKeyboardState::KeyAction>
RdpSessionKeyboardState::handleFocusLost(const RdpKeyboardPhysicalState &physical)
{
    m_reservedShortcutTracker.reset();
    return m_keyboardRouter.handleFocusLost(physical);
}

std::vector<RdpSessionKeyboardState::KeyAction>
RdpSessionKeyboardState::releaseAllPressedKeys()
{
    m_reservedShortcutTracker.reset();
    return m_keyboardRouter.releaseAllPressedKeys();
}

void RdpSessionKeyboardState::noteConsumedLocalShortcutKey(unsigned int virtualKey)
{
    m_reservedShortcutTracker.noteHandledKeyDown(virtualKey);
}

bool RdpSessionKeyboardState::consumeReservedShortcutKey(unsigned int virtualKey)
{
    return m_reservedShortcutTracker.consumeHandledKeyUp(virtualKey);
}
