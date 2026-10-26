#include "common/rdp/RdpSessionKeyboardHook.h"

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
}
#else

namespace
{
RdpSystemKeyTarget *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;

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

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION || !g_systemKeyTarget)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const bool shouldCapture = shouldCaptureLowLevelKey(info, keyUp);
    const bool canCapture = g_systemKeyTarget->canCaptureSystemKeys();
    if (!shouldCapture || !canCapture)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

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
    if (!g_keyboardHook)
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, nullptr, 0);
}

void releaseKeyboardHookIfUnused()
{
    if (!g_systemKeyTarget && g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
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

    RdpSystemKeyTarget *oldTarget = g_systemKeyTarget;
    g_systemKeyTarget = target;

    if (oldTarget)
        oldTarget->releaseKeyboardInputForTargetTransfer();

    if (g_systemKeyTarget)
        ensureKeyboardHook();
}

void clearKeyboardTarget(RdpSystemKeyTarget *target)
{
    if (g_systemKeyTarget == target)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
}
}
#endif // _WIN32
