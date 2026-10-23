#include "PasswordProtectionStartup.h"

#include "AppPaths.h"
#include "PasswordProtection.h"

namespace PasswordProtectionStartup
{
bool initialize(bool portableRequested)
{
    // AppPaths already persisted the flag when --portable was parsed; trust the
    // effective mode rather than the request, so a read-only install directory
    // falls back to DPAPI instead of a keyless portable file.
    const bool portable = portableRequested && AppPaths::isPortableMode();

    PasswordProtection::setMode(
        portable ? PasswordProtection::Mode::Portable
                 : PasswordProtection::Mode::Dpapi);
    return PasswordProtection::isReady();
}
}
