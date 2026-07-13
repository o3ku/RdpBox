#include "rdp/RdpKeyboardInputRouter.h"

#include "rdp/RdpInputModifiers.h"

#include <algorithm>

namespace
{
constexpr unsigned int kAllKeyboardModifiers =
    ModifierControl | ModifierShift | ModifierAlt | ModifierWin;
constexpr unsigned int kSystemKeyModifiers = ModifierAlt | ModifierWin;
constexpr unsigned int kKeyboardModifierMasks[] = {
    ModifierControl,
    ModifierShift,
    ModifierAlt,
    ModifierWin
};

bool isAltKey(unsigned int virtualKey)
{
    return virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU;
}

bool isSystemKey(unsigned int virtualKey)
{
    return virtualKey == VK_LWIN || virtualKey == VK_RWIN;
}

bool isCtrlEscapeSequence(unsigned int virtualKey, const RdpKeyboardPhysicalState &physical)
{
    return virtualKey == VK_ESCAPE && (physical.modifiers & ModifierControl) != 0;
}

bool hasActiveLowLevelAltContext(const RdpLowLevelKeyEvent &event,
                                 const RdpKeyboardPhysicalState &physical)
{
    return (event.flags & LLKHF_ALTDOWN) != 0
        || (physical.modifiers & ModifierAlt) != 0;
}

bool hasActiveLowLevelWinContext(const RdpKeyboardPhysicalState &physical)
{
    return (physical.modifiers & ModifierWin) != 0;
}

bool isVirtualKeyPhysicallyDown(unsigned int virtualKey, const RdpKeyboardPhysicalState &physical)
{
    const unsigned int modifier = rdp::keyboardModifierMaskForVirtualKey(virtualKey);
    if (modifier != ModifierNone)
        return (physical.modifiers & modifier) != 0;

    if (!physical.knowsNonModifierKeys)
        return true;

    return std::find(physical.pressedVirtualKeys.begin(),
                     physical.pressedVirtualKeys.end(),
                     virtualKey)
        != physical.pressedVirtualKeys.end();
}

unsigned int virtualKeyForModifierMask(unsigned int modifier)
{
    switch (modifier) {
    case ModifierControl:
        return VK_CONTROL;
    case ModifierShift:
        return VK_SHIFT;
    case ModifierAlt:
        return VK_MENU;
    case ModifierWin:
        return VK_LWIN;
    default:
        return 0;
    }
}

unsigned int virtualKeyForKeyIdentifier(const KeyIdentifier &key)
{
    UINT scanCode = key.scanCode & 0xFFu;
    if (key.extended)
        scanCode |= 0xE000u;

    return MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
}

unsigned int modifierMaskForKeyIdentifier(const KeyIdentifier &key)
{
    return rdp::keyboardModifierMaskForVirtualKey(virtualKeyForKeyIdentifier(key));
}
}

void RdpKeyboardInputRouter::reset()
{
    m_modifierTracker.reset();
    m_pressedKeys.clear();
    m_lowLevelReleaseKeys.clear();
    m_keyboardModifiers = ModifierNone;
    m_passThroughModifierReleases = ModifierNone;
}

unsigned int RdpKeyboardInputRouter::activeKeyboardModifiers() const
{
    return m_keyboardModifiers;
}

std::size_t RdpKeyboardInputRouter::pressedKeyCount() const
{
    return m_pressedKeys.size();
}

