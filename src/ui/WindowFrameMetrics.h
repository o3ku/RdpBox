#pragma once

#include <windows.h>

struct WindowFrameMetrics
{
    int dwmTopInset = 0;
    int clientEdgeInset = 0;
    bool drawAccentBorder = false;
    COLORREF backgroundColor = RGB(0, 0, 0);
};

WindowFrameMetrics calculateWindowFrameMetrics(bool isMaximized, bool isFullScreen);
