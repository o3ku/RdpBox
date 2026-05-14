#include "ui/WindowStateScaling.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMinWindowWidth = 400;
constexpr int kMinWindowHeight = 300;

int rectWidth(const RECT &rect)
{
    return rect.right - rect.left;
}

int rectHeight(const RECT &rect)
{
    return rect.bottom - rect.top;
}

int scaleToPixels(double ratio, int span)
{
    return static_cast<int>(std::lround(ratio * static_cast<double>(span)));
}

int persistedShowCommand(int showCmd)
{
    return showCmd == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : showCmd;
}
}

namespace WindowStateScaling
{
RECT workspaceRectForMonitorWorkArea(const RECT &monitorRect,
                                     const RECT &workAreaRect)
{
    RECT workspaceRect = workAreaRect;

    const int topInset = static_cast<int>(workAreaRect.top - monitorRect.top);
    const int leftInset = static_cast<int>(workAreaRect.left - monitorRect.left);
    const int rightInset = static_cast<int>(monitorRect.right - workAreaRect.right);
    const int bottomInset = static_cast<int>(monitorRect.bottom - workAreaRect.bottom);

    if (topInset > 0) {
        ::OffsetRect(&workspaceRect, 0, -topInset);
    } else if (leftInset > 0) {
        ::OffsetRect(&workspaceRect, -leftInset, 0);
    } else if (rightInset > 0) {
        ::OffsetRect(&workspaceRect, rightInset, 0);
    } else if (bottomInset > 0) {
        ::OffsetRect(&workspaceRect, 0, bottomInset);
    }

    return workspaceRect;
}

bool saveToMonitorWorkArea(const RECT &windowRect,
                           const RECT &workArea,
                           int showCmd,
                           WindowState &state)
{
    const int workWidth = rectWidth(workArea);
    const int workHeight = rectHeight(workArea);
    const int windowWidth = rectWidth(windowRect);
    const int windowHeight = rectHeight(windowRect);
    if (workWidth <= 0 || workHeight <= 0 || windowWidth <= 0 || windowHeight <= 0)
        return false;

    state.leftRatio = static_cast<double>(windowRect.left - workArea.left) / static_cast<double>(workWidth);
    state.topRatio = static_cast<double>(windowRect.top - workArea.top) / static_cast<double>(workHeight);
    state.widthRatio = static_cast<double>(windowWidth) / static_cast<double>(workWidth);
    state.heightRatio = static_cast<double>(windowHeight) / static_cast<double>(workHeight);
    state.showCmd = persistedShowCommand(showCmd);
    state.valid = true;
    return true;
}

bool restoreFromMonitorWorkArea(const WindowState &state,
                                const RECT &workArea,
                                RECT &windowRect)
{
    if (!state.valid)
        return false;

    const int workWidth = rectWidth(workArea);
    const int workHeight = rectHeight(workArea);
    if (workWidth <= 0 || workHeight <= 0)
        return false;

    const int width = std::clamp(scaleToPixels(state.widthRatio, workWidth),
                                 kMinWindowWidth,
                                 workWidth);
    const int height = std::clamp(scaleToPixels(state.heightRatio, workHeight),
                                  kMinWindowHeight,
                                  workHeight);

    const int workLeft = static_cast<int>(workArea.left);
    const int workTop = static_cast<int>(workArea.top);
    const int workRight = static_cast<int>(workArea.right);
    const int workBottom = static_cast<int>(workArea.bottom);

    int left = workLeft + scaleToPixels(state.leftRatio, workWidth);
    int top = workTop + scaleToPixels(state.topRatio, workHeight);
    left = std::clamp(left, workLeft, workRight - width);
    top = std::clamp(top, workTop, workBottom - height);

    windowRect.left = left;
    windowRect.top = top;
    windowRect.right = left + width;
    windowRect.bottom = top + height;
    return true;
}
}
