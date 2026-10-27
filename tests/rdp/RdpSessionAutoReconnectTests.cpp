#include <cassert>

#include "common/rdp/RdpSessionAutoReconnect.h"

int main()
{
    // A manual reconnect is always honoured: the user asking again, even while
    // a stale automatic attempt is still in flight or the retry budget is out.
    assert(shouldStartAutoReconnect(false, false, 0));
    assert(shouldStartAutoReconnect(false, false, 42));
    assert(shouldStartAutoReconnect(false, true, 42));

    // An automatic one has to see a live session first: this is the case that
    // turns a dead endpoint into a reconnect storm.
    assert(!shouldStartAutoReconnect(true, false, 0));
    assert(!shouldStartAutoReconnect(true, false, 1));
    assert(shouldStartAutoReconnect(true, true, 0));
    assert(shouldStartAutoReconnect(true, true, 2));

    // Backstop for a session that reaches Running but never delivers content.
    assert(!shouldStartAutoReconnect(true, true, 3));

    // Nothing automatic starts while the last handshake has not reached Running,
    // whether or not this view started it.
    assert(!canAutoReconnect(false));
    assert(canAutoReconnect(true));

    // Only a give-up shows the server's own error text; an attempt that will
    // be retried says "Reconnecting...". A manual attempt shows the reason it
    // failed (the user asked for it), and an automatic one that spent its
    // budget is a give-up like any other.
    assert(shouldReportDisconnectText(false, 0));
    assert(shouldReportDisconnectText(false, 3));
    assert(!shouldReportDisconnectText(true, 0));
    assert(!shouldReportDisconnectText(true, 2));
    assert(shouldReportDisconnectText(true, 3));

    return 0;
}
