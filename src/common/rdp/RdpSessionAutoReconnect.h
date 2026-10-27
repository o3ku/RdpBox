#pragma once

#include <cstdint>

// How many automatic attempts a session may make in one outage. A manual
// reconnect (overlay click, tab menu) is not counted and is never refused.
inline constexpr std::uint64_t kMaxAutoReconnectAttempts = 3;

// A handshake that never reaches Running must not start another one: this is
// what turned a dead endpoint into a reconnect storm. A session that did reach
// Running (or delivered a first frame) may be recovered automatically.
inline bool canAutoReconnect(bool stateReachedRunning)
{
    return stateReachedRunning;
}

// An attempt the user did not ask for has to be allowed by the gates above and
// must stay within the outage's retry budget. A manual reconnect is always
// honoured: that is the user asking again, even while a stale automatic attempt
// is still in flight.
inline bool shouldStartAutoReconnect(bool autoAttempt, bool stateReachedRunning, std::uint64_t autoAttempts)
{
    if (!autoAttempt)
        return true;

    if (!stateReachedRunning)
        return false;

    // Backstop: even a session that connects but never delivers content must
    // not spin the UI thread with handshakes forever.
    return autoAttempts < kMaxAutoReconnectAttempts;
}

// Only a give-up shows the server's own error text; an attempt that will be
// retried says "Reconnecting..." instead of flashing the reason it failed.
inline bool shouldReportDisconnectText(bool autoAttempt, std::uint64_t autoAttempts)
{
    return !autoAttempt || autoAttempts < kMaxAutoReconnectAttempts;
}
