#include <cassert>
#include <string>

#include "common/PasswordProtection.h"

int main()
{
    {
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
        assert(PasswordProtection::mode() == PasswordProtection::Mode::Dpapi);
        assert(PasswordProtection::isReady());

        const std::wstring secret = L"dpapi-secret-你好";
        const std::string encoded = PasswordProtection::protectDpapi(secret);
        assert(!encoded.empty());

        bool ok = false;
        const std::wstring decoded = PasswordProtection::unprotectDpapi(encoded, &ok);
        assert(ok);
        assert(decoded == secret);

        ok = true;
        assert(PasswordProtection::protectDpapi(L"").empty());
        assert(PasswordProtection::unprotectDpapi("", &ok).empty());
        assert(!ok);

        ok = true;
        assert(PasswordProtection::unprotectDpapi("not-base64", &ok).empty());
        assert(!ok);

        ok = true;
        assert(PasswordProtection::unprotectDpapi("AQIDBA==", &ok).empty());
        assert(!ok);
    }

    {
        PasswordProtection::setMode(PasswordProtection::Mode::Portable);
        assert(PasswordProtection::mode() == PasswordProtection::Mode::Portable);
        assert(PasswordProtection::isReady());

        const std::wstring secret = L"portable-secret-你好";
        const std::string encodedA = PasswordProtection::protectPortable(secret);
        const std::string encodedB = PasswordProtection::protectPortable(secret);
        assert(!encodedA.empty());
        assert(!encodedB.empty());
        assert(encodedA != encodedB);

        bool ok = false;
        const std::wstring decodedA = PasswordProtection::unprotectPortable(encodedA, &ok);
        assert(ok);
        assert(decodedA == secret);

        ok = false;
        const std::wstring decodedB = PasswordProtection::unprotectPortable(encodedB, &ok);
        assert(ok);
        assert(decodedB == secret);

        ok = true;
        assert(PasswordProtection::protectPortable(L"").empty());
        assert(PasswordProtection::unprotectPortable("", &ok).empty());
        assert(!ok);

        ok = true;
        assert(PasswordProtection::unprotectPortable("not-base64", &ok).empty());
        assert(!ok);

        std::string tampered = encodedA;
        tampered.back() = (tampered.back() == 'A') ? 'B' : 'A';
        ok = true;
        assert(PasswordProtection::unprotectPortable(tampered, &ok).empty());
        assert(!ok);
    }

    PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    return 0;
}
