#include <cassert>

#include <windows.h>

#include <winhttp.h>

#include "common/UpdateClient.h"

int main()
{
    assert(updater::isNewerReleaseTag(L"v1.9.2", L"v1.9.3"));
    assert(updater::isNewerReleaseTag(L"1.9.2", L"v1.10.0"));
    assert(!updater::isNewerReleaseTag(L"v1.9.2", L"v1.9.2"));
    assert(!updater::isNewerReleaseTag(L"v1.9.3", L"v1.9.2"));
    assert(!updater::isNewerReleaseTag(L"v1.9.3", L"v1.9"));

    // Proxy mode -> WinHttpOpen access type, plus config round-trip.
    updater::ProxyConfig proxy;
    assert(updater::winhttpAccessTypeFor(proxy) == WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY);
    proxy.mode = updater::ProxyMode::None;
    assert(updater::winhttpAccessTypeFor(proxy) == WINHTTP_ACCESS_TYPE_NO_PROXY);
    proxy.mode = updater::ProxyMode::Manual;
    assert(updater::winhttpAccessTypeFor(proxy) == WINHTTP_ACCESS_TYPE_NAMED_PROXY);
    proxy.manualProxy = L"127.0.0.1:7890";
    updater::setCurrentProxyConfig(proxy);
    assert(updater::currentProxyConfig().mode == updater::ProxyMode::Manual);
    assert(updater::currentProxyConfig().manualProxy == L"127.0.0.1:7890");
    updater::setCurrentProxyConfig(updater::ProxyConfig());
    assert(updater::currentProxyConfig().mode == updater::ProxyMode::System);
    return 0;
}
