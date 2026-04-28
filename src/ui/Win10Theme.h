#pragma once

#include <windows.h>

namespace Win10Theme
{
constexpr COLORREF kAccent = RGB(0, 120, 215);
constexpr COLORREF kAccentDark = RGB(0, 90, 158);
constexpr COLORREF kAccentLight = RGB(229, 241, 251);
constexpr COLORREF kAccentPressed = RGB(204, 228, 247);
constexpr COLORREF kSurface = RGB(255, 255, 255);
constexpr COLORREF kSurfaceMuted = RGB(243, 243, 243);
constexpr COLORREF kSurfaceSubtle = RGB(248, 248, 248);
constexpr COLORREF kBorder = RGB(204, 204, 204);
constexpr COLORREF kBorderSubtle = RGB(229, 229, 229);
constexpr COLORREF kText = RGB(33, 33, 33);
constexpr COLORREF kTextSubtle = RGB(96, 96, 96);
constexpr COLORREF kTextDisabled = RGB(160, 160, 160);
constexpr COLORREF kCloseHover = RGB(232, 17, 35);
constexpr COLORREF kCloseHoverText = RGB(255, 255, 255);

// Dark titlebar palette
constexpr COLORREF kCaptionBg = RGB(32, 32, 32);
constexpr COLORREF kCaptionTabInactive = RGB(45, 45, 45);
constexpr COLORREF kCaptionTabActive = RGB(60, 60, 60);
constexpr COLORREF kCaptionTabHover = RGB(55, 55, 55);
constexpr COLORREF kCaptionText = RGB(230, 230, 230);
constexpr COLORREF kCaptionTextSubtle = RGB(170, 170, 170);
constexpr COLORREF kCaptionButtonHover = RGB(64, 64, 64);
constexpr COLORREF kCaptionSeparator = RGB(70, 70, 70);

inline HFONT createUiFont(int pointSize = 9)
{
    HDC dc = ::GetDC(nullptr);
    const int height = -MulDiv(pointSize, ::GetDeviceCaps(dc, LOGPIXELSY), 72);
    ::ReleaseDC(nullptr, dc);

    LOGFONTW lf = {};
    lf.lfHeight = height;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return ::CreateFontIndirectW(&lf);
}
}
