#include <cassert>

#include "ui/WindowFrameMetrics.h"
#include "ui/Win10Theme.h"

int main()
{
    {
        const WindowFrameMetrics metrics = calculateWindowFrameMetrics(false, false);
        assert(metrics.dwmTopInset == 1);
        assert(metrics.clientEdgeInset == 1);
        assert(metrics.drawAccentBorder);
        assert(metrics.backgroundColor == Win10Theme::kBrandAccent);
    }

    {
        const WindowFrameMetrics metrics = calculateWindowFrameMetrics(true, false);
        assert(metrics.dwmTopInset == 0);
        assert(metrics.clientEdgeInset == 0);
        assert(!metrics.drawAccentBorder);
        assert(metrics.backgroundColor == Win10Theme::kCaptionBg);
    }

    {
        const WindowFrameMetrics metrics = calculateWindowFrameMetrics(false, true);
        assert(metrics.dwmTopInset == 0);
        assert(metrics.clientEdgeInset == 0);
        assert(!metrics.drawAccentBorder);
        assert(metrics.backgroundColor == Win10Theme::kCaptionBg);
    }

    {
        const WindowFrameMetrics metrics = calculateWindowFrameMetrics(true, true);
        assert(metrics.dwmTopInset == 0);
        assert(metrics.clientEdgeInset == 0);
        assert(!metrics.drawAccentBorder);
        assert(metrics.backgroundColor == Win10Theme::kCaptionBg);
    }

    return 0;
}
