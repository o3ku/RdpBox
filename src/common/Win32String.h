#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <objbase.h>
#include <stringapiset.h>
#include <windows.h>

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

inline std::string currentUtcIso8601()
{
    SYSTEMTIME st = {};
    GetSystemTime(&st);
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    return buffer;
}
#else
#include <chrono>
#include <codecvt>
#include <ctime>
#include <locale>
#include <random>
#include <sstream>

// POSIX: plain std implementations for the Win32 helpers.
inline std::string utf8FromWide(const std::wstring &text)
{
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>().to_bytes(text);
}

inline std::wstring wideFromUtf8(const std::string &text)
{
    return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>().from_bytes(text);
}

inline std::string createGuidString()
{
    std::random_device rd;
    std::uniform_int_distribution<unsigned> dist(0, 0xFFFFFFFFu);
    const unsigned a = dist(rd), b = dist(rd), c = dist(rd), d = dist(rd);
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "%08x-%04x-%04x-%04x-%04x%08x",
                  a, b >> 16, (b & 0xFFFF) | 0x4000,
                  (c >> 16) | 0x8000, c & 0xFFFF, d);
    return buffer;
}

inline std::string currentUtcIso8601()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm = {};
    gmtime_r(&now, &tm);
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}
#endif
