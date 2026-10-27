#pragma once

// Whether a session view may start a reconnect the user did not ask for.
//
// Nothing in a view retries on its own any more: the recovery timers outlive
// the handshake they were started for, and what arms them belongs to a live
// session (a resize the remote desktop refused). What the session itself
// reports ends it; the way back is the pointer/click path, which the user
// drives. One unattended attempt is left - the one a host resume asks for - and
// it must not be handed a second one while the first is still in flight.
inline bool shouldAutoReconnect(bool autoAttempt, bool hasProcess, bool sessionAlive)
{
    if (!autoAttempt)
        return true;

    return hasProcess && sessionAlive;
}
