#include <cassert>

#include "ui/MainWindowLayoutBehavior.h"

namespace
{
void assertRect(const ui::LayoutRect &rect, int left, int top, int right, int bottom)
{
    assert(rect.left == left);
    assert(rect.top == top);
    assert(rect.right == right);
    assert(rect.bottom == bottom);
}
}

int main()
{
    assertRect(ui::mainWindowLogoRect(), 0, 0, 38, 34);
    assertRect(ui::mainWindowLogoHoverRect(1), 0, 1, 38, 34);
    assert(ui::mainWindowLogoHitTest({ 0, 0 }, false));
    assert(ui::mainWindowLogoHitTest({ 37, 33 }, false));
    assert(!ui::mainWindowLogoHitTest({ 38, 10 }, false));
    assert(!ui::mainWindowLogoHitTest({ 10, 10 }, true));

    assertRect(ui::mainWindowUpdateButtonRect(800), 624, 0, 662, 34);
    assert(ui::mainWindowCaptionButtonReserveWidth(false) == 138);
    assert(ui::mainWindowCaptionButtonReserveWidth(true) == 176);

    assertRect(ui::mainWindowCaptionButtonRectFor(800, HTCLOSE, false), 754, 0, 800, 34);
    assertRect(ui::mainWindowCaptionButtonRectFor(800, HTMAXBUTTON, false), 708, 0, 754, 34);
    assertRect(ui::mainWindowCaptionButtonRectFor(800, HTMINBUTTON, false), 662, 0, 708, 34);
    assertRect(ui::mainWindowCaptionButtonRectFor(800, ui::kMainWindowUpdateCaptionButtonHit, true),
               624, 0, 662, 34);
    assertRect(ui::mainWindowCaptionButtonRectFor(800, ui::kMainWindowUpdateCaptionButtonHit, false),
               0, 0, 0, 0);

    assert(ui::mainWindowCaptionButtonHitTest({ 799, 0 }, 800, false) == HTCLOSE);
    assert(ui::mainWindowCaptionButtonHitTest({ 753, 10 }, 800, false) == HTMAXBUTTON);
    assert(ui::mainWindowCaptionButtonHitTest({ 707, 10 }, 800, false) == HTMINBUTTON);
    assert(ui::mainWindowCaptionButtonHitTest({ 624, 10 }, 800, true)
           == ui::kMainWindowUpdateCaptionButtonHit);
    assert(ui::mainWindowCaptionButtonHitTest({ 630, 10 }, 800, false) == 0);
    assert(ui::mainWindowCaptionButtonHitTest({ 799, 34 }, 800, true) == 0);
    assert(ui::mainWindowCaptionButtonHitTest({ 800, 10 }, 800, true) == 0);

    assert(ui::mainWindowNonClientHitTest({ 1, 1 }, 800, 600, false, false) == HTTOPLEFT);
    assert(ui::mainWindowNonClientHitTest({ 794, 1 }, 800, 600, false, false) == HTTOPRIGHT);
    assert(ui::mainWindowNonClientHitTest({ 1, 594 }, 800, 600, false, false) == HTBOTTOMLEFT);
    assert(ui::mainWindowNonClientHitTest({ 794, 594 }, 800, 600, false, false) == HTBOTTOMRIGHT);
    assert(ui::mainWindowNonClientHitTest({ 100, 1 }, 800, 600, false, false) == HTTOP);
    assert(ui::mainWindowNonClientHitTest({ 100, 594 }, 800, 600, false, false) == HTBOTTOM);
    assert(ui::mainWindowNonClientHitTest({ 1, 100 }, 800, 600, false, false) == HTLEFT);
    assert(ui::mainWindowNonClientHitTest({ 794, 100 }, 800, 600, false, false) == HTRIGHT);
    assert(ui::mainWindowNonClientHitTest({ 6, 6 }, 800, 600, false, false) == HTCLIENT);
    assert(ui::mainWindowNonClientHitTest({ 1, 1 }, 800, 600, true, false) == HTCLIENT);
    assert(ui::mainWindowNonClientHitTest({ 1, 1 }, 800, 600, false, true) == HTCLIENT);

    {
        const ui::MainWindowChildLayout layout =
            ui::mainWindowChildLayout(800, 600, false, false, false);
        assert(layout.tabBarVisible);
        assertRect(layout.tabBarRect, 38, 1, 662, 33);
        assertRect(layout.sessionHostRect, 1, 34, 799, 599);
    }

    {
        const ui::MainWindowChildLayout layout =
            ui::mainWindowChildLayout(800, 600, false, false, true);
        assert(layout.tabBarVisible);
        assertRect(layout.tabBarRect, 38, 1, 624, 33);
        assertRect(layout.sessionHostRect, 1, 34, 799, 599);
    }

    {
        const ui::MainWindowChildLayout layout =
            ui::mainWindowChildLayout(800, 600, true, false, false);
        assert(layout.tabBarVisible);
        assertRect(layout.tabBarRect, 38, 0, 662, 33);
        assertRect(layout.sessionHostRect, 0, 34, 800, 600);
    }

    {
        const ui::MainWindowChildLayout layout =
            ui::mainWindowChildLayout(800, 600, false, true, true);
        assert(!layout.tabBarVisible);
        assertRect(layout.tabBarRect, 0, 0, 0, 0);
        assertRect(layout.sessionHostRect, 0, 0, 800, 600);
    }

    {
        const ui::MainWindowChildLayout layout =
            ui::mainWindowChildLayout(100, 60, false, false, true);
        assert(layout.tabBarVisible);
        assertRect(layout.tabBarRect, 38, 1, 38, 33);
        assertRect(layout.sessionHostRect, 1, 34, 99, 59);
    }

    return 0;
}
