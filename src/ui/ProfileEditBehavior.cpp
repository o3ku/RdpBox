#include "ProfileEditBehavior.h"

ProfileEditValidationResult validateProfileEditFields(const std::wstring &name,
                                                      const std::wstring &host)
{
    if (name.empty() || host.empty())
        return ProfileEditValidationResult::MissingRequiredField;

    if (name.find(L',') != std::wstring::npos || name.find(L'"') != std::wstring::npos)
        return ProfileEditValidationResult::InvalidNameCharacter;

    return ProfileEditValidationResult::Valid;
}

int normalizedProfilePort(int port)
{
    if (port < 1 || port > 65535)
        return 3389;
    return port;
}
