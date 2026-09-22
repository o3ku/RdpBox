#include <cassert>
#include <cstring>

#include <windows.h>

#include "common/NativeTypes.h"
#include "common/rdp/RdpCursorClassifier.h"

namespace
{
FrameBuffer cursorFrameFromHandle(HCURSOR cursorHandle)
{
    if (!cursorHandle)
        return {};

    ICONINFO iconInfo = {};
    if (!GetIconInfo(cursorHandle, &iconInfo))
        return {};

    BITMAP bitmap = {};
    int width = 32;
    int height = 32;

    if (iconInfo.hbmColor && GetObject(iconInfo.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        width = bitmap.bmWidth;
        height = bitmap.bmHeight;
    } else if (iconInfo.hbmMask && GetObject(iconInfo.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        width = bitmap.bmWidth;
        height = bitmap.bmHeight / 2;
    }

    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = width;
    bi.bV5Height = -height;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void *bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    HBITMAP dib = screenDc ? CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS,
                                              &bits, nullptr, 0) : nullptr;

    FrameBuffer result;
    if (dib && bits && memDc) {
        HGDIOBJ oldBitmap = SelectObject(memDc, dib);
        PatBlt(memDc, 0, 0, width, height, BLACKNESS);
        DrawIconEx(memDc, 0, 0, cursorHandle, width, height, 0, nullptr, DI_NORMAL);

        result.width = width;
        result.height = height;
        result.stride = width * 4;
        result.pixels.resize(static_cast<std::size_t>(result.stride) * static_cast<std::size_t>(result.height));
        std::memcpy(result.pixels.data(), bits, result.pixels.size());

        SelectObject(memDc, oldBitmap);
    }

    if (dib)
        DeleteObject(dib);
    if (memDc)
        DeleteDC(memDc);
    if (screenDc)
        ReleaseDC(nullptr, screenDc);
    if (iconInfo.hbmColor)
        DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask)
        DeleteObject(iconInfo.hbmMask);

    return result;
}

FrameBuffer makeCursorFrame(int width, int height)
{
    FrameBuffer frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * static_cast<std::size_t>(frame.height), 0);
    return frame;
}

void setPixel(FrameBuffer &frame, int x, int y)
{
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height)
        return;

    const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride)
        + static_cast<std::size_t>(x) * 4u;
    frame.pixels[offset + 0] = 0x00;
    frame.pixels[offset + 1] = 0x00;
    frame.pixels[offset + 2] = 0x00;
    frame.pixels[offset + 3] = 0xFF;
}




FrameBuffer makeMagnifierCursorFrame()
{
    FrameBuffer frame = makeCursorFrame(16, 16);

    const PointI lensPixels[] = {
        {4, 1}, {5, 1}, {6, 1},
        {3, 2}, {7, 2},
        {2, 3}, {8, 3},
        {2, 4}, {8, 4},
        {2, 5}, {8, 5},
        {3, 6}, {7, 6},
        {4, 7}, {5, 7}, {6, 7}
    };

    for (const auto &pixel : lensPixels)
        setPixel(frame, pixel.x, pixel.y);

    const PointI handlePixels[] = {
        {7, 7}, {8, 8}, {9, 9}, {10, 10}, {11, 11},
        {10, 11}, {11, 12}, {12, 13}
    };

    for (const auto &pixel : handlePixels)
        setPixel(frame, pixel.x, pixel.y);

    return frame;
}
}

int main()
{
    {
        const CursorInfo hidden{CursorKind::Hidden, nullptr, false};
        assert(RdpCursorClassifier::cursorHandleFromInfo(hidden) == nullptr);
    }

    {
        const CursorInfo fallback{CursorKind::Arrow, nullptr, false};
        assert(RdpCursorClassifier::cursorHandleFromInfo(fallback) != nullptr);
    }

    {
        FrameBuffer invalidFrame;
        invalidFrame.width = 16;
        invalidFrame.height = 16;
        invalidFrame.stride = 8;
        invalidFrame.pixels.resize(16u);

        const CursorInfo invalidCursor = RdpCursorClassifier::createCursor(invalidFrame, PointI{0, 0});
        assert(invalidCursor.kind == CursorKind::Custom);
        assert(invalidCursor.handle == nullptr);
        assert(!invalidCursor.ownsHandle);
    }

    const FrameBuffer frame = cursorFrameFromHandle(LoadCursor(nullptr, IDC_IBEAM));
    assert(!frame.empty());

    const CursorInfo iBeamCursor = RdpCursorClassifier::createCursor(frame, PointI{0, 0});
    assert(iBeamCursor.kind == CursorKind::Custom);
    assert(iBeamCursor.handle != nullptr);
    if (iBeamCursor.ownsHandle && iBeamCursor.handle)
        DestroyCursor(iBeamCursor.handle);

    const FrameBuffer arrowFrame = cursorFrameFromHandle(LoadCursor(nullptr, IDC_ARROW));
    assert(!arrowFrame.empty());

    const CursorInfo arrowCursor = RdpCursorClassifier::createCursor(arrowFrame, PointI{0, 0});
    assert(arrowCursor.kind == CursorKind::Custom);
    assert(arrowCursor.handle != nullptr);
    if (arrowCursor.ownsHandle && arrowCursor.handle)
        DestroyCursor(arrowCursor.handle);

    const CursorInfo magnifierCursor = RdpCursorClassifier::createCursor(makeMagnifierCursorFrame(), PointI{5, 5});
    assert(magnifierCursor.kind == CursorKind::Custom);
    if (magnifierCursor.ownsHandle && magnifierCursor.handle)
        DestroyCursor(magnifierCursor.handle);

    return 0;
}
