#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "common/AppPaths.h"
#include "common/PasswordProtection.h"
#include "common/PasswordProtectionStartup.h"

int main()
{
    // First run with --portable and no flag file: the entry point applies the
    // flag first, then picks the mode. Both platforms end up Portable, so the
    // profile is encrypted with the portable key and lives in the exe
    // directory instead of fighting the flag on the next save.
    assert(AppPaths::enablePortableMode());
    assert(PasswordProtectionStartup::initialize());
    assert(AppPaths::isPortableMode());
    assert(!AppPaths::portableModeEnableFailed());
    assert(PasswordProtection::mode() == PasswordProtection::Mode::Portable);
    assert(PasswordProtection::isReady());

    // Repeated initialization converges on the same usable mode instead of
    // flipping between Portable and DPAPI.
    assert(PasswordProtectionStartup::initialize());
    assert(PasswordProtection::mode() == PasswordProtection::Mode::Portable);

    // A password written in portable mode must survive a round trip; a keyless
    // Portable state would seal it as an empty string.
    const std::string sealed = PasswordProtection::protectPortable(L"startup-secret");
    assert(!sealed.empty());
    bool ok = false;
    assert(PasswordProtection::unprotectPortable(sealed, &ok) == L"startup-secret");
    assert(ok);

    std::puts("PasswordProtectionStartupTests passed");
    return 0;
}
