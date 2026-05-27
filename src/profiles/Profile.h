#pragma once

#include <string>

struct Profile
{
    std::wstring name;
    std::wstring host;
    int port = 3389;
    std::wstring username;
    std::wstring password;
    std::wstring domain;
    bool clipboardEnabled = true;
    bool ignoreCertificate = false;
    bool fullScreenOnConnect = false;
    std::string lastConnectedAt;

    static Profile create()
    {
        return {};
    }

    bool isValid() const
    {
        return !name.empty() && !host.empty();
    }
};
