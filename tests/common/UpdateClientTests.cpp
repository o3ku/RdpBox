#include <cassert>

#include "common/UpdateClient.h"

int main()
{
    assert(updater::isNewerReleaseTag(L"v1.9.2", L"v1.9.3"));
    assert(updater::isNewerReleaseTag(L"1.9.2", L"v1.10.0"));
    assert(!updater::isNewerReleaseTag(L"v1.9.2", L"v1.9.2"));
    assert(!updater::isNewerReleaseTag(L"v1.9.3", L"v1.9.2"));
    assert(!updater::isNewerReleaseTag(L"v1.9.3", L"v1.9"));
    return 0;
}
