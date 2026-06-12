#include "qt/QtWindowChromeBehavior.h"

#include <cassert>
#include <vector>

using qt::chrome::HitArea;

int main()
{
    const QSize windowSize(800, 600);
    const QRect captionRect(0, 0, 800, 40);
    const std::vector<QRect> buttons = {QRect(700, 0, 100, 40)};

    assert(qt::chrome::hitAreaForPoint(QPoint(2, 2), windowSize, captionRect, buttons, 6, false)
        == HitArea::TopLeft);
    assert(qt::chrome::hitAreaForPoint(QPoint(797, 2), windowSize, captionRect, buttons, 6, false)
        == HitArea::TopRight);
    assert(qt::chrome::hitAreaForPoint(QPoint(2, 597), windowSize, captionRect, buttons, 6, false)
        == HitArea::BottomLeft);
    assert(qt::chrome::hitAreaForPoint(QPoint(797, 597), windowSize, captionRect, buttons, 6, false)
        == HitArea::BottomRight);
    assert(qt::chrome::hitAreaForPoint(QPoint(2, 300), windowSize, captionRect, buttons, 6, false)
        == HitArea::Left);
    assert(qt::chrome::hitAreaForPoint(QPoint(797, 300), windowSize, captionRect, buttons, 6, false)
        == HitArea::Right);
    assert(qt::chrome::hitAreaForPoint(QPoint(400, 2), windowSize, captionRect, buttons, 6, false)
        == HitArea::Top);
    assert(qt::chrome::hitAreaForPoint(QPoint(400, 597), windowSize, captionRect, buttons, 6, false)
        == HitArea::Bottom);

    assert(qt::chrome::hitAreaForPoint(QPoint(120, 20), windowSize, captionRect, buttons, 6, false)
        == HitArea::Caption);
    assert(qt::chrome::hitAreaForPoint(QPoint(730, 20), windowSize, captionRect, buttons, 6, false)
        == HitArea::Client);
    assert(qt::chrome::hitAreaForPoint(QPoint(120, 80), windowSize, captionRect, buttons, 6, false)
        == HitArea::Client);

    assert(qt::chrome::hitAreaForPoint(QPoint(2, 2), windowSize, captionRect, buttons, 6, true)
        == HitArea::Caption);
    assert(qt::chrome::isResizeArea(HitArea::BottomRight));
    assert(!qt::chrome::isResizeArea(HitArea::Caption));

    return 0;
}
