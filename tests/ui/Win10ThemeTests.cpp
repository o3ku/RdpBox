#include <cassert>

#include <windows.h>

#include "ui/Win10Theme.h"

int main()
{
    assert(Win10Theme::kCaptionBg == RGB(0, 0, 0));
    assert(Win10Theme::kCloseHover == RGB(232, 17, 35));
    assert(Win10Theme::kBrandAccent != Win10Theme::kAccent);

    HFONT font = Win10Theme::createUiFont();
    assert(font != nullptr);

    LOGFONTW logFont = {};
    assert(::GetObjectW(font, sizeof(logFont), &logFont) == sizeof(logFont));
    assert(wcscmp(logFont.lfFaceName, L"Segoe UI") == 0);
    assert(logFont.lfWeight == FW_NORMAL);
    assert(logFont.lfHeight < 0);

    HFONT largerFont = Win10Theme::createUiFont(12);
    assert(largerFont != nullptr);

    LOGFONTW largerLogFont = {};
    assert(::GetObjectW(largerFont, sizeof(largerLogFont), &largerLogFont) == sizeof(largerLogFont));
    assert(largerLogFont.lfHeight < logFont.lfHeight);

    ::DeleteObject(font);
    ::DeleteObject(largerFont);
    return 0;
}
