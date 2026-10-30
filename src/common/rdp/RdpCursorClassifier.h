#pragma once

#include "common/NativeTypes.h"

namespace RdpCursorClassifier
{
CursorInfo createCursor(const FrameBuffer &remoteImage, PointI hotspot);
HCURSOR cursorHandleFromInfo(const CursorInfo &cursorInfo);
// The cursor without the ownership bit, for the handle-free state a view
// remembers (see RdpRemoteCursorState.h).
CursorInfo cursorInfoForState(const CursorInfo &cursorInfo);
}
