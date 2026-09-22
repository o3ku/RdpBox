#pragma once

#include <windows.h>

namespace ui
{
inline bool shouldTrackWindowStateInteraction(UINT message, WPARAM hitTest)
{
    return message == WM_NCLBUTTONDOWN
        && (hitTest == HTCAPTION || (hitTest >= HTLEFT && hitTest <= HTBOTTOMRIGHT));
}

inline bool shouldSuppressSessionResizeDuringWindowInteraction(UINT message, WPARAM hitTest)
{
    return message == WM_NCLBUTTONDOWN && hitTest >= HTLEFT && hitTest <= HTBOTTOMRIGHT;
}

inline bool shouldPersistWindowStateOnSize(UINT sizeType, bool inMoveOrSizeLoop)
{
    return sizeType != SIZE_MINIMIZED && !inMoveOrSizeLoop;
}

inline bool shouldPersistWindowStateOnExitSizeMove(bool inMoveOrSizeLoop)
{
    return inMoveOrSizeLoop;
}
}
