#include "common/rdp/RdpSessionKeyboardHook.h"

#include "common/rdp/RdpKeyboardHookState.h"
#include "common/rdp/RdpInputEventUtil.h"
#include "common/rdp/RdpKeyboardPhysicalProbing.h"
#include "common/ui/MainWindowShortcuts.h"

#include <cstdint>

using rdp::session_view_input::RdpSystemKeyTarget;

#ifndef _WIN32
// Low-level system-key capture is a WH_KEYBOARD_LL facility; off Windows the
// target bookkeeping stays functional but no keys are intercepted.
namespace rdp::session_view_input
{
namespace
{
RdpSystemKeyTarget *g_systemKeyTarget = nullptr;
}

bool isKeyboardTarget(const RdpSystemKeyTarget *target)
{
    return g_systemKeyTarget == target;
}

void setKeyboardTarget(RdpSystemKeyTarget *target)
{
    g_systemKeyTarget = target;
}

void clearKeyboardTarget(RdpSystemKeyTarget *target)
{
    if (g_systemKeyTarget == target)
        g_systemKeyTarget = nullptr;
}

// No low-level hook exists off Windows; nothing to suspend.
void suspendKeyboardHookForBlockingCall()
{
}
}
#else

namespace
{
RdpSystemKeyTarget *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;
RdpKeyboardHookState g_hookState;

bool isReservedLowLevelShortcut(const KBDLLHOOKSTRUCT *info)
{
    if (!info)
        return false;

    const bool controlDown = rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_CONTROL);
    const bool shiftDown = rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_SHIFT);
    const bool altDown = rdp::physical_keyboard::isVirtualKeyPhysicallyDown(VK_MENU);
    return ui::isReservedMainWindowShortcut(controlDown,
                                            shiftDown,
                                            altDown
                                                || (info->flags & LLKHF_ALTDOWN) != 0,
                                            static_cast<unsigned int>(info->vkCode));
}

RdpLowLevelKeyEvent lowLevelKeyEventFromInfo(const KBDLLHOOKSTRUCT *info, bool keyUp)
{
    if (!info)
        return {};

    return RdpLowLevelKeyEvent{
        static_cast<unsigned int>(info->vkCode),
        static_cast<unsigned int>(info->flags),
        keyUp,
        g_systemKeyTarget && g_systemKeyTarget->hasWindowFocus(),
        isReservedLowLevelShortcut(info)
    };
}

bool shouldCaptureLowLevelKey(const KBDLLHOOKSTRUCT *info, bool keyUp)
{
    if (!info || !g_systemKeyTarget)
        return false;

    return g_systemKeyTarget->shouldCaptureLowLevelKey(
        lowLevelKeyEventFromInfo(info, keyUp),
        rdp::physical_keyboard::physicalKeyboardState());
}

void releaseKeyboardHookIfUnused();

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    if (!info)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const unsigned int virtualKey = static_cast<unsigned int>(info->vkCode);
    const bool detached = g_hookState.isDetached(virtualKey);
    const bool captureRequested = g_systemKeyTarget
        && shouldCaptureLowLevelKey(info, keyUp) && g_systemKeyTarget->canCaptureSystemKeys();
    if (!g_hookState.consumeKey(virtualKey, !keyUp, captureRequested,
                                rdp::physical_keyboard::isVirtualKeyPhysicallyDown(virtualKey)))
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    // Focus loss already released the remote keys; swallow their remaining
    // repeats/releases without sending them into a newly focused session.
    if (detached || !g_systemKeyTarget || !g_systemKeyTarget->canCaptureSystemKeys()) {
        releaseKeyboardHookIfUnused();
        return 1;
    }

    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
    const RdpLowLevelKeyEvent lowLevelEvent = lowLevelKeyEventFromInfo(info, keyUp);
    const std::uint32_t message =
        g_systemKeyTarget->messageForLowLevelKey(lowLevelEvent, rdp::physical_keyboard::physicalKeyboardState());
    const std::intptr_t keyLParam = static_cast<std::intptr_t>((info->scanCode & 0xFFu) << 16)
        | (extended ? 0x01000000 : 0)
        | (keyUp ? 0xC0000000 : 0);

    g_systemKeyTarget->forwardNativeKeyMessage(
        message,
        static_cast<std::uintptr_t>(info->vkCode),
        keyLParam);
    return 1;
}

void ensureKeyboardHook()
{
    if (!g_keyboardHook) {
        // Keys held across an install have no recorded press to pair with.
        g_hookState.reset();
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);
    }
}

void releaseKeyboardHookIfUnused()
{
    if (!g_systemKeyTarget && !g_hookState.hasCapturedKeys() && g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        // A release that arrives while unhooked can no longer be paired.
        g_hookState.reset();
    }
}
}

namespace rdp::session_view_input
{
bool isKeyboardTarget(const RdpSystemKeyTarget *target)
{
    return g_systemKeyTarget == target;
}

void setKeyboardTarget(RdpSystemKeyTarget *target)
{
    if (g_systemKeyTarget == target) {
        if (g_systemKeyTarget)
            ensureKeyboardHook();
        return;
    }

    g_hookState.detachCapturedKeys();
    RdpSystemKeyTarget *oldTarget = g_systemKeyTarget;
    g_systemKeyTarget = target;

    if (oldTarget)
        oldTarget->releaseKeyboardInputForTargetTransfer();

    if (g_systemKeyTarget)
        ensureKeyboardHook();
}

void clearKeyboardTarget(RdpSystemKeyTarget *target)
{
    if (g_systemKeyTarget == target) {
        g_hookState.detachCapturedKeys();
        g_systemKeyTarget = nullptr;
    }
    releaseKeyboardHookIfUnused();
}

// Temporarily remove the low-level hook for a UI-thread operation that cannot
// pump messages (FreeRdpProcess::stop() waits out a wedged disconnect). A
// WH_KEYBOARD_LL installed on a thread that is not pumping stalls EVERY key
// on the desktop for the hook timeout (~300ms each) while it is skipped, so
// the hook is taken down first and reinstalled by the next focus/target call.
void suspendKeyboardHookForBlockingCall()
{
#ifdef _WIN32
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
        // Presses that went to Windows while unhooked have no pairing; start
        // clean exactly like a fresh install does.
        g_hookState.reset();
    }
#else
    // Off Windows the target bookkeeping needs no hook.
#endif
}
} // namespace rdp::session_view_input
#endif // _WIN32
