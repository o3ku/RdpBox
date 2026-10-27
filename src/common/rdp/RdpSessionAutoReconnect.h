#pragma once

#include <cstdint>

// How many automatic attempts a session may make in one outage. A manual
// reconnect (overlay click, tab menu, host resume) is not counted and is never
// refused.
inline constexpr std::uint64_t kMaxAutoReconnectAttempts = 3;

// A handshake that never reaches Running must not start another one: this is
// what turned a dead endpoint (and every view reacting to the stale events of
// the attempt it replaced) into a reconnect storm. A session that did reach
// Running, or delivered a first frame, may be recovered automatically.
inline bool canAutoReconnect(bool stateReachedRunning)
{
    return stateReachedRunning;
}

// An attempt the user did not ask for has to be allowed by the gate above and
// must stay within the outage's retry budget. A manual reconnect is always
// honoured: that is the user asking again, even while a stale automatic attempt
// is still in flight.
inline bool shouldStartAutoReconnect(bool autoAttempt, bool stateReachedRunning, std::uint64_t autoAttempts)
{
    if (!autoAttempt)
        return true;

    // The attempt in flight never came up, so there is nothing to retry: the
    // next handshake would fail the same way. Give the overlay back instead.
    if (!stateReachedRunning)
        return false;

    return autoAttempts < kMaxAutoReconnectAttempts;
}

// Only giving up shows the server's own error text; an attempt that will be
// retried says "Reconnecting..." instead of flashing the reason it failed. An
// automatic attempt that spent its budget is a give-up like any other.
inline bool shouldReportDisconnectText(bool autoAttempt, std::uint64_t autoAttempts)
{
    return !autoAttempt || autoAttempts < kMaxAutoReconnectAttempts;
}
