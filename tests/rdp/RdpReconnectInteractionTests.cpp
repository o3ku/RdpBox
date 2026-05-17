#include <cassert>

#include "rdp/RdpReconnectInteraction.h"

int main()
{
    assert(shouldReconnectOnPointerDown(true, false, false, false));
    assert(shouldReconnectOnPointerDown(true, false, true, true));

    assert(!shouldReconnectOnPointerDown(false, false, false, false));
    assert(!shouldReconnectOnPointerDown(true, true, true, true));
    assert(!shouldReconnectOnPointerDown(true, false, true, false));

    return 0;
}
