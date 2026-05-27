#pragma once

#include <cstdint>
#include <vector>

struct HICON__;
using HICON = HICON__*;
using HCURSOR = HICON;

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
