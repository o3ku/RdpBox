#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

#include <vector>

namespace qt::chrome
{
enum class HitArea
{
    Client,
    Caption,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

HitArea hitAreaForPoint(const QPoint &point,
                        const QSize &windowSize,
                        const QRect &captionRect,
                        const std::vector<QRect> &captionExclusionRects,
                        int resizeBorderWidth,
                        bool maximized);

bool isResizeArea(HitArea area);
}
