#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

#include <vector>

namespace qt::chrome
{
// Edge/corner resize hit-testing is owned by frameless::nativeEvent
// (FramelessWin.h kResizeBorder); this enum only decides caption vs client.
enum class HitArea
{
    Client,
    Caption,
};

HitArea hitAreaForPoint(const QPoint &point,
                        const QSize &windowSize,
                        const QRect &captionRect,
                        const std::vector<QRect> &captionExclusionRects,
                        bool maximized);
}
