#pragma once

#include "common/rdp/RdpKeyboardInputRouter.h"

#include <cstdint>

class CRdpSessionView;

namespace rdp::session_view_input
{
enum class KeyboardMessageDisposition
{
    NotHandled,
    PassThrough,
    Handled,
};

// Implemented by session views that want the WH_KEYBOARD_LL hook to capture
// system keys (Win, Alt+Tab, Esc, ...) and forward them into the session.
class RdpSystemKeyTarget
{
public:
    virtual ~RdpSystemKeyTarget() = default;
    virtual bool canCaptureSystemKeys() const = 0;
    virtual bool hasWindowFocus() const = 0;
    virtual bool shouldCaptureLowLevelKey(const RdpLowLevelKeyEvent &event,
                                          const RdpKeyboardPhysicalState &physical) const = 0;
    virtual std::uint32_t messageForLowLevelKey(const RdpLowLevelKeyEvent &event,
                                                const RdpKeyboardPhysicalState &physical) const = 0;
    virtual void forwardNativeKeyMessage(std::uint32_t message,
                                         std::uintptr_t wParam,
                                         std::intptr_t lParam) = 0;
    virtual void releaseKeyboardInputForTargetTransfer() = 0;
};

bool isKeyboardTarget(const RdpSystemKeyTarget *target);
void setKeyboardTarget(RdpSystemKeyTarget *target);
void clearKeyboardTarget(RdpSystemKeyTarget *target);
}
