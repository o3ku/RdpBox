#pragma once

#include <afxwin.h>

namespace ParentResizeForwarder
{
constexpr int kBorderThickness = 6;

// Returns HT* code if the screen point sits on top-level window's resize frame.
// Returns 0 (HTNOWHERE) when not on a frame edge or top-level is maximized/fullscreen.
int hitTestParentFrame(CWnd *child, CPoint screenPoint);

// Sets the appropriate resize cursor for a frame hit code. Returns true if a cursor was set.
bool applyResizeCursor(int hitCode);

// Forwards a left-button press to top-level window for native frame resize.
// Returns true if forwarded (caller should not process the click).
bool forwardLButtonDown(CWnd *child, CPoint screenPoint);
}
