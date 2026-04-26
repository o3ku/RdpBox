#pragma once

#include "common/NativeTypes.h"

#include <optional>

namespace RdpCursorClassifier
{
std::optional<CursorKind> classifyShape(const FrameBuffer &remoteImage, PointI hotspot);
CursorInfo createCursor(const FrameBuffer &remoteImage, PointI hotspot);
HCURSOR cursorHandleFromInfo(const CursorInfo &cursorInfo);
}
