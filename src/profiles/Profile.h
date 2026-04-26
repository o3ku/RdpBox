#pragma once

#include <string>

#include "common/Win32String.h"

struct Profile
{
    std::string id;
    std::wstring name;
    std::wstring host;
    int port = 3389;
    std::wstring username;
    std::wstring password;
    bool clipboardEnabled = true;
    bool ignoreCertificate = true;

    static Profile create()
    {
        Profile profile;
        profile.id = createGuidString();
        return profile;
    }

    bool isValid() const
    {
        return !id.empty() && !host.empty();
    }
};