bool RdpKeyboardInputRouter::shouldCaptureLowLevelKey(
    const RdpLowLevelKeyEvent &event,
    const RdpKeyboardPhysicalState &physical) const
{
    const unsigned int activeKnownModifiers = m_keyboardModifiers | physical.modifiers;
    const unsigned int eventModifier = rdp::keyboardModifierMaskForVirtualKey(event.virtualKey);
    const bool passThroughModifierRelease =
        event.keyUp
        && eventModifier != ModifierNone
        && (m_passThroughModifierReleases & eventModifier) != 0
        && isVirtualKeyPhysicallyDown(event.virtualKey, physical);
    const bool captureTrackedSystemModifierRelease =
        event.keyUp
        && (eventModifier & kSystemKeyModifiers) != 0
        && (m_keyboardModifiers & eventModifier) != 0;
    bool captureTrackedKeyRelease = false;
    if (event.keyUp) {
        const auto lowLevelKey = keyIdentifierFromVirtualKey(event.virtualKey);
        captureTrackedKeyRelease = lowLevelKey && hasLowLevelReleaseKey(*lowLevelKey);
    }

    if (event.reservedShortcut)
        return false;
    if (passThroughModifierRelease && !captureTrackedSystemModifierRelease)
        return false;
    if (!event.hasWindowFocus)
        return false;

    const bool altChord =
        hasActiveLowLevelAltContext(event, physical) || (m_keyboardModifiers & ModifierAlt) != 0;
    const bool winChord =
        hasActiveLowLevelWinContext(physical) || (m_keyboardModifiers & ModifierWin) != 0;

    return captureTrackedSystemModifierRelease
        || captureTrackedKeyRelease
        || isSystemKey(event.virtualKey)
        || isAltKey(event.virtualKey)
        || altChord
        || winChord
        || isCtrlEscapeSequence(event.virtualKey, physical)
        || rdp::shouldCaptureTabForSystemChord(event.virtualKey,
                                               event.flags,
                                               activeKnownModifiers);
}

std::uint32_t RdpKeyboardInputRouter::messageForLowLevelKey(
    const RdpLowLevelKeyEvent &event,
    const RdpKeyboardPhysicalState &physical) const
{
    const bool sysContext = isAltKey(event.virtualKey)
        || hasActiveLowLevelAltContext(event, physical)
        || (m_keyboardModifiers & ModifierAlt) != 0;
    if (event.keyUp)
        return sysContext ? WM_SYSKEYUP : WM_KEYUP;
    return sysContext ? WM_SYSKEYDOWN : WM_KEYDOWN;
}

std::vector<RdpKeyboardInputRouter::KeyAction> RdpKeyboardInputRouter::handleKeyMessage(
    std::uint32_t message,
    unsigned int virtualKey,
    const KeyEventInfo &event,
    const RdpKeyboardPhysicalState &physical,
    bool hasWindowFocus)
{
    std::vector<KeyAction> actions;

    if (!rdp::isKeyboardModifierVirtualKey(virtualKey)) {
        unsigned int messageModifiers =
            rdp::keyboardInputModifiersForKeyMessage(message, virtualKey, ModifierNone);
        messageModifiers |= m_keyboardModifiers;
        synchronizeKeyboardModifiersToPhysicalState(actions,
                                                    true,
                                                    messageModifiers,
                                                    &event.key,
                                                    physical);
    }

    appendKeyAction(actions, event.key, event.down, event.wasDown, virtualKey, false);
    trackLowLevelReleaseKey(event.key,
                            event.down && shouldTrackLowLevelRelease(message, virtualKey, physical));
    m_modifierTracker.recordKeyState(virtualKey, event.down);
    recordModifierState(message, virtualKey);

    return actions;
}

std::vector<RdpKeyboardInputRouter::KeyAction> RdpKeyboardInputRouter::synchronizeMouseModifiers(
    UINT mouseFlags,
    const RdpKeyboardPhysicalState &physical,
    bool hasWindowFocus)
{
    std::vector<KeyAction> actions;
    const unsigned int activeSystemModifiers = m_keyboardModifiers & (ModifierAlt | ModifierWin);
    synchronizeKeyboardModifiersToPhysicalState(actions, true, activeSystemModifiers, nullptr, physical);

    const unsigned int desiredModifiers =
        rdp::mouseInputModifiers(mouseFlags, m_keyboardModifiers) | activeSystemModifiers;
    const std::vector<RdpModifierSyncTracker::KeyAction> syncActions =
        m_modifierTracker.synchronize(desiredModifiers);
    for (const auto &action : syncActions) {
        appendSynchronizedModifier(actions,
                                   action.virtualKey,
                                   action.message == static_cast<std::uint32_t>(WM_KEYDOWN)
                                       || action.message == static_cast<std::uint32_t>(WM_SYSKEYDOWN),
                                   physical);
    }
    m_keyboardModifiers = desiredModifiers;
    return actions;
}

std::vector<RdpKeyboardInputRouter::KeyAction> RdpKeyboardInputRouter::releaseAllPressedKeys()
{
    std::vector<KeyAction> actions;
    m_modifierTracker.reset();
    m_keyboardModifiers = ModifierNone;
    m_passThroughModifierReleases = ModifierNone;

    const std::vector<KeyIdentifier> pressedKeys = m_pressedKeys;
    for (const auto &key : pressedKeys)
        appendKeyAction(actions, key, false, true, virtualKeyForKeyIdentifier(key), false);

    m_pressedKeys.clear();
    m_lowLevelReleaseKeys.clear();
    return actions;
}

