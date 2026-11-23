#include <cassert>
#include <cstring>

#include <windows.h>

#include "common/NativeTypes.h"
#include "common/rdp/RdpCursorClassifier.h"
#include "common/rdp/RdpPointerAlpha.h"

namespace
{
FrameBuffer cursorFrameFromHandle(HCURSOR cursorHandle, bool whiteBackground = false)
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
        PatBlt(memDc, 0, 0, width, height, whiteBackground ? WHITENESS : BLACKNESS);
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

bool isArrowPixel(int x, int y)
{
    if (y >= 0 && y < 16 && (x == 2 || x == 3))
        return true;
    if (y >= 0 && y < 8 && x >= 2 && x <= 2 + y)
        return true;
    if (y >= 16 && y < 24 && (x == 4 || x == 5))
        return true;
    return false;
}

int countArrowPixels()
{
    int count = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (isArrowPixel(x, y))
                ++count;
    return count;
}

int countOpaqueBlack(const FrameBuffer &frame)
{
    int count = 0;
    for (int y = 0; y < frame.height; ++y)
    {
        for (int x = 0; x < frame.width; ++x)
        {
            const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride)
                + static_cast<std::size_t>(x) * 4u;
            if (frame.pixels[offset + 3] == 0xFF && frame.pixels[offset + 0] == 0
                && frame.pixels[offset + 1] == 0 && frame.pixels[offset + 2] == 0)
                ++count;
        }
    }
    return count;
}

int countRgb(const FrameBuffer &frame, std::uint8_t value)
{
    int count = 0;
    for (int y = 0; y < frame.height; ++y)
    {
        for (int x = 0; x < frame.width; ++x)
        {
            const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride)
                + static_cast<std::size_t>(x) * 4u;
            if (frame.pixels[offset + 0] == value && frame.pixels[offset + 1] == value
                && frame.pixels[offset + 2] == value)
                ++count;
        }
    }
    return count;
}

int pixelAlpha(const FrameBuffer &frame, int x, int y)
{
    const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride)
        + static_cast<std::size_t>(x) * 4u;
    return frame.pixels[offset + 3];
}

struct PointerFixture
{
    FrameBuffer frame;
    std::vector<std::uint8_t> andMask;
};

// A pointer exactly as freerdp_image_copy_from_pointer_data leaves it:
// BGRA32, shape and background both with alpha 0 (the sender left the
// alpha byte unused), and the wire AND mask (1 = transparent) alongside.
PointerFixture makeBgrxArrow(bool blackShape)
{
    PointerFixture fixture;
    fixture.frame = makeCursorFrame(32, 32);
    const int andStep = (32 + 7) / 8 + ((32 + 7) / 8) % 2;
    fixture.andMask.assign(static_cast<std::size_t>(andStep) * 32, 0);

    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            const bool shape = isArrowPixel(x, y);
            const std::size_t offset = static_cast<std::size_t>(y)
                    * static_cast<std::size_t>(fixture.frame.stride)
                + static_cast<std::size_t>(x) * 4u;
            if (shape && !blackShape)
            {
                fixture.frame.pixels[offset + 0] = 0xFF;
                fixture.frame.pixels[offset + 1] = 0xFF;
                fixture.frame.pixels[offset + 2] = 0xFF;
            }
            // Shape and background both keep alpha 0, as a BGRX sender
            // leaves it; a black shape is (0, 0, 0, 0) just like the
            // background, so only the AND mask tells them apart.
            if (!shape)
            {
                // Colour pointers are bottom-up on the wire.
                const int wireRow = 32 - 1 - y;
                fixture.andMask[static_cast<std::size_t>(wireRow) * static_cast<std::size_t>(andStep)
                    + static_cast<std::size_t>(x) / 8u]
                    |= static_cast<std::uint8_t>(0x80u >> (x % 8));
            }
        }
    }
    return fixture;
}

// Same shape, but with the alpha channel already filled in by the
// sender: shape opaque, background transparent.
PointerFixture makeTrueAlphaArrow()
{
    PointerFixture fixture = makeBgrxArrow(false);
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            if (isArrowPixel(x, y))
                fixture.frame.pixels[static_cast<std::size_t>(y)
                        * static_cast<std::size_t>(fixture.frame.stride)
                    + static_cast<std::size_t>(x) * 4u + 3u] = 0xFF;
        }
    }
    return fixture;
}

