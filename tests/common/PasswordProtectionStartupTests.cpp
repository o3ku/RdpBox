
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "common/PasswordProtectionStartup.h"

int main()
{
    assert(PasswordProtectionStartup::initialize(false));
    assert(PasswordProtectionStartup::initialize(false));
    assert(PasswordProtectionStartup::initialize(true));
    assert(PasswordProtectionStartup::initialize(false));
    std::puts("PasswordProtectionStartupTests passed");
    return 0;
}
