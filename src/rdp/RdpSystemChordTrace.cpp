#include "rdp/RdpSystemChordTrace.h"

#include <windows.h>

#include <cwchar>
#include <mutex>
#include <string>

namespace
{
constexpr bool kEnableSystemChordTrace = true;
std::mutex g_traceMutex;

std::wstring traceFilePath()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath))
        return L"RdpSystemChord.log";

    std::wstring path(modulePath, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return L"RdpSystemChord.log";

    path.resize(slash + 1);
    path += L"RdpSystemChord.log";
    return path;
}

const wchar_t *messageName(std::uint32_t message)
{
    switch (message) {
    case 0:
        return L"-";
    case WM_KEYDOWN:
        return L"WM_KEYDOWN";
    case WM_KEYUP:
        return L"WM_KEYUP";
    case WM_SYSKEYDOWN:
        return L"WM_SYSKEYDOWN";
    case WM_SYSKEYUP:
        return L"WM_SYSKEYUP";
    default:
        return L"WM_OTHER";
    }
}

void appendUtf8Line(const wchar_t *text)
{
    const std::wstring path = traceFilePath();
    HANDLE handle = ::CreateFileW(path.c_str(),
                                  FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    const int wideLength = static_cast<int>(wcslen(text));
    const int byteCount = ::WideCharToMultiByte(CP_UTF8, 0, text, wideLength, nullptr, 0, nullptr, nullptr);
    if (wideLength <= 0 || byteCount <= 0) {
        ::CloseHandle(handle);
        return;
    }

    std::string utf8(byteCount, '\0');
    if (::WideCharToMultiByte(CP_UTF8, 0, text, wideLength, utf8.data(), byteCount, nullptr, nullptr) <= 0) {
        ::CloseHandle(handle);
        return;
    }

    DWORD written = 0;
    ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    ::CloseHandle(handle);
}

void emit(const wchar_t *text)
{
    if (!kEnableSystemChordTrace)
        return;

    std::lock_guard<std::mutex> lock(g_traceMutex);
    ::OutputDebugStringW(text);
    appendUtf8Line(text);
}
}

namespace rdp::trace
{
void resetSystemChordTrace()
{
    if (!kEnableSystemChordTrace)
        return;

    std::lock_guard<std::mutex> lock(g_traceMutex);
    const std::wstring path = traceFilePath();
    HANDLE handle = ::CreateFileW(path.c_str(),
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (handle != INVALID_HANDLE_VALUE)
        ::CloseHandle(handle);
}

bool shouldTraceSystemChordVirtualKey(unsigned int virtualKey)
{
    return virtualKey == VK_MENU
        || virtualKey == VK_LMENU
        || virtualKey == VK_RMENU
        || virtualKey == VK_TAB
        || virtualKey == VK_LWIN
        || virtualKey == VK_RWIN;
}

void logSystemChordEvent(const wchar_t *stage,
                         unsigned int virtualKey,
                         std::uint32_t message,
                         std::intptr_t lParam,
                         unsigned int flags,
                         unsigned int keyboardModifiers,
                         bool hasFocus,
                         bool captureWithoutFocus,
                         std::size_t pressedKeyCount)
{
    wchar_t buffer[512] = {};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE,
                 L"[SystemChord] %ls vk=0x%02X msg=%ls lp=0x%08IX flags=0x%X mods=0x%X focus=%d captureNoFocus=%d pressed=%zu\r\n",
                 stage,
                 virtualKey,
                 messageName(message),
                 lParam,
                 flags,
                 keyboardModifiers,
                 hasFocus ? 1 : 0,
                 captureWithoutFocus ? 1 : 0,
                 pressedKeyCount);
    emit(buffer);
}

void logSystemChordNote(const wchar_t *stage,
                        unsigned int keyboardModifiers,
                        bool hasFocus,
                        bool captureWithoutFocus,
                        std::size_t pressedKeyCount)
{
    logSystemChordEvent(stage, 0, 0, 0, 0, keyboardModifiers, hasFocus, captureWithoutFocus, pressedKeyCount);
}

void logSystemChordSyncAction(const wchar_t *stage,
                              unsigned int sourceVirtualKey,
                              unsigned int actionVirtualKey,
                              bool down,
                              unsigned int desiredModifiers,
                              unsigned int keyboardModifiers,
                              bool hasFocus,
                              bool captureWithoutFocus,
                              std::size_t pressedKeyCount)
{
    wchar_t buffer[512] = {};
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE,
                 L"[SystemChord] %ls srcVk=0x%02X actionVk=0x%02X down=%d desiredMods=0x%X mods=0x%X focus=%d captureNoFocus=%d pressed=%zu\r\n",
                 stage,
                 sourceVirtualKey,
                 actionVirtualKey,
                 down ? 1 : 0,
                 desiredModifiers,
                 keyboardModifiers,
                 hasFocus ? 1 : 0,
                 captureWithoutFocus ? 1 : 0,
                 pressedKeyCount);
    emit(buffer);
}
}
