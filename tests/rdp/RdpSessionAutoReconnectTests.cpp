#include <cassert>

#include "common/rdp/RdpSessionAutoReconnect.h"

int main()
{
    // A resume may only start a handshake while the session it means to bring
    // back is alive: the window this closes is the one where a resume arrives
    // after the reconnect it follows has already begun.
    assert(!shouldAutoReconnect(true, true, false));
    assert(!shouldAutoReconnect(true, false, true));
    assert(!shouldAutoReconnect(true, false, false));
    assert(shouldAutoReconnect(true, true, true));

    // A manual reconnect is the user asking, and is never refused.
    assert(shouldAutoReconnect(false, true, true));
    assert(shouldAutoReconnect(false, true, false));
    assert(shouldAutoReconnect(false, false, false));

    return 0;
}
