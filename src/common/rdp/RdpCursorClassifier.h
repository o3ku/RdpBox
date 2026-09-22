#pragma once

#include "common/NativeTypes.h"

namespace RdpCursorClassifier
{
CursorInfo createCursor(const FrameBuffer &remoteImage, PointI hotspot);
HCURSOR cursorHandleFromInfo(const CursorInfo &cursorInfo);
}
