#pragma once

#include <windows.h>

namespace rdp
{
constexpr DWORD kClipboardWindowReadyTimeoutMs = 5000;

inline bool didClipboardWindowInitialize(DWORD waitResult, bool windowReady)
{
    return waitResult == WAIT_OBJECT_0 && windowReady;
}
}
