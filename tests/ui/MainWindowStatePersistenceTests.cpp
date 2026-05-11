#include <cassert>

#include "ui/MainWindowStatePersistence.h"

int main()
{
    using ui::shouldPersistWindowStateOnExitSizeMove;
    using ui::shouldPersistWindowStateOnSize;
    using ui::shouldSuppressSessionResizeDuringWindowInteraction;
    using ui::shouldTrackWindowStateInteraction;

    assert(shouldTrackWindowStateInteraction(WM_NCLBUTTONDOWN, HTLEFT));
    assert(shouldTrackWindowStateInteraction(WM_NCLBUTTONDOWN, HTBOTTOMRIGHT));
    assert(shouldTrackWindowStateInteraction(WM_NCLBUTTONDOWN, HTCAPTION));
    assert(!shouldTrackWindowStateInteraction(WM_LBUTTONDOWN, HTLEFT));

    assert(shouldSuppressSessionResizeDuringWindowInteraction(WM_NCLBUTTONDOWN, HTLEFT));
    assert(shouldSuppressSessionResizeDuringWindowInteraction(WM_NCLBUTTONDOWN, HTBOTTOMRIGHT));
    assert(!shouldSuppressSessionResizeDuringWindowInteraction(WM_NCLBUTTONDOWN, HTCAPTION));
    assert(!shouldSuppressSessionResizeDuringWindowInteraction(WM_LBUTTONDOWN, HTLEFT));

    assert(shouldPersistWindowStateOnSize(SIZE_RESTORED, false));
    assert(shouldPersistWindowStateOnSize(SIZE_MAXIMIZED, false));
    assert(!shouldPersistWindowStateOnSize(SIZE_MINIMIZED, false));
    assert(!shouldPersistWindowStateOnSize(SIZE_RESTORED, true));

    assert(shouldPersistWindowStateOnExitSizeMove(true));
    assert(!shouldPersistWindowStateOnExitSizeMove(false));

    return 0;
}
