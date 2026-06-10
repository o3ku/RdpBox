#include <cassert>

#include "rdp/RdpCertificatePromptBehavior.h"

namespace
{
rdp::certificate_prompt::Challenge challenge(bool changed)
{
    rdp::certificate_prompt::Challenge result;
    result.host = L"rdp.example.test";
    result.port = 3389;
    result.commonName = L"rdp.example.test";
    result.subject = L"CN=rdp.example.test";
    result.issuer = L"CN=Test CA";
    result.fingerprint = L"AA:BB:CC";
    result.changed = changed;
    return result;
}
}

int main()
{
    {
        const auto prompt = rdp::certificate_prompt::promptForChallenge(challenge(false));
        assert(prompt.icon == rdp::certificate_prompt::PromptIcon::Question);
        assert(prompt.message.find(L"could not be verified") != std::wstring::npos);
        assert(prompt.message.find(L"Host: rdp.example.test:3389") != std::wstring::npos);
        assert(prompt.message.find(L"Common Name: rdp.example.test") != std::wstring::npos);
        assert(prompt.message.find(L"Subject: CN=rdp.example.test") != std::wstring::npos);
        assert(prompt.message.find(L"Issuer: CN=Test CA") != std::wstring::npos);
        assert(prompt.message.find(L"Fingerprint: AA:BB:CC") != std::wstring::npos);
        assert(prompt.message.find(L"Accept this certificate?") != std::wstring::npos);
    }

    {
        const auto prompt = rdp::certificate_prompt::promptForChallenge(challenge(true));
        assert(prompt.icon == rdp::certificate_prompt::PromptIcon::Warning);
        assert(prompt.message.find(L"certificate has CHANGED") != std::wstring::npos);
        assert(prompt.message.find(L"Host: rdp.example.test:3389") != std::wstring::npos);
        assert(prompt.message.find(L"Accept this certificate?") != std::wstring::npos);
    }

    return 0;
}
