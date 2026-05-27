#include <cassert>

#include "rdp/RdpReconnectInteraction.h"

int main()
{
    assert(shouldReconnectOnPointerDown(true, false, false, false, false));
    assert(shouldReconnectOnPointerDown(true, false, true, true, false));
    assert(shouldReconnectOnPointerDown(true, true, true, false, true));
    assert(shouldReconnectOnPointerDown(true, false, false, false, true));
    assert(shouldReconnectOnPointerDown(true, false, true, false, true));

    assert(!shouldReconnectOnPointerDown(false, false, false, false, false));
    assert(!shouldReconnectOnPointerDown(false, true, true, false, true));
    assert(!shouldReconnectOnPointerDown(true, true, true, true, false));
    assert(!shouldReconnectOnPointerDown(true, true, false, false, false));
    assert(!shouldReconnectOnPointerDown(true, false, true, false, false));

    return 0;
}