bool RdpKeyboardInputRouter::hasTrackedKey(const KeyIdentifier &key) const
{
    return std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key) != m_pressedKeys.end();
}

bool RdpKeyboardInputRouter::hasLowLevelReleaseKey(const KeyIdentifier &key) const
{
    return std::find(m_lowLevelReleaseKeys.begin(), m_lowLevelReleaseKeys.end(), key)
        != m_lowLevelReleaseKeys.end();
}

void RdpKeyboardInputRouter::appendKeyAction(std::vector<KeyAction> &actions,
                                             const KeyIdentifier &key,
                                             bool down,
                                             bool wasDown,
                                             unsigned int virtualKey,
                                             bool synchronizedModifier)
{
    actions.push_back(KeyAction{
        key,
        down,
        wasDown,
        virtualKey,
        synchronizedModifier
    });
    trackKeyState(key, down);
}

void RdpKeyboardInputRouter::trackLowLevelReleaseKey(const KeyIdentifier &key, bool down)
{
    const auto it = std::find(m_lowLevelReleaseKeys.begin(), m_lowLevelReleaseKeys.end(), key);
    if (down) {
        if (it == m_lowLevelReleaseKeys.end())
            m_lowLevelReleaseKeys.push_back(key);
        return;
    }

    if (it != m_lowLevelReleaseKeys.end())
        m_lowLevelReleaseKeys.erase(it);
}

void RdpKeyboardInputRouter::trackPassThroughModifierRelease(unsigned int modifier, bool enabled)
{
    if ((modifier & kAllKeyboardModifiers) == 0)
        return;

    if (enabled)
        m_passThroughModifierReleases |= modifier;
    else
        m_passThroughModifierReleases &= ~modifier;
}

bool RdpKeyboardInputRouter::shouldTrackLowLevelRelease(
    std::uint32_t message,
    unsigned int virtualKey,
    const RdpKeyboardPhysicalState &physical) const
{
    if (rdp::isKeyboardModifierVirtualKey(virtualKey))
        return false;

    const unsigned int messageModifiers =
        rdp::keyboardInputModifiersForKeyMessage(message, virtualKey, ModifierNone);
    const unsigned int activeModifiers = m_keyboardModifiers | physical.modifiers | messageModifiers;
    if ((activeModifiers & (ModifierAlt | ModifierWin)) != 0)
        return true;

    return virtualKey == VK_ESCAPE && (activeModifiers & ModifierControl) != 0;
}

void RdpKeyboardInputRouter::trackKeyState(const KeyIdentifier &key, bool down)
{
    const auto it = std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key);
    if (down) {
        if (it == m_pressedKeys.end())
            m_pressedKeys.push_back(key);
        return;
    }

    if (it != m_pressedKeys.end())
        m_pressedKeys.erase(it);
    trackLowLevelReleaseKey(key, false);
    trackPassThroughModifierRelease(modifierMaskForKeyIdentifier(key), false);
}

void RdpKeyboardInputRouter::recordModifierState(std::uint32_t message, unsigned int virtualKey)
{
    if (!rdp::isKeyboardModifierVirtualKey(virtualKey))
        return;

    m_keyboardModifiers = rdp::keyboardInputModifiersForKeyMessage(message, virtualKey, m_keyboardModifiers);
}

unsigned int RdpKeyboardInputRouter::preferredVirtualKeyForModifier(
    unsigned int modifier,
    const RdpKeyboardPhysicalState &physical) const
{
    switch (modifier) {
    case ModifierControl:
        return physical.controlVirtualKey;
    case ModifierShift:
        return physical.shiftVirtualKey;
    case ModifierAlt:
        return physical.altVirtualKey;
    case ModifierWin:
        return physical.winVirtualKey;
    default:
        return 0;
    }
}

