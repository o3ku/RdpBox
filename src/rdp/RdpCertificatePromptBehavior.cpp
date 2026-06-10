#include "RdpCertificatePromptBehavior.h"

namespace rdp::certificate_prompt
{
Prompt promptForChallenge(const Challenge &challenge)
{
    Prompt prompt;
    prompt.icon = challenge.changed ? PromptIcon::Warning : PromptIcon::Question;

    prompt.message = challenge.changed
        ? L"The remote host's certificate has CHANGED since the previous connection."
        : L"The remote host's certificate could not be verified.";
    prompt.message += L"\n\nHost: ";
    prompt.message += challenge.host;
    prompt.message += L":";
    prompt.message += std::to_wstring(challenge.port);
    prompt.message += L"\nCommon Name: ";
    prompt.message += challenge.commonName;
    prompt.message += L"\nSubject: ";
    prompt.message += challenge.subject;
    prompt.message += L"\nIssuer: ";
    prompt.message += challenge.issuer;
    prompt.message += L"\nFingerprint: ";
    prompt.message += challenge.fingerprint;
    prompt.message += L"\n\nAccept this certificate?";

    return prompt;
}
}
