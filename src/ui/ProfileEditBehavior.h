#pragma once

#include <string>

enum class ProfileEditValidationResult
{
    Valid,
    MissingRequiredField,
    InvalidNameCharacter,
};

ProfileEditValidationResult validateProfileEditFields(const std::wstring &name,
                                                      const std::wstring &host);

int normalizedProfilePort(int port);
