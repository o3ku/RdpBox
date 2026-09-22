#pragma once

// Power broadcast events: real macros from windows.h when present, else ABI values.
#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 18
#endif
#ifndef PBT_APMRESUMESUSPEND
#define PBT_APMRESUMESUSPEND 7
#endif

#include <cstdint>
#include <vector>

#ifdef _WIN32
struct HICON__;
using HICON = HICON__*;
using HCURSOR = HICON;
#else
#include <cstddef>

using HICON = void*;   // no native cursor handles outside Windows
using HCURSOR = void*;
using UINT = unsigned int;

// Minimal Win32 geometry shims ( RECT math shared by WindowStateScaling).
struct RECT
{
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

inline void OffsetRect(RECT *rect, int dx, int dy)
{
    if (!rect)
        return;
    rect->left += dx;
    rect->top += dy;
    rect->right += dx;
    rect->bottom += dy;
}

constexpr UINT WA_INACTIVE = 0;
constexpr UINT WA_ACTIVE = 1;
constexpr UINT WA_CLICKACTIVE = 2;
constexpr int SW_SHOWNORMAL = 1;
constexpr int SW_SHOWMINIMIZED = 2;
constexpr int SW_SHOWMAXIMIZED = 3;
#endif

struct PointI
{
    int x = 0;
    int y = 0;
};

struct SizeI
{
    int width = 0;
    int height = 0;

    friend bool operator==(const SizeI &, const SizeI &) = default;
};

enum class MouseButton
{
    None,
    Left,
    Right,
    Middle,
    Back,
    Forward
};

enum ModifierFlags : unsigned int
{
    ModifierNone = 0,
    ModifierShift = 1u << 0,
    ModifierControl = 1u << 1,
    ModifierAlt = 1u << 2,
    ModifierWin = 1u << 3
};

struct FrameBuffer
{
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> pixels;

    bool empty() const
    {
        return width <= 0 || height <= 0 || stride <= 0 || pixels.empty();
    }
};

enum class CursorKind
{
    Hidden,
    Arrow,
    IBeam,
    Cross,
    Wait,
    AppStarting,
    Hand,
    SizeWE,
    SizeNS,
    SizeNWSE,
    SizeNESW,
    SizeAll,
    Custom
};

struct CursorInfo
{
    CursorKind kind = CursorKind::Arrow;
    HCURSOR handle = nullptr;
    bool ownsHandle = false;
};