void RdpKeyboardInputRouter::appendSynchronizedModifier(
    std::vector<KeyAction> &actions,
    unsigned int virtualKey,
    bool down,
    const RdpKeyboardPhysicalState &physical)
{
    if (down) {
        const unsigned int modifier = rdp::keyboardModifierMaskForVirtualKey(virtualKey);
        if ((physical.modifiers & modifier) != 0)
            trackPassThroughModifierRelease(modifier, true);

        const unsigned int physicalVirtualKey =
            preferredVirtualKeyForModifier(modifier, physical);
        if (physicalVirtualKey != 0)
            virtualKey = physicalVirtualKey;
    }

    const auto key = keyIdentifierFromVirtualKey(virtualKey);
    if (!key)
        return;

    appendKeyAction(actions, *key, down, down && hasTrackedKey(*key), virtualKey, true);
}

bool RdpKeyboardInputRouter::releaseTrackedModifierKeys(std::vector<KeyAction> &actions,
                                                        unsigned int modifiers)
{
    bool released = false;
    const std::vector<KeyIdentifier> pressedKeys = m_pressedKeys;
    for (const auto &key : pressedKeys) {
        if ((modifierMaskForKeyIdentifier(key) & modifiers) == 0)
            continue;

        appendKeyAction(actions, key, false, true, virtualKeyForKeyIdentifier(key), true);
        released = true;
    }
    return released;
}

void RdpKeyboardInputRouter::releaseTrackedNonModifierKeysNotPhysicallyDown(
    std::vector<KeyAction> &actions,
    const RdpKeyboardPhysicalState &physical,
    const KeyIdentifier *excludedKey)
{
    if (!physical.knowsNonModifierKeys)
        return;

    const std::vector<KeyIdentifier> pressedKeys = m_pressedKeys;
    for (const auto &key : pressedKeys) {
        if (excludedKey && key == *excludedKey)
            continue;

        const unsigned int virtualKey = virtualKeyForKeyIdentifier(key);
        if (virtualKey == 0 || rdp::isKeyboardModifierVirtualKey(virtualKey))
            continue;
        if (isVirtualKeyPhysicallyDown(virtualKey, physical))
            continue;

        appendKeyAction(actions, key, false, true, virtualKey, false);
    }
}

void RdpKeyboardInputRouter::synchronizeKeyboardModifiersToPhysicalState(
    std::vector<KeyAction> &actions,
    bool allowKeyDown,
    unsigned int requiredModifiers,
    const KeyIdentifier *excludedKey,
    const RdpKeyboardPhysicalState &physical)
{
    const unsigned int desiredModifiers = allowKeyDown
        ? (physical.modifiers | requiredModifiers)
        : ((m_keyboardModifiers & physical.modifiers) | requiredModifiers);
    const unsigned int staleModifiers = m_keyboardModifiers & ~desiredModifiers;

    unsigned int releasedModifiers = ModifierNone;
    const std::vector<RdpModifierSyncTracker::KeyAction> syncActions =
        m_modifierTracker.synchronize(desiredModifiers);
    for (const auto &action : syncActions) {
        const bool down = action.message == static_cast<std::uint32_t>(WM_KEYDOWN)
            || action.message == static_cast<std::uint32_t>(WM_SYSKEYDOWN);
        if (!down) {
            const unsigned int actionModifiers =
                rdp::keyboardModifierMaskForVirtualKey(action.virtualKey);
            if (releaseTrackedModifierKeys(actions, actionModifiers)) {
                releasedModifiers |= actionModifiers;
                continue;
            }
            releasedModifiers |= actionModifiers;
        }
        appendSynchronizedModifier(actions, action.virtualKey, down, physical);
    }

    const unsigned int noLongerDesiredModifiers = kAllKeyboardModifiers & ~desiredModifiers;
    for (const unsigned int modifier : kKeyboardModifierMasks) {
        if ((noLongerDesiredModifiers & modifier) == 0)
            continue;
        if (releaseTrackedModifierKeys(actions, modifier))
            releasedModifiers |= modifier;
    }

    const unsigned int unreleasedStaleModifiers = staleModifiers & ~releasedModifiers;
    for (const unsigned int modifier : kKeyboardModifierMasks) {
        if ((unreleasedStaleModifiers & modifier) == 0)
            continue;

        const unsigned int virtualKey = virtualKeyForModifierMask(modifier);
        if (virtualKey != 0)
            appendSynchronizedModifier(actions, virtualKey, false, physical);
    }

    releaseTrackedNonModifierKeysNotPhysicallyDown(actions, physical, excludedKey);
    m_keyboardModifiers = desiredModifiers;
}
