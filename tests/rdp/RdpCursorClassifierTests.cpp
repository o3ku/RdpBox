#include <cassert>
#include <cstring>

#include <windows.h>

#include "common/NativeTypes.h"
#include "rdp/RdpCursorClassifier.h"

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

FrameBuffer makeColumnSplitResizeCursorFrame()
{
    FrameBuffer frame = makeCursorFrame(16, 16);

    for (int y = 3; y <= 12; ++y)
        setPixel(frame, 8, y);

    for (int x = 3; x <= 6; ++x)
        setPixel(frame, x, 8);
    setPixel(frame, 2, 8);
    setPixel(frame, 3, 7);
    setPixel(frame, 3, 9);
    setPixel(frame, 4, 6);
    setPixel(frame, 4, 10);

    for (int x = 10; x <= 13; ++x)
        setPixel(frame, x, 8);
    setPixel(frame, 14, 8);
    setPixel(frame, 13, 7);
    setPixel(frame, 13, 9);
    setPixel(frame, 12, 6);
    setPixel(frame, 12, 10);

    return frame;
}

FrameBuffer makeRowSplitResizeCursorFrame()
{
    FrameBuffer frame = makeCursorFrame(16, 16);

    for (int x = 3; x <= 12; ++x)
        setPixel(frame, x, 8);

    for (int y = 3; y <= 6; ++y)
        setPixel(frame, 8, y);
    setPixel(frame, 8, 2);
    setPixel(frame, 7, 3);
    setPixel(frame, 9, 3);
    setPixel(frame, 6, 4);
    setPixel(frame, 10, 4);

    for (int y = 10; y <= 13; ++y)
        setPixel(frame, 8, y);
    setPixel(frame, 8, 14);
    setPixel(frame, 7, 13);
    setPixel(frame, 9, 13);
    setPixel(frame, 6, 12);
    setPixel(frame, 10, 12);

    return frame;
}

FrameBuffer makeVerticalLineCursorFrame()
{
    FrameBuffer frame = makeCursorFrame(16, 16);

    for (int y = 2; y <= 13; ++y)
        setPixel(frame, 8, y);

    return frame;
}
}

int main()
{
    const FrameBuffer frame = cursorFrameFromHandle(LoadCursor(nullptr, IDC_IBEAM));
    assert(!frame.empty());

    const auto shape = RdpCursorClassifier::classifyShape(frame, PointI{0, 0});
    assert(shape.has_value());
    assert(*shape == CursorKind::IBeam);

    const FrameBuffer arrowFrame = cursorFrameFromHandle(LoadCursor(nullptr, IDC_ARROW));
    assert(!arrowFrame.empty());

    const auto arrowShape = RdpCursorClassifier::classifyShape(arrowFrame, PointI{0, 0});
    assert(arrowShape.has_value());
    assert(*arrowShape == CursorKind::Arrow);

    const auto columnSplitShape = RdpCursorClassifier::classifyShape(makeColumnSplitResizeCursorFrame(), PointI{8, 8});
    assert(columnSplitShape.has_value());
    assert(*columnSplitShape == CursorKind::SizeWE);

    const auto rowSplitShape = RdpCursorClassifier::classifyShape(makeRowSplitResizeCursorFrame(), PointI{8, 8});
    assert(rowSplitShape.has_value());
    assert(*rowSplitShape == CursorKind::SizeNS);

    const auto verticalLineShape = RdpCursorClassifier::classifyShape(makeVerticalLineCursorFrame(), PointI{8, 8});
    assert(!verticalLineShape.has_value()
        || (*verticalLineShape != CursorKind::SizeNS && *verticalLineShape != CursorKind::SizeWE));

    return 0;
}
