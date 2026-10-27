#include <cassert>

#include "common/rdp/RdpSessionAutoReconnect.h"

int main()
{
    // A manual reconnect is always honoured.
    assert(shouldStartAutoReconnect(false, false, 0));
    assert(shouldStartAutoReconnect(false, false, 42));

    // An automatic one has to see a live session first: this is the case that
    // turns a dead endpoint into a reconnect storm.
    assert(!shouldStartAutoReconnect(true, false, 0));
    assert(!shouldStartAutoReconnect(true, false, 1));
    assert(shouldStartAutoReconnect(true, true, 0));
    assert(shouldStartAutoReconnect(true, true, 2));

    // Backstop for a session that reaches Running but never delivers content.
    assert(!shouldStartAutoReconnect(true, true, 3));

    // Stale state callbacks may not trigger anything while a manual reconnect
    // is still in flight.
    assert(!canAutoReconnect(true, true));
    assert(!canAutoReconnect(false, false));
    assert(canAutoReconnect(false, true));

    // Only a give-up shows the server's own error text; an attempt that will
    // be retried says "Reconnecting...".
    assert(shouldReportDisconnectText(false, 0));
    assert(shouldReportDisconnectText(true, 0));
    assert(shouldReportDisconnectText(true, 2));
    assert(!shouldReportDisconnectText(true, 3));

    return 0;
}
