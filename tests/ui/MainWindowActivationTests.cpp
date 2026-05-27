#include <cassert>

#include "ui/MainWindowActivation.h"

int main()
{
    using ui::shouldFocusActiveSessionOnActivate;

    assert(!shouldFocusActiveSessionOnActivate(WA_INACTIVE, false));
    assert(!shouldFocusActiveSessionOnActivate(WA_ACTIVE, true));
    assert(shouldFocusActiveSessionOnActivate(WA_ACTIVE, false));
    assert(shouldFocusActiveSessionOnActivate(WA_CLICKACTIVE, false));
    assert(!shouldFocusActiveSessionOnActivate(42, false));
    assert(!shouldFocusActiveSessionOnActivate(WA_CLICKACTIVE, true));

    return 0;
}
