#pragma once

#include <cstdint>

// One automatic reconnect may be in flight per session at a time. A failing
// endpoint fails twice: once inside the attempt (the resolution/resume
// recovery timer fired during the handshake) and once after it (the state
// callback it scheduled lands on the UI thread as Finished). Both used to call
// reconnect() unconditionally, which is a reconnect storm, not a recovery.
inline bool canAutoReconnect(bool manualReconnectInFlight, bool stateReachedRunning)
{
    return !manualReconnectInFlight && stateReachedRunning;
}

// An attempt that the user did not ask for has to reach Running before the
// next one is allowed. A manual reconnect (overlay click, tab menu) is always
// honoured: that is the user asking again.
inline bool shouldStartAutoReconnect(bool autoAttempt, bool stateReachedRunning, std::uint64_t autoAttempts)
{
    if (!autoAttempt)
        return true;

    if (!stateReachedRunning)
        return false;

    // Backstop: even a session that connects but never delivers content must
    // not spin the UI thread with handshakes forever.
    return autoAttempts < 3;
}

inline bool shouldReportDisconnectText(bool autoAttempt, std::uint64_t autoAttempts)
{
    return !autoAttempt || autoAttempts < 3;
}
