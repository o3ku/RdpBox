#include <cassert>

#include "rdp/RdpFocusNotification.h"

int main()
{
    using rdp::shouldSendFocusIn;

    assert(shouldSendFocusIn(true, true, true) == false);
    assert(shouldSendFocusIn(false, false, true) == true);
    assert(shouldSendFocusIn(true, false, true) == true);
    assert(shouldSendFocusIn(false, true, true) == true);
    assert(shouldSendFocusIn(false, false, false) == false);

    return 0;
}
