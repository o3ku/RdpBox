#pragma once

#include <windows.h>

namespace ui
{
constexpr int kMainWindowCaptionHeight = 34;
constexpr int kMainWindowLogoSize = 22;
constexpr int kMainWindowLogoLeftPadding = 8;
constexpr int kMainWindowLogoRightPadding = 8;
constexpr int kMainWindowCaptionButtonWidth = 46;
constexpr int kMainWindowUpdateButtonWidth = 38;
constexpr int kMainWindowResizeBorderWidth = 6;
constexpr int kMainWindowUpdateCaptionButtonHit = 0x4001;

struct LayoutPoint
{
    int x = 0;
    int y = 0;
};

struct LayoutRect
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct MainWindowChildLayout
{
    LayoutRect tabBarRect;
    bool tabBarVisible = true;
    LayoutRect sessionHostRect;
};

bool layoutRectContains(const LayoutRect &rect, LayoutPoint point);

LayoutRect mainWindowLogoRect();
LayoutRect mainWindowLogoHoverRect(int clientEdgeInset);
bool mainWindowLogoHitTest(LayoutPoint clientPoint, bool isFullScreen);

LayoutRect mainWindowUpdateButtonRect(int clientWidth);
int mainWindowCaptionButtonReserveWidth(bool showUpdateButton);
LayoutRect mainWindowCaptionButtonRectFor(int clientWidth, int hitCode, bool showUpdateButton);
int mainWindowCaptionButtonHitTest(LayoutPoint clientPoint, int clientWidth, bool showUpdateButton);

int mainWindowNonClientHitTest(LayoutPoint clientPoint,
                               int clientWidth,
                               int clientHeight,
                               bool isMaximized,
                               bool isFullScreen);

MainWindowChildLayout mainWindowChildLayout(int clientWidth,
                                            int clientHeight,
                                            bool isMaximized,
                                            bool isFullScreen,
                                            bool showUpdateButton);
}
