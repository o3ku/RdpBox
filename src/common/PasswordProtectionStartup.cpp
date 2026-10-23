#include "PasswordProtectionStartup.h"

#include "AppPaths.h"
#include "PasswordProtection.h"

namespace PasswordProtectionStartup
{
bool initialize()
{
#ifdef _WIN32
    // The flag is already persisted (or already failed) by the entry point at
    // this stage; trust the effective mode, not the raw --portable request.
    const PasswordProtection::Mode mode = AppPaths::isPortableMode()
        ? PasswordProtection::Mode::Portable
        : PasswordProtection::Mode::Dpapi;
#else
    // No DPAPI off Windows: protectDpapi() forwards to protectPortable(), which
    // needs a key that only Mode::Portable derives. Dpapi here would silently
    // seal every password as an empty string while isReady() stayed true.
    const PasswordProtection::Mode mode = PasswordProtection::Mode::Portable;
#endif

    PasswordProtection::setMode(mode);
    return PasswordProtection::isReady();
}
}
