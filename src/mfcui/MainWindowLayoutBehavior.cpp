#include "MainWindowLayoutBehavior.h"

#include "WindowFrameMetrics.h"

#include <algorithm>

namespace ui
{
namespace
{
constexpr int kCaptionButtonCount = 3;

int standardCaptionButtonOrder(int hitCode)
{
    if (hitCode == HTCLOSE)
        return 0;
    if (hitCode == HTMAXBUTTON)
        return 1;
    if (hitCode == HTMINBUTTON)
        return 2;
    return -1;
}
}

bool layoutRectContains(const LayoutRect &rect, LayoutPoint point)
{
    return point.x >= rect.left && point.x < rect.right
        && point.y >= rect.top && point.y < rect.bottom;
}

LayoutRect mainWindowLogoRect()
{
    return { 0,
             0,
             kMainWindowLogoLeftPadding + kMainWindowLogoSize + kMainWindowLogoRightPadding,
             kMainWindowCaptionHeight };
}

LayoutRect mainWindowLogoHoverRect(int clientEdgeInset)
{
    return { 0,
             clientEdgeInset,
             kMainWindowLogoLeftPadding + kMainWindowLogoSize + kMainWindowLogoRightPadding,
             kMainWindowCaptionHeight };
}

bool mainWindowLogoHitTest(LayoutPoint clientPoint, bool isFullScreen)
{
    return !isFullScreen && layoutRectContains(mainWindowLogoRect(), clientPoint);
}

LayoutRect mainWindowUpdateButtonRect(int clientWidth)
{
    const int right = clientWidth - kMainWindowCaptionButtonWidth * kCaptionButtonCount;
    return { right - kMainWindowUpdateButtonWidth,
             0,
             right,
             kMainWindowCaptionHeight };
}

int mainWindowCaptionButtonReserveWidth(bool showUpdateButton)
{
    return kMainWindowCaptionButtonWidth * kCaptionButtonCount
        + (showUpdateButton ? kMainWindowUpdateButtonWidth : 0);
}

LayoutRect mainWindowCaptionButtonRectFor(int clientWidth, int hitCode, bool showUpdateButton)
{
    if (hitCode == kMainWindowUpdateCaptionButtonHit && showUpdateButton)
        return mainWindowUpdateButtonRect(clientWidth);

    const int order = standardCaptionButtonOrder(hitCode);
    if (order < 0)
        return {};

    const int right = clientWidth - order * kMainWindowCaptionButtonWidth;
    return { right - kMainWindowCaptionButtonWidth,
             0,
             right,
             kMainWindowCaptionHeight };
}

int mainWindowCaptionButtonHitTest(LayoutPoint clientPoint, int clientWidth, bool showUpdateButton)
{
    if (clientPoint.y < 0 || clientPoint.y >= kMainWindowCaptionHeight)
        return 0;

    if (showUpdateButton && layoutRectContains(mainWindowUpdateButtonRect(clientWidth), clientPoint))
        return kMainWindowUpdateCaptionButtonHit;
    if (layoutRectContains(mainWindowCaptionButtonRectFor(clientWidth, HTCLOSE, showUpdateButton), clientPoint))
        return HTCLOSE;
    if (layoutRectContains(mainWindowCaptionButtonRectFor(clientWidth, HTMAXBUTTON, showUpdateButton), clientPoint))
        return HTMAXBUTTON;
    if (layoutRectContains(mainWindowCaptionButtonRectFor(clientWidth, HTMINBUTTON, showUpdateButton), clientPoint))
        return HTMINBUTTON;
    return 0;
}

int mainWindowNonClientHitTest(LayoutPoint clientPoint,
                               int clientWidth,
                               int clientHeight,
                               bool isMaximized,
                               bool isFullScreen)
{
    if (isFullScreen)
        return HTCLIENT;

    if (!isMaximized) {
        const bool nearLeft = clientPoint.x < kMainWindowResizeBorderWidth;
        const bool nearRight = clientPoint.x >= clientWidth - kMainWindowResizeBorderWidth;
        const bool nearTop = clientPoint.y < kMainWindowResizeBorderWidth;
        const bool nearBottom = clientPoint.y >= clientHeight - kMainWindowResizeBorderWidth;

        if (nearTop && nearLeft)
            return HTTOPLEFT;
        if (nearTop && nearRight)
            return HTTOPRIGHT;
        if (nearBottom && nearLeft)
            return HTBOTTOMLEFT;
        if (nearBottom && nearRight)
            return HTBOTTOMRIGHT;
        if (nearTop)
            return HTTOP;
        if (nearBottom)
            return HTBOTTOM;
        if (nearLeft)
            return HTLEFT;
        if (nearRight)
            return HTRIGHT;
    }

    return HTCLIENT;
}

MainWindowChildLayout mainWindowChildLayout(int clientWidth,
                                            int clientHeight,
                                            bool isMaximized,
                                            bool isFullScreen,
                                            bool showUpdateButton)
{
    MainWindowChildLayout layout;
    if (isFullScreen) {
        layout.tabBarVisible = false;
        layout.sessionHostRect = { 0, 0, clientWidth, clientHeight };
        return layout;
    }

    const WindowFrameMetrics metrics = calculateWindowFrameMetrics(isMaximized, false);
    const int inset = metrics.clientEdgeInset;
    const int tabLeft = kMainWindowLogoLeftPadding + kMainWindowLogoSize + kMainWindowLogoRightPadding;
    const int tabRight = std::max(tabLeft, clientWidth - mainWindowCaptionButtonReserveWidth(showUpdateButton));

    layout.tabBarVisible = true;
    layout.tabBarRect = { tabLeft,
                          inset,
                          tabRight,
                          inset + std::max(0, kMainWindowCaptionHeight - inset - 1) };
    layout.sessionHostRect = { inset,
                               kMainWindowCaptionHeight,
                               inset + std::max(0, clientWidth - 2 * inset),
                               kMainWindowCaptionHeight
                                   + std::max(0, clientHeight - kMainWindowCaptionHeight - inset) };
    return layout;
}
}
