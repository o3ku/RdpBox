#include "qtui/QtWindowChromeBehavior.h"

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
                        bool maximized)
{
    Q_UNUSED(maximized);

    if (captionRect.contains(point) && !containsAny(captionExclusionRects, point))
        return HitArea::Caption;

    return HitArea::Client;
}
}
