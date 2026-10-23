#pragma once

namespace PasswordProtectionStartup
{
// Picks the password protection mode from the effective data root and prepares
// it. Both UI entry points must call this exactly once, after --portable has
// been applied (AppPaths::enablePortableMode()) and before the first
// ProfileRepository is constructed: the repository encrypts with whatever mode
// is current, and a Portable mode without a derived key drops the password on
// write instead of failing.
//
// The mode follows isPortableMode() rather than the raw request, so a read-only
// install directory falls back to DPAPI instead of a keyless portable file.
// Off Windows there is no DPAPI at all, so Portable is the only workable mode.
//
// Returns false when no usable mode could be prepared. That is not fatal: the
// caller keeps starting up and lets the save-time warning report the dropped
// passwords.
bool initialize();
}
