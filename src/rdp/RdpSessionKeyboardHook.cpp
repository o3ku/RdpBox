#include "rdp/RdpSessionKeyboardHook.h"

#include "RdpSessionView.h"

#include "rdp/RdpInputEventUtil.h"
#include "rdp/RdpSystemChordTrace.h"
#include "ui/MainWindowShortcuts.h"

#include <cstdint>

namespace
{
CRdpSessionView *g_systemKeyTarget = nullptr;
HHOOK g_keyboardHook = nullptr;

bool isVirtualKeyPhysicallyDown(int virtualKey)
{
    return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

unsigned int physicalKeyboardModifiers()
{
    unsigned int modifiers = ModifierNone;
    if (isVirtualKeyPhysicallyDown(VK_CONTROL))
        modifiers |= ModifierControl;
    if (isVirtualKeyPhysicallyDown(VK_SHIFT))
        modifiers |= ModifierShift;
    if (isVirtualKeyPhysicallyDown(VK_MENU))
        modifiers |= ModifierAlt;
    if (isVirtualKeyPhysicallyDown(VK_LWIN) || isVirtualKeyPhysicallyDown(VK_RWIN))
        modifiers |= ModifierWin;
    return modifiers;
}

RdpKeyboardPhysicalState physicalKeyboardState()
{
    RdpKeyboardPhysicalState state;
    state.modifiers = physicalKeyboardModifiers();
    if (isVirtualKeyPhysicallyDown(VK_RCONTROL))
        state.controlVirtualKey = VK_RCONTROL;
    else if (isVirtualKeyPhysicallyDown(VK_LCONTROL))
        state.controlVirtualKey = VK_LCONTROL;
    if (isVirtualKeyPhysicallyDown(VK_RSHIFT))
        state.shiftVirtualKey = VK_RSHIFT;
    else if (isVirtualKeyPhysicallyDown(VK_LSHIFT))
        state.shiftVirtualKey = VK_LSHIFT;
    if (isVirtualKeyPhysicallyDown(VK_RMENU))
        state.altVirtualKey = VK_RMENU;
    else if (isVirtualKeyPhysicallyDown(VK_LMENU))
        state.altVirtualKey = VK_LMENU;
    if (isVirtualKeyPhysicallyDown(VK_RWIN))
        state.winVirtualKey = VK_RWIN;
    else if (isVirtualKeyPhysicallyDown(VK_LWIN))
        state.winVirtualKey = VK_LWIN;
    return state;
}

bool isReservedLowLevelShortcut(const KBDLLHOOKSTRUCT *info)
{
    if (!info)
        return false;

    return ui::isReservedMainWindowShortcut(isVirtualKeyPhysicallyDown(VK_CONTROL),
                                            isVirtualKeyPhysicallyDown(VK_MENU)
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
        g_systemKeyTarget && ::GetFocus() == g_systemKeyTarget->GetSafeHwnd(),
        isReservedLowLevelShortcut(info)
    };
}

bool shouldCaptureLowLevelKey(const KBDLLHOOKSTRUCT *info, bool keyUp)
{
    if (!info || !g_systemKeyTarget)
        return false;

    return g_systemKeyTarget->shouldCaptureLowLevelKey(
        lowLevelKeyEventFromInfo(info, keyUp),
        physicalKeyboardState());
}

LRESULT CALLBACK keyboardHookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < HC_ACTION || !g_systemKeyTarget)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    const bool traceKey = rdp::trace::shouldTraceSystemChordVirtualKey(static_cast<unsigned int>(info->vkCode));
    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP || (info->flags & LLKHF_UP));
    const bool shouldCapture = shouldCaptureLowLevelKey(info, keyUp);
    const bool canCapture = g_systemKeyTarget->canCaptureSystemKeys();
    if (traceKey) {
        rdp::trace::logSystemChordEvent(
            shouldCapture && canCapture ? L"hook-forward" : L"hook-pass",
            static_cast<unsigned int>(info->vkCode),
            0,
            0,
            static_cast<unsigned int>(info->flags),
            g_systemKeyTarget->activeKeyboardModifiers(),
            ::GetFocus() == g_systemKeyTarget->GetSafeHwnd(),
            false,
            0);
    }
    if (!shouldCapture || !canCapture)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
    const RdpLowLevelKeyEvent lowLevelEvent = lowLevelKeyEventFromInfo(info, keyUp);
    const std::uint32_t message =
        g_systemKeyTarget->messageForLowLevelKey(lowLevelEvent, physicalKeyboardState());
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
bool isKeyboardTarget(const CRdpSessionView *target)
{
    return g_systemKeyTarget == target;
}

void setKeyboardTarget(CRdpSessionView *target)
{
    g_systemKeyTarget = target;
    if (g_systemKeyTarget)
        ensureKeyboardHook();
}

void clearKeyboardTarget(CRdpSessionView *target)
{
    if (g_systemKeyTarget == target)
        g_systemKeyTarget = nullptr;
    releaseKeyboardHookIfUnused();
}

KeyboardMessageDisposition handleWindowKeyMessage(CRdpSessionView &target,
                                                  std::uint32_t message,
                                                  std::uintptr_t wParam,
                                                  std::intptr_t lParam)
{
    if (message != WM_KEYDOWN && message != WM_KEYUP && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP)
        return KeyboardMessageDisposition::NotHandled;

    const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (down && ui::isReservedMainWindowShortcut(controlDown,
                                                 altDown,
                                                 static_cast<unsigned int>(wParam))) {
        target.noteConsumedLocalShortcutKey(static_cast<unsigned int>(wParam));
        return KeyboardMessageDisposition::PassThrough;
    }

    if (!down && target.consumeReservedShortcutKey(static_cast<unsigned int>(wParam)))
        return KeyboardMessageDisposition::PassThrough;

    target.forwardNativeKeyMessage(static_cast<std::uint32_t>(message),
                                   static_cast<std::uintptr_t>(wParam),
                                   static_cast<std::intptr_t>(lParam));
    return KeyboardMessageDisposition::Handled;
}
}
