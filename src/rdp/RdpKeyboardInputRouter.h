#pragma once

#include "common/NativeTypes.h"
#include "rdp/RdpInputEventUtil.h"
#include "rdp/RdpModifierSyncTracker.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <windows.h>

struct RdpKeyboardPhysicalState
{
    unsigned int modifiers = ModifierNone;
    unsigned int controlVirtualKey = VK_CONTROL;
    unsigned int shiftVirtualKey = VK_SHIFT;
    unsigned int altVirtualKey = VK_MENU;
    unsigned int winVirtualKey = VK_LWIN;
    bool knowsNonModifierKeys = false;
    std::vector<unsigned int> pressedVirtualKeys;
};

struct RdpLowLevelKeyEvent
{
    unsigned int virtualKey = 0;
    unsigned int flags = 0;
    bool keyUp = false;
    bool hasWindowFocus = false;
    bool reservedShortcut = false;
};

class RdpKeyboardInputRouter
{
public:
    struct KeyAction
    {
        KeyIdentifier key;
        bool down = false;
        bool wasDown = false;
        unsigned int virtualKey = 0;
        bool synchronizedModifier = false;
    };

    void reset();
    void onFocusGained();

    unsigned int activeKeyboardModifiers() const;
    bool captureSystemKeysWithoutFocus() const;
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
    std::vector<KeyAction> handleFocusLost(const RdpKeyboardPhysicalState &physical);
    std::vector<KeyAction> releaseAllPressedKeys();

private:
    bool hasTrackedKey(const KeyIdentifier &key) const;
    bool hasLowLevelReleaseKey(const KeyIdentifier &key) const;
    void appendKeyAction(std::vector<KeyAction> &actions,
                         const KeyIdentifier &key,
                         bool down,
                         bool wasDown,
                         unsigned int virtualKey,
                         bool synchronizedModifier);
    void trackLowLevelReleaseKey(const KeyIdentifier &key, bool down);
    void trackPassThroughModifierRelease(unsigned int modifier, bool enabled);
    bool shouldTrackLowLevelRelease(std::uint32_t message,
                                    unsigned int virtualKey,
                                    const RdpKeyboardPhysicalState &physical) const;
    void trackKeyState(const KeyIdentifier &key, bool down);
    void recordModifierState(std::uint32_t message, unsigned int virtualKey);
    unsigned int preferredVirtualKeyForModifier(unsigned int modifier,
                                                const RdpKeyboardPhysicalState &physical) const;
    void appendSynchronizedModifier(std::vector<KeyAction> &actions,
                                    unsigned int virtualKey,
                                    bool down,
                                    const RdpKeyboardPhysicalState &physical);
    bool releaseTrackedModifierKeys(std::vector<KeyAction> &actions, unsigned int modifiers);
    void releaseTrackedNonModifierKeysNotPhysicallyDown(std::vector<KeyAction> &actions,
                                                        const RdpKeyboardPhysicalState &physical,
                                                        const KeyIdentifier *excludedKey);
    void synchronizeKeyboardModifiersToPhysicalState(std::vector<KeyAction> &actions,
                                                     bool allowKeyDown,
                                                     unsigned int requiredModifiers,
                                                     const KeyIdentifier *excludedKey,
                                                     const RdpKeyboardPhysicalState &physical);
    void refreshSystemKeyCaptureState(std::vector<KeyAction> &actions,
                                      const RdpKeyboardPhysicalState &physical,
                                      bool hasWindowFocus);

    RdpModifierSyncTracker m_modifierTracker;
    std::vector<KeyIdentifier> m_pressedKeys;
    std::vector<KeyIdentifier> m_lowLevelReleaseKeys;
    unsigned int m_keyboardModifiers = ModifierNone;
    unsigned int m_passThroughModifierReleases = ModifierNone;
    bool m_captureSystemKeysWithoutFocus = false;
};
