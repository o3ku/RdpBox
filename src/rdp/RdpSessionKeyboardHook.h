#pragma once

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

bool isKeyboardTarget(const CRdpSessionView *target);
void setKeyboardTarget(CRdpSessionView *target);
void clearKeyboardTarget(CRdpSessionView *target);

KeyboardMessageDisposition handleWindowKeyMessage(CRdpSessionView &target,
                                                   std::uint32_t message,
                                                   std::uintptr_t wParam,
                                                   std::intptr_t lParam);
}
