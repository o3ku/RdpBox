#pragma once

#include <algorithm>
#include <cwctype>
#include <string>

#include <objbase.h>
#include <stringapiset.h>

inline std::string utf8FromWide(const std::wstring &text)
{
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<size_t>(size), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    if (written != size)
        return {};
    return result;
}

inline std::wstring wideFromUtf8(const std::string &text)
{
    if (text.empty())
        return {};

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};

    std::wstring result(static_cast<size_t>(size), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    if (written != size)
        return {};
    return result;
}

inline std::wstring lowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

inline std::string createGuidString()
{
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid)))
        return {};

    wchar_t buffer[39] = {};
    if (StringFromGUID2(guid, buffer, 39) == 0)
        return {};

    std::wstring value(buffer);
    if (!value.empty() && value.front() == L'{')
        value.erase(value.begin());
    if (!value.empty() && value.back() == L'}')
        value.pop_back();
    return utf8FromWide(value);
}
