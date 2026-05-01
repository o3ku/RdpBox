#pragma once

#include <string>

namespace PasswordProtection
{
enum class Mode
{
    Dpapi,
    Portable
};

void setMode(Mode mode);
Mode mode();
bool isReady();

std::string protectDpapi(const std::wstring &plaintext);
std::wstring unprotectDpapi(const std::string &encoded, bool *ok = nullptr);
std::string protectPortable(const std::wstring &plaintext);
std::wstring unprotectPortable(const std::string &encoded, bool *ok = nullptr);
}
