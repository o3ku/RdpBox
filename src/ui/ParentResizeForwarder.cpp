#include "ParentResizeForwarder.h"

namespace ParentResizeForwarder
{
namespace
{
HWND topLevelOf(CWnd *child)
{
    if (!child || !child->GetSafeHwnd())
        return nullptr;
    return ::GetAncestor(child->GetSafeHwnd(), GA_ROOT);
}

bool isTopLevelMaximizedOrFullscreen(HWND topLevel)
{
    if (!topLevel)
        return true;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (::GetWindowPlacement(topLevel, &placement) && placement.showCmd == SW_SHOWMAXIMIZED)
        return true;

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    HMONITOR monitor = ::MonitorFromWindow(topLevel, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !::GetMonitorInfoW(monitor, &info))
        return false;

    RECT windowRect = {};
    if (!::GetWindowRect(topLevel, &windowRect))
        return false;

    return windowRect.left == info.rcMonitor.left &&
           windowRect.top == info.rcMonitor.top &&
           windowRect.right == info.rcMonitor.right &&
           windowRect.bottom == info.rcMonitor.bottom;
}
}

int hitTestParentFrame(CWnd *child, CPoint screenPoint)
{
    HWND topLevel = topLevelOf(child);
    if (!topLevel || isTopLevelMaximizedOrFullscreen(topLevel))
        return 0;

    RECT topRect = {};
    if (!::GetWindowRect(topLevel, &topRect))
        return 0;

    const int border = kBorderThickness;
    const bool nearLeft = screenPoint.x >= topRect.left && screenPoint.x < topRect.left + border;
    const bool nearRight = screenPoint.x < topRect.right && screenPoint.x >= topRect.right - border;
    const bool nearTop = screenPoint.y >= topRect.top && screenPoint.y < topRect.top + border;
    const bool nearBottom = screenPoint.y < topRect.bottom && screenPoint.y >= topRect.bottom - border;

    if (nearTop && nearLeft) return HTTOPLEFT;
    if (nearTop && nearRight) return HTTOPRIGHT;
    if (nearBottom && nearLeft) return HTBOTTOMLEFT;
    if (nearBottom && nearRight) return HTBOTTOMRIGHT;
    if (nearTop) return HTTOP;
    if (nearBottom) return HTBOTTOM;
    if (nearLeft) return HTLEFT;
    if (nearRight) return HTRIGHT;
    return 0;
}

bool forwardLButtonDown(CWnd *child, CPoint screenPoint)
{
    HWND topLevel = topLevelOf(child);
    if (!topLevel)
        return false;

    const int hit = hitTestParentFrame(child, screenPoint);
    if (!hit)
        return false;

    ::ReleaseCapture();
    ::SendMessageW(topLevel, WM_NCLBUTTONDOWN, static_cast<WPARAM>(hit),
                   MAKELPARAM(screenPoint.x, screenPoint.y));
    return true;
}
}
