#pragma once

#include <windows.h>

namespace ui
{
inline bool shouldFocusActiveSessionOnActivate(UINT state, bool minimized)
{
    if (minimized)
        return false;

    return state == WA_ACTIVE || state == WA_CLICKACTIVE;
}
}
