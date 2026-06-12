#include "qt/QtWindowChromeBehavior.h"

namespace qt::chrome
{
namespace
{
bool containsAny(const std::vector<QRect> &rects, const QPoint &point)
{
    for (const QRect &rect : rects) {
        if (rect.contains(point))
            return true;
    }
    return false;
}
}

HitArea hitAreaForPoint(const QPoint &point,
                        const QSize &windowSize,
                        const QRect &captionRect,
                        const std::vector<QRect> &captionExclusionRects,
                        int resizeBorderWidth,
                        bool maximized)
{
    if (!maximized && resizeBorderWidth > 0) {
        const bool onLeft = point.x() >= 0 && point.x() < resizeBorderWidth;
        const bool onRight = point.x() < windowSize.width() && point.x() >= windowSize.width() - resizeBorderWidth;
        const bool onTop = point.y() >= 0 && point.y() < resizeBorderWidth;
        const bool onBottom = point.y() < windowSize.height() && point.y() >= windowSize.height() - resizeBorderWidth;

        if (onTop && onLeft)
            return HitArea::TopLeft;
        if (onTop && onRight)
            return HitArea::TopRight;
        if (onBottom && onLeft)
            return HitArea::BottomLeft;
        if (onBottom && onRight)
            return HitArea::BottomRight;
        if (onLeft)
            return HitArea::Left;
        if (onRight)
            return HitArea::Right;
        if (onTop)
            return HitArea::Top;
        if (onBottom)
            return HitArea::Bottom;
    }

    if (captionRect.contains(point) && !containsAny(captionExclusionRects, point))
        return HitArea::Caption;

    return HitArea::Client;
}

bool isResizeArea(HitArea area)
{
    switch (area) {
    case HitArea::Left:
    case HitArea::Right:
    case HitArea::Top:
    case HitArea::Bottom:
    case HitArea::TopLeft:
    case HitArea::TopRight:
    case HitArea::BottomLeft:
    case HitArea::BottomRight:
        return true;
    default:
        return false;
    }
}
}
