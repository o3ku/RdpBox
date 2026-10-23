#pragma once

namespace PasswordProtectionStartup
{
// Picks the password protection mode from the requested startup options and
// prepares it. Both UI entry points must call this exactly once, before the
// first ProfileRepository is constructed: the repository encrypts with whatever
// mode is current, and a Portable mode without a derived key drops the password
// on write instead of failing.
//
// Returns false when the mode could not be prepared; the caller should treat
// that as fatal rather than silently persisting unprotected or unreadable data.
bool initialize(bool portableRequested);
}