// A 32bpp ARGB pointer as Windows sends its aero cursors (I-beam
// included): the alpha channel already carries the transparency and
// the AND mask is vestigial, all zeros. A black shape is (0, 0, 0, 0xFF).
PointerFixture makeTrueAlphaArrowWithZeroMask(bool blackShape)
{
    PointerFixture fixture;
    fixture.frame = makeCursorFrame(32, 32);
    const int andStep = (32 + 7) / 8 + ((32 + 7) / 8) % 2;
    fixture.andMask.assign(static_cast<std::size_t>(andStep) * 32, 0);

    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            if (isArrowPixel(x, y))
            {
                const std::size_t offset = static_cast<std::size_t>(y)
                        * static_cast<std::size_t>(fixture.frame.stride)
                    + static_cast<std::size_t>(x) * 4u;
                if (!blackShape)
                {
                    fixture.frame.pixels[offset + 0] = 0xFF;
                    fixture.frame.pixels[offset + 1] = 0xFF;
                    fixture.frame.pixels[offset + 2] = 0xFF;
                }
                fixture.frame.pixels[offset + 3] = 0xFF;
            }
        }
    }
    return fixture;
}

// A 1bpp pointer as FreeRDP decodes it: black/white shape pixels opaque,
// transparent pixels (0, 0, 0, 0), and one inverted pixel (AND 1, XOR 1)
// resolved to a checkerboard colour with alpha 0xFF. The AND mask is
// top-down for 1bpp (no flip).
PointerFixture makeMonoPointer()
{
    PointerFixture fixture;
    fixture.frame = makeCursorFrame(32, 32);
    const int andStep = (32 + 7) / 8 + ((32 + 7) / 8) % 2;
    fixture.andMask.assign(static_cast<std::size_t>(andStep) * 32, 0);

    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            const bool shape = isArrowPixel(x, y);
            const bool inverted = (x == 20 && y == 20);
            const std::size_t offset = static_cast<std::size_t>(y)
                    * static_cast<std::size_t>(fixture.frame.stride)
                + static_cast<std::size_t>(x) * 4u;
            if (inverted)
            {
                const std::uint8_t fill = ((x + y) & 1) ? 0x00 : 0xFF;
                fixture.frame.pixels[offset + 0] = fill;
                fixture.frame.pixels[offset + 1] = fill;
                fixture.frame.pixels[offset + 2] = fill;
                fixture.frame.pixels[offset + 3] = 0xFF;
            }
            else if (shape)
            {
                fixture.frame.pixels[offset + 3] = 0xFF;
            }
            if (!shape)
            {
                fixture.andMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(andStep)
                    + static_cast<std::size_t>(x) / 8u]
                    |= static_cast<std::uint8_t>(0x80u >> (x % 8));
            }
        }
    }
    return fixture;
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

    // Black-square regression (issue #1): a 32bpp BGRX pointer decodes
    // to alpha 0 for every pixel, and the old classifier forced all of
    // them opaque, turning the transparent background into a black
    // square. The AND mask is what tells shape from background.
    const int arrowPixels = countArrowPixels();
    assert(arrowPixels > 0);

    {
        // 32bpp BGRX white arrow.
        PointerFixture fixture = makeBgrxArrow(false);
        assert(countOpaqueBlack(fixture.frame) == 0);
        const bool appliedWhite = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(),
                                                                  fixture.andMask.size(), 32);
        assert(appliedWhite);
        assert(countOpaqueBlack(fixture.frame) == 0);
        assert(pixelAlpha(fixture.frame, 2, 0) == 0xFF);
        assert(pixelAlpha(fixture.frame, 0, 0) == 0x00);
        const CursorInfo cursor = RdpCursorClassifier::createCursor(fixture.frame, PointI{0, 0});
        assert(cursor.kind == CursorKind::Custom);
        assert(cursor.handle != nullptr);
        // On white, a transparent background stays white; a black square
        // would show up as black pixels here.
        const FrameBuffer onWhite = cursorFrameFromHandle(cursor.handle, true);
        assert(!onWhite.empty());
        assert(countRgb(onWhite, 0x00) == 0);
        const FrameBuffer onBlack = cursorFrameFromHandle(cursor.handle);
        assert(!onBlack.empty());
        assert(countRgb(onBlack, 0xFF) == arrowPixels);
        if (cursor.ownsHandle && cursor.handle)
            DestroyCursor(cursor.handle);
    }

    {
        // 32bpp BGRX black arrow: the shape is (0, 0, 0, 0), exactly like
        // the background, so only the AND mask can keep it opaque.
        PointerFixture fixture = makeBgrxArrow(true);
        const bool appliedBlack = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(),
                                                                  fixture.andMask.size(), 32);
        assert(appliedBlack);
        assert(countOpaqueBlack(fixture.frame) == arrowPixels);
        assert(pixelAlpha(fixture.frame, 2, 0) == 0xFF);
        assert(pixelAlpha(fixture.frame, 0, 0) == 0x00);
        const CursorInfo cursor = RdpCursorClassifier::createCursor(fixture.frame, PointI{0, 0});
        assert(cursor.handle != nullptr);
        const FrameBuffer onWhite = cursorFrameFromHandle(cursor.handle, true);
        assert(!onWhite.empty());
        assert(countRgb(onWhite, 0x00) == arrowPixels);
        assert(countRgb(onWhite, 0xFF) == 32 * 32 - arrowPixels);
        if (cursor.ownsHandle && cursor.handle)
            DestroyCursor(cursor.handle);
    }

    {
        // 32bpp true-alpha cursor: pixels that already carry alpha are
        // left alone.
        PointerFixture fixture = makeTrueAlphaArrow();
        const FrameBuffer before = fixture.frame;
        const bool appliedAlpha = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(),
                                                                  fixture.andMask.size(), 32);
        assert(appliedAlpha);
        assert(fixture.frame.pixels == before.pixels);
        const CursorInfo cursor = RdpCursorClassifier::createCursor(fixture.frame, PointI{0, 0});
        assert(cursor.handle != nullptr);
        const FrameBuffer onWhite = cursorFrameFromHandle(cursor.handle, true);
        assert(!onWhite.empty());
        assert(countRgb(onWhite, 0x00) == 0);
        if (cursor.ownsHandle && cursor.handle)
            DestroyCursor(cursor.handle);
    }

    {
        // 32bpp ARGB white arrow with a vestigial all-zero AND mask
        // (the aero-cursor case): the alpha channel is authoritative,
        // the mask must not be consulted -- it would force the
        // transparent background opaque.
        PointerFixture fixture = makeTrueAlphaArrowWithZeroMask(false);
        const FrameBuffer before = fixture.frame;
        const bool appliedZeroMask = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(),
                                                                    fixture.andMask.size(), 32);
        assert(appliedZeroMask);
        assert(fixture.frame.pixels == before.pixels);
        const CursorInfo cursor = RdpCursorClassifier::createCursor(fixture.frame, PointI{0, 0});
        assert(cursor.handle != nullptr);
        const FrameBuffer onWhite = cursorFrameFromHandle(cursor.handle, true);
        assert(!onWhite.empty());
        assert(countRgb(onWhite, 0x00) == 0);
        if (cursor.ownsHandle && cursor.handle)
            DestroyCursor(cursor.handle);
    }

    {
        // 32bpp ARGB black arrow with a vestigial all-zero AND mask:
        // the I-beam case (issue #1 follow-up). The shape is black, so
        // forcing the background opaque would hide it inside a solid
        // black rectangle; the background must stay transparent.
        PointerFixture fixture = makeTrueAlphaArrowWithZeroMask(true);
        const FrameBuffer before = fixture.frame;
        const bool appliedBlackZeroMask = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(),
                                                                         fixture.andMask.size(), 32);
        assert(appliedBlackZeroMask);
        assert(fixture.frame.pixels == before.pixels);
        assert(countOpaqueBlack(fixture.frame) == arrowPixels);
        const CursorInfo cursor = RdpCursorClassifier::createCursor(fixture.frame, PointI{0, 0});
        assert(cursor.handle != nullptr);
        const FrameBuffer onWhite = cursorFrameFromHandle(cursor.handle, true);
        assert(!onWhite.empty());
        assert(countRgb(onWhite, 0x00) == arrowPixels);
        assert(countRgb(onWhite, 0xFF) == 32 * 32 - arrowPixels);
        if (cursor.ownsHandle && cursor.handle)
            DestroyCursor(cursor.handle);
    }

    {
        // 1bpp pointer as FreeRDP decodes it: alpha is already correct,
        // so reconstruction is a no-op, including the inverted pixel.
        PointerFixture fixture = makeMonoPointer();
        const FrameBuffer before = fixture.frame;
        const bool appliedMono = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(),
                                                                fixture.andMask.size(), 1);
        assert(appliedMono);
        assert(fixture.frame.pixels == before.pixels);
        assert(pixelAlpha(fixture.frame, 20, 20) == 0xFF);
        assert(pixelAlpha(fixture.frame, 2, 0) == 0xFF);
        assert(pixelAlpha(fixture.frame, 0, 0) == 0x00);
    }

    {
        // Missing or truncated AND mask: no reconstruction, no crash,
        // frame left untouched for the classifier's opaque fallback.
        PointerFixture fixture = makeBgrxArrow(false);
        const FrameBuffer before = fixture.frame;
        const bool appliedNone = RdpPointerAlpha::applyAndMask(fixture.frame, nullptr, 0, 32);
        assert(!appliedNone);
        assert(fixture.frame.pixels == before.pixels);
        const bool appliedShort = RdpPointerAlpha::applyAndMask(fixture.frame, fixture.andMask.data(), 1, 32);
        assert(!appliedShort);
        assert(fixture.frame.pixels == before.pixels);
        FrameBuffer empty;
        const bool appliedEmpty = RdpPointerAlpha::applyAndMask(empty, fixture.andMask.data(),
                                                                fixture.andMask.size(), 32);
        assert(!appliedEmpty);
    }

    return 0;
}
