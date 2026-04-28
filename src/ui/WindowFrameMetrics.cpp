#include "WindowFrameMetrics.h"

#include "Win10Theme.h"

WindowFrameMetrics calculateWindowFrameMetrics(bool isMaximized, bool isFullScreen)
{
    WindowFrameMetrics metrics;
    metrics.dwmTopInset = (!isMaximized && !isFullScreen) ? 1 : 0;
    metrics.clientEdgeInset = (!isMaximized && !isFullScreen) ? 1 : 0;
    metrics.drawAccentBorder = !isMaximized && !isFullScreen;
    metrics.backgroundColor = metrics.drawAccentBorder ? Win10Theme::kAccent : Win10Theme::kCaptionBg;
    return metrics;
}
