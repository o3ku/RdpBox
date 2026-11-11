#include <cassert>
#include <cstdio>
#include <vector>

#include <windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "common/NativeTypes.h"
#include "common/rdp/RdpInputEventUtil.h"
#include "common/rdp/RdpKeyboardInputRouter.h"
#include "common/rdp/RdpKeyboardHookState.h"

namespace
{
KeyIdentifier keyFor(unsigned int virtualKey)
{
    const auto key = keyIdentifierFromVirtualKey(virtualKey);
    assert(key.has_value());
    return *key;
}

KeyEventInfo keyEvent(unsigned int virtualKey, bool down, bool wasDown = false)
{
    return KeyEventInfo{
        keyFor(virtualKey),
        down,
        wasDown
    };
}

RdpKeyboardPhysicalState physical(unsigned int modifiers)
{
    RdpKeyboardPhysicalState state;
    state.modifiers = modifiers;
    if ((modifiers & ModifierControl) != 0)
        state.controlVirtualKey = VK_LCONTROL;
    if ((modifiers & ModifierShift) != 0)
        state.shiftVirtualKey = VK_LSHIFT;
    if ((modifiers & ModifierAlt) != 0)
        state.altVirtualKey = VK_MENU;
    if ((modifiers & ModifierWin) != 0)
        state.winVirtualKey = VK_LWIN;
    return state;
}

RdpLowLevelKeyEvent lowLevelKey(unsigned int virtualKey,
                                bool keyUp,
                                const RdpKeyboardPhysicalState &physical,
                                bool hasWindowFocus = true)
{
    unsigned int flags = 0;
    if ((physical.modifiers & ModifierAlt) != 0)
        flags |= LLKHF_ALTDOWN;
    if (keyUp)
        flags |= LLKHF_UP;

    return RdpLowLevelKeyEvent{
        virtualKey,
        flags,
        keyUp,
        hasWindowFocus,
        false
    };
}

void assertAction(const RdpKeyboardInputRouter::KeyAction &action,
                  unsigned int virtualKey,
                  bool down)
{
    if (action.virtualKey != virtualKey || action.down != down) {
        std::fprintf(stderr,
                     "expected vk=%u down=%d, actual vk=%u down=%d\n",
                     virtualKey,
                     down ? 1 : 0,
                     action.virtualKey,
                     action.down ? 1 : 0);
    }
    assert(action.virtualKey == virtualKey);
    assert(action.down == down);
}
}

