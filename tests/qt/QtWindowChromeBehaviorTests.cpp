#include "qtui/QtWindowChromeBehavior.h"

#include <cassert>
#include <vector>

using qt::chrome::HitArea;

int main()
{
    const QSize windowSize(800, 600);
    const QRect captionRect(0, 0, 800, 40);
    const std::vector<QRect> buttons = {QRect(700, 0, 100, 40)};

    // Edge/corner resize hit-testing is owned by frameless::nativeEvent, so
    // every interior point (including former border zones) is caption/client.
    assert(qt::chrome::hitAreaForPoint(QPoint(2, 2), windowSize, captionRect, buttons, false)
        == HitArea::Caption);
    assert(qt::chrome::hitAreaForPoint(QPoint(797, 2), windowSize, captionRect, buttons, false)
        == HitArea::Client); // inside the button exclusion rect
    assert(qt::chrome::hitAreaForPoint(QPoint(2, 597), windowSize, captionRect, buttons, false)
        == HitArea::Client);
    assert(qt::chrome::hitAreaForPoint(QPoint(797, 597), windowSize, captionRect, buttons, false)
        == HitArea::Client);
    assert(qt::chrome::hitAreaForPoint(QPoint(2, 300), windowSize, captionRect, buttons, false)
        == HitArea::Client);
    assert(qt::chrome::hitAreaForPoint(QPoint(400, 2), windowSize, captionRect, buttons, false)
        == HitArea::Caption);

    assert(qt::chrome::hitAreaForPoint(QPoint(120, 20), windowSize, captionRect, buttons, false)
        == HitArea::Caption);
    assert(qt::chrome::hitAreaForPoint(QPoint(730, 20), windowSize, captionRect, buttons, false)
        == HitArea::Client);
    assert(qt::chrome::hitAreaForPoint(QPoint(120, 80), windowSize, captionRect, buttons, false)
        == HitArea::Client);

    assert(qt::chrome::hitAreaForPoint(QPoint(2, 2), windowSize, captionRect, buttons, true)
        == HitArea::Caption);

    return 0;
}