int main()
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    {
        RdpKeyboardInputRouter router;
        const RdpKeyboardPhysicalState altPhysical = physical(ModifierAlt);
        const RdpLowLevelKeyEvent altF{
            'F',
            0,
            false,
            true,
            false
        };

        assert(router.shouldCaptureLowLevelKey(altF, altPhysical));
        assert(router.messageForLowLevelKey(altF, altPhysical) == static_cast<std::uint32_t>(WM_SYSKEYDOWN));

        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, 'F', keyEvent('F', true), altPhysical, true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_MENU, true);
        assertAction(actions[1], 'F', true);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        actions = router.handleKeyMessage(WM_SYSKEYUP, 'F', keyEvent('F', false, true), altPhysical, true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'F', false);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        actions = router.handleKeyMessage(WM_SYSKEYUP, VK_MENU, keyEvent(VK_MENU, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, VK_MENU, keyEvent(VK_MENU, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        const RdpLowLevelKeyEvent altTab{
            VK_TAB,
            0,
            false,
            true,
            false
        };
        assert(router.shouldCaptureLowLevelKey(altTab, physical(ModifierNone)));
        assert(router.messageForLowLevelKey(altTab, physical(ModifierNone)) == static_cast<std::uint32_t>(WM_SYSKEYDOWN));

        actions = router.handleKeyMessage(WM_SYSKEYDOWN, VK_TAB, keyEvent(VK_TAB, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_TAB, true);
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 2);

        actions = router.synchronizeMouseModifiers(0, physical(ModifierNone), true);
        assert(actions.empty());
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 2);

        actions = router.handleKeyMessage(WM_SYSKEYUP, VK_TAB, keyEvent(VK_TAB, false, true), physical(ModifierAlt), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_TAB, false);
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 1);

        actions = router.synchronizeMouseModifiers(0, physical(ModifierNone), true);
        assert(actions.empty());
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 1);

        actions = router.handleKeyMessage(WM_SYSKEYUP, VK_MENU, keyEvent(VK_MENU, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        const RdpKeyboardPhysicalState winPhysical = physical(ModifierWin);
        const RdpLowLevelKeyEvent winR{
            'R',
            0,
            false,
            true,
            false
        };

        assert(router.shouldCaptureLowLevelKey(winR, winPhysical));
        assert(router.messageForLowLevelKey(winR, winPhysical) == static_cast<std::uint32_t>(WM_KEYDOWN));

        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN, 'R', keyEvent('R', true), winPhysical, true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LWIN, true);
        assertAction(actions[1], 'R', true);
        assert(router.activeKeyboardModifiers() == ModifierWin);

        actions = router.handleKeyMessage(WM_KEYUP, 'R', keyEvent('R', false, true), winPhysical, true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'R', false);
        assert(router.activeKeyboardModifiers() == ModifierWin);
        assert(router.pressedKeyCount() == 1);

        actions = router.handleKeyMessage(WM_KEYUP, VK_LWIN, keyEvent(VK_LWIN, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN, VK_LWIN, keyEvent(VK_LWIN, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, true);
        assert(router.activeKeyboardModifiers() == ModifierWin);

        const RdpLowLevelKeyEvent winR{
            'R',
            0,
            false,
            true,
            false
        };
        assert(router.shouldCaptureLowLevelKey(winR, physical(ModifierNone)));
        assert(router.messageForLowLevelKey(winR, physical(ModifierNone)) == static_cast<std::uint32_t>(WM_KEYDOWN));

        actions = router.handleKeyMessage(WM_KEYDOWN, 'R', keyEvent('R', true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'R', true);
        assert(router.activeKeyboardModifiers() == ModifierWin);
        assert(router.pressedKeyCount() == 2);

        actions = router.handleKeyMessage(WM_KEYUP, 'R', keyEvent('R', false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'R', false);
        assert(router.activeKeyboardModifiers() == ModifierWin);

        actions = router.handleKeyMessage(WM_KEYUP, VK_LWIN, keyEvent(VK_LWIN, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN, 'R', keyEvent('R', true), physical(ModifierWin), true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LWIN, true);
        assertAction(actions[1], 'R', true);

        actions = router.handleKeyMessage(WM_KEYUP, VK_LWIN, keyEvent(VK_LWIN, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 1);

        const RdpLowLevelKeyEvent winRRelease{
            'R',
            0,
            true,
            true,
            false
        };
        assert(router.shouldCaptureLowLevelKey(winRRelease, physical(ModifierNone)));
        assert(router.messageForLowLevelKey(winRRelease, physical(ModifierNone)) == static_cast<std::uint32_t>(WM_KEYUP));

        actions = router.handleKeyMessage(WM_KEYUP, 'R', keyEvent('R', false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'R', false);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, 'F', keyEvent('F', true), physical(ModifierAlt), true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_MENU, true);
        assertAction(actions[1], 'F', true);

        actions = router.handleKeyMessage(WM_SYSKEYUP, VK_MENU, keyEvent(VK_MENU, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 1);

        const RdpLowLevelKeyEvent altFRelease{
            'F',
            0,
            true,
            true,
            false
        };
        assert(router.shouldCaptureLowLevelKey(altFRelease, physical(ModifierNone)));
        assert(router.messageForLowLevelKey(altFRelease, physical(ModifierNone)) == static_cast<std::uint32_t>(WM_KEYUP));

        actions = router.handleKeyMessage(WM_KEYUP, 'F', keyEvent('F', false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'F', false);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, VK_MENU, keyEvent(VK_MENU, true), physical(ModifierAlt), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LMENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN,
                                    VK_LWIN,
                                    keyEvent(VK_LWIN, true),
                                    physical(ModifierWin),
                                    true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, true);

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, VK_MENU, keyEvent(VK_MENU, true), physical(ModifierAlt), true);
        assert(actions.size() == 1);

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LMENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_MENU, true, physical(ModifierNone), false),
            physical(ModifierNone)));
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN,
                                    'F',
                                    keyEvent('F', true),
                                    physical(ModifierAlt),
                                    true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_MENU, true);
        assertAction(actions[1], 'F', true);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey('F', true, physical(ModifierNone), false),
            physical(ModifierNone)));

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LMENU, false);
        assertAction(actions[1], 'F', false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, VK_MENU, keyEvent(VK_MENU, true), physical(ModifierAlt), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);

        actions = router.handleKeyMessage(WM_KEYDOWN,
                                          VK_LCONTROL,
                                          keyEvent(VK_LCONTROL, true),
                                          physical(ModifierAlt | ModifierControl),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, true);
        assert(router.activeKeyboardModifiers() == (ModifierAlt | ModifierControl));

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LMENU, false);
        assertAction(actions[1], VK_LCONTROL, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LCONTROL, true, physical(ModifierNone), false),
            physical(ModifierNone)));
        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_MENU, true, physical(ModifierNone), false),
            physical(ModifierNone)));
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(0, physical(ModifierAlt), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);
        assert(actions[0].synchronizedModifier);
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 1);

        actions = router.synchronizeMouseModifiers(0, physical(ModifierNone), true);
        assert(actions.empty());
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 1);

        actions = router.handleKeyMessage(WM_SYSKEYUP, VK_MENU, keyEvent(VK_MENU, false, true), physical(ModifierNone), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_LBUTTON, physical(ModifierAlt), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);
        assert(actions[0].synchronizedModifier);
        assert(router.activeKeyboardModifiers() == ModifierAlt);
        assert(router.pressedKeyCount() == 1);

        const RdpKeyboardPhysicalState altPhysical = physical(ModifierAlt);
        assert(router.shouldCaptureLowLevelKey(lowLevelKey('1', false, altPhysical), altPhysical));
        actions = router.handleKeyMessage(WM_SYSKEYDOWN,
                                          '1',
                                          keyEvent('1', true),
                                          altPhysical,
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], '1', true);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        assert(router.shouldCaptureLowLevelKey(lowLevelKey('1', true, altPhysical), altPhysical));
        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          '1',
                                          keyEvent('1', false, true),
                                          altPhysical,
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], '1', false);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        assert(router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_MENU, true, physical(ModifierNone)),
            physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_MENU,
                                          keyEvent(VK_MENU, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        const RdpKeyboardPhysicalState altCtrlShiftPhysical =
            physical(ModifierAlt | ModifierControl | ModifierShift);
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_LBUTTON | MK_CONTROL | MK_SHIFT,
                                             altCtrlShiftPhysical,
                                             true);
        assert(actions.size() == 3);
        assertAction(actions[0], VK_LCONTROL, true);
        assertAction(actions[1], VK_LSHIFT, true);
        assertAction(actions[2], VK_MENU, true);
        assert(router.activeKeyboardModifiers() == (ModifierAlt | ModifierControl | ModifierShift));

        assert(router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LCONTROL, true, physical(ModifierAlt | ModifierShift)),
            physical(ModifierAlt | ModifierShift)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LCONTROL,
                                          keyEvent(VK_LCONTROL, false, true),
                                          physical(ModifierAlt | ModifierShift),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, false);
        assert(router.activeKeyboardModifiers() == (ModifierAlt | ModifierShift));

        assert(router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LSHIFT, true, physical(ModifierAlt)),
            physical(ModifierAlt)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, false, true),
                                          physical(ModifierAlt),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, false);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        assert(router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_MENU, true, physical(ModifierNone)),
            physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_MENU,
                                          keyEvent(VK_MENU, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        const RdpKeyboardPhysicalState winShiftPhysical = physical(ModifierWin | ModifierShift);
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_LBUTTON | MK_SHIFT, winShiftPhysical, true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LSHIFT, true);
        assertAction(actions[1], VK_LWIN, true);
        assert(router.activeKeyboardModifiers() == (ModifierWin | ModifierShift));

        assert(router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LSHIFT, true, physical(ModifierWin)),
            physical(ModifierWin)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, false, true),
                                          physical(ModifierWin),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, false);
        assert(router.activeKeyboardModifiers() == ModifierWin);

        assert(router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LWIN, true, physical(ModifierNone)),
            physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LWIN,
                                          keyEvent(VK_LWIN, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LWIN, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        // A host drag with Ctrl held passes over an unfocused session: the
        // pane may release what it holds, but must not press host-side
        // modifiers into the remote desktop.
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_LBUTTON | MK_CONTROL, physical(ModifierControl), false);
        assert(actions.empty());
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);

        // Held session modifiers still come back up, focus or not.
        RdpKeyboardInputRouter heldRouter;
        actions = heldRouter.synchronizeMouseModifiers(MK_LBUTTON | MK_CONTROL,
                                                       physical(ModifierControl | ModifierAlt),
                                                       true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LCONTROL, true);
        assertAction(actions[1], VK_MENU, true);
        actions = heldRouter.synchronizeMouseModifiers(0, physical(ModifierControl), false);
        // The unfocused sync releases both held modifiers (release order is
        // the tracker's, not the press order).
        assert(actions.size() == 2);
        for (const auto &action : actions) {
            assert(!action.down);
            assert(action.virtualKey == VK_LMENU || action.virtualKey == VK_CONTROL);
        }
        assert(heldRouter.activeKeyboardModifiers() == ModifierNone);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_LBUTTON, physical(ModifierAlt), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LMENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_CONTROL | MK_SHIFT, physical(ModifierNone), true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_CONTROL, true);
        assert(actions[0].synchronizedModifier);
        assertAction(actions[1], VK_SHIFT, true);
        assert(actions[1].synchronizedModifier);
        assert(router.activeKeyboardModifiers() == (ModifierControl | ModifierShift));
        assert(router.pressedKeyCount() == 2);

        actions = router.synchronizeMouseModifiers(0, physical(ModifierNone), true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LCONTROL, false);
        assert(actions[0].synchronizedModifier);
        assertAction(actions[1], VK_LSHIFT, false);
        assert(actions[1].synchronizedModifier);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN,
                                    VK_LCONTROL,
                                    keyEvent(VK_LCONTROL, true),
                                    physical(ModifierControl),
                                    true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, true);
        assert(router.activeKeyboardModifiers() == ModifierControl);

        actions = router.handleKeyMessage(WM_KEYDOWN,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, true),
                                          physical(ModifierControl | ModifierShift),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, true);
        assert(router.activeKeyboardModifiers() == (ModifierControl | ModifierShift));

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LSHIFT, true, physical(ModifierControl)),
            physical(ModifierControl)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, false, true),
                                          physical(ModifierControl),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, false);
        assert(router.activeKeyboardModifiers() == ModifierControl);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LCONTROL, true, physical(ModifierNone)),
            physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LCONTROL,
                                          keyEvent(VK_LCONTROL, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN,
                                    VK_LCONTROL,
                                    keyEvent(VK_LCONTROL, true),
                                    physical(ModifierControl),
                                    true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, true);

        const RdpKeyboardPhysicalState controlPhysical = physical(ModifierControl);
        assert(router.shouldCaptureLowLevelKey(lowLevelKey(VK_ESCAPE, false, controlPhysical),
                                               controlPhysical));
        assert(router.messageForLowLevelKey(lowLevelKey(VK_ESCAPE, false, controlPhysical),
                                            controlPhysical)
               == static_cast<std::uint32_t>(WM_KEYDOWN));

        actions = router.handleKeyMessage(WM_KEYDOWN,
                                          VK_ESCAPE,
                                          keyEvent(VK_ESCAPE, true),
                                          controlPhysical,
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_ESCAPE, true);
        assert(router.activeKeyboardModifiers() == ModifierControl);

        assert(router.shouldCaptureLowLevelKey(lowLevelKey(VK_ESCAPE, true, physical(ModifierControl)),
                                               physical(ModifierControl)));
        assert(!router.shouldCaptureLowLevelKey(lowLevelKey(VK_LCONTROL, true, physical(ModifierNone)),
                                                physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_ESCAPE,
                                          keyEvent(VK_ESCAPE, false, true),
                                          controlPhysical,
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_ESCAPE, false);
        assert(router.activeKeyboardModifiers() == ModifierControl);

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LCONTROL,
                                          keyEvent(VK_LCONTROL, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN, 'F', keyEvent('F', true), physical(ModifierAlt), true);
        assert(actions.size() == 2);

        actions = router.releaseAllPressedKeys();
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LMENU, false);
        assertAction(actions[1], 'F', false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN,
                                    VK_LSHIFT,
                                    keyEvent(VK_LSHIFT, true),
                                    physical(ModifierShift),
                                    true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, true);
        assert(router.activeKeyboardModifiers() == ModifierShift);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LSHIFT, true, physical(ModifierNone), true),
            physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(MK_LBUTTON | MK_SHIFT, physical(ModifierShift), true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, true);
        assert(actions[0].synchronizedModifier);
        assert(router.activeKeyboardModifiers() == ModifierShift);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LSHIFT, true, physical(ModifierShift), true),
            physical(ModifierShift)));
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_KEYDOWN,
                                    VK_LCONTROL,
                                    keyEvent(VK_LCONTROL, true),
                                    physical(ModifierControl),
                                    true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, true);
        assert(router.activeKeyboardModifiers() == ModifierControl);

        assert(!router.shouldCaptureLowLevelKey(
            lowLevelKey(VK_LCONTROL, true, physical(ModifierNone), true),
            physical(ModifierNone)));

        actions = router.handleKeyMessage(WM_KEYUP,
                                          VK_LCONTROL,
                                          keyEvent(VK_LCONTROL, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
    }

    {
        RdpKeyboardInputRouter router;
        const RdpLowLevelKeyEvent reservedShortcut{
            'P',
            0,
            false,
            true,
            true
        };
        assert(!router.shouldCaptureLowLevelKey(reservedShortcut, physical(ModifierControl)));
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.handleKeyMessage(WM_SYSKEYDOWN,
                                    VK_MENU,
                                    keyEvent(VK_MENU, true),
                                    physical(ModifierAlt),
                                    true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, true);

        actions = router.handleKeyMessage(WM_SYSKEYDOWN,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, true),
                                          physical(ModifierAlt | ModifierShift),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, true);
        assert(router.activeKeyboardModifiers() == (ModifierAlt | ModifierShift));

        actions = router.handleKeyMessage(WM_SYSKEYDOWN,
                                          VK_OEM_MINUS,
                                          keyEvent(VK_OEM_MINUS, true),
                                          physical(ModifierAlt),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_OEM_MINUS, true);
        assert(router.activeKeyboardModifiers() == (ModifierAlt | ModifierShift));

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_OEM_MINUS,
                                          keyEvent(VK_OEM_MINUS, false, true),
                                          physical(ModifierAlt),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_OEM_MINUS, false);
        assert(router.activeKeyboardModifiers() == (ModifierAlt | ModifierShift));

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_LSHIFT,
                                          keyEvent(VK_LSHIFT, false, true),
                                          physical(ModifierAlt),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LSHIFT, false);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        actions = router.handleKeyMessage(WM_SYSKEYDOWN,
                                          'W',
                                          keyEvent('W', true),
                                          physical(ModifierAlt),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'W', true);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          'W',
                                          keyEvent('W', false, true),
                                          physical(ModifierAlt),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'W', false);
        assert(router.activeKeyboardModifiers() == ModifierAlt);

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_MENU,
                                          keyEvent(VK_MENU, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    {
        RdpKeyboardInputRouter router;
        std::vector<RdpKeyboardInputRouter::KeyAction> actions =
            router.synchronizeMouseModifiers(
                MK_LBUTTON | MK_CONTROL,
                physical(ModifierControl | ModifierAlt),
                true);
        assert(actions.size() == 2);
        assertAction(actions[0], VK_LCONTROL, true);
        assertAction(actions[1], VK_MENU, true);
        assert(router.activeKeyboardModifiers() == (ModifierControl | ModifierAlt));

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_MENU,
                                          keyEvent(VK_MENU, false, true),
                                          physical(ModifierControl),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_MENU, false);
        assert(router.activeKeyboardModifiers() == ModifierControl);

        actions = router.handleKeyMessage(WM_SYSKEYUP,
                                          VK_LCONTROL,
                                          keyEvent(VK_LCONTROL, false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], VK_LCONTROL, false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);

        actions = router.handleKeyMessage(WM_KEYDOWN,
                                          'W',
                                          keyEvent('W', true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'W', true);
        assert(router.activeKeyboardModifiers() == ModifierNone);

        actions = router.handleKeyMessage(WM_KEYUP,
                                          'W',
                                          keyEvent('W', false, true),
                                          physical(ModifierNone),
                                          true);
        assert(actions.size() == 1);
        assertAction(actions[0], 'W', false);
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    // Both sides remain independent even if physical probing is transiently stale.
    const unsigned int modifierPairs[][3] = {
        {VK_LCONTROL, VK_RCONTROL, ModifierControl}, {VK_LSHIFT, VK_RSHIFT, ModifierShift},
        {VK_LMENU, VK_RMENU, ModifierAlt}, {VK_LWIN, VK_RWIN, ModifierWin}
    };
    for (const auto &pair : modifierPairs) {
        for (unsigned int first : {0u, 1u}) {
            RdpKeyboardInputRouter router;
            router.handleKeyMessage(WM_KEYDOWN, pair[0], keyEvent(pair[0], true), physical(ModifierNone), true);
            router.handleKeyMessage(WM_KEYDOWN, pair[1], keyEvent(pair[1], true), physical(ModifierNone), true);
            auto actions = router.handleKeyMessage(WM_KEYUP, pair[first], keyEvent(pair[first], false, true), physical(ModifierNone), true);
            assert(actions.size() == 1);
            assert(router.activeKeyboardModifiers() == pair[2]);
            assert(router.pressedKeyCount() == 1);
            actions = router.handleKeyMessage(WM_KEYDOWN, 'A', keyEvent('A', true), physical(ModifierNone), true);
            assert(actions.size() == 1); // no duplicate opposite-side press
            actions = router.handleKeyMessage(WM_KEYUP, pair[1 - first], keyEvent(pair[1 - first], false, true), physical(ModifierNone), true);
            assert(actions.size() == 1);
            assert(router.activeKeyboardModifiers() == ModifierNone);
            actions = router.releaseAllPressedKeys();
            assert(actions.size() == 1);
            assertAction(actions[0], 'A', false);
        }
    }

    // Ctrl pressed before Alt belongs to Windows; its release must pass through.
    // Ctrl pressed after Alt belongs to the hook, even if Alt is released first.
    for (bool altFirst : {false, true}) {
        RdpKeyboardInputRouter router;
        RdpKeyboardHookState hook;
        const unsigned int first = altFirst ? VK_LMENU : VK_LCONTROL;
        const unsigned int second = altFirst ? VK_LCONTROL : VK_LMENU;
        for (unsigned int vk : {first, second}) {
            const bool requested = router.shouldCaptureLowLevelKey(
                lowLevelKey(vk, false, physical(ModifierNone)), physical(ModifierNone));
            const bool captured = hook.consumeKey(vk, true, requested);
            assert(captured == (altFirst || vk == VK_LMENU));
            // Both the captured hook path and pass-through window path forward.
            router.handleKeyMessage(WM_KEYDOWN, vk, keyEvent(vk, true), physical(ModifierNone), true);
        }
        for (unsigned int vk : {VK_LMENU, VK_LCONTROL}) {
            const bool requested = router.shouldCaptureLowLevelKey(
                lowLevelKey(vk, true, physical(ModifierNone)), physical(ModifierNone));
            const bool captured = hook.consumeKey(vk, false, requested);
            assert(captured == (altFirst || vk == VK_LMENU));
            router.handleKeyMessage(WM_KEYUP, vk, keyEvent(vk, false, true), physical(ModifierNone), true);
        }
        assert(!hook.hasCapturedKeys());
        assert(router.activeKeyboardModifiers() == ModifierNone);
        assert(router.pressedKeyCount() == 0);
    }

    return 0;
}
