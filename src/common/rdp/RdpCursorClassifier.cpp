#include "common/rdp/RdpCursorClassifier.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
bool isValidFrameBuffer(const FrameBuffer &buffer)
{
    if (buffer.empty())
        return false;

    if (buffer.width <= 0 || buffer.height <= 0 || buffer.stride < buffer.width * 4)
        return false;

    const std::size_t requiredBytes = static_cast<std::size_t>(buffer.stride)
        * static_cast<std::size_t>(buffer.height);
    return buffer.pixels.size() >= requiredBytes;
}

// Last resort only, for a frame that carries no transparency information
// at all (no pixel with alpha != 0 and no AND mask was available to
// reconstruct it from -- see RdpPointerAlpha, which the production path
// runs before this). With nothing to distinguish a black opaque pixel
// from a transparent one, making the whole frame opaque at least shows
// the shape; it is what used to turn a 32bpp BGRX pointer into a black
// square, so do not extend this to any frame whose alpha was already
// resolved. Callers that have an AND mask must resolve the alpha first
// instead of relying on this.
FrameBuffer opaqueCursorFrame(FrameBuffer buffer)
{
    if (!isValidFrameBuffer(buffer))
        return {};

    bool hasMeaningfulAlpha = false;
    for (int y = 0; y < buffer.height && !hasMeaningfulAlpha; ++y) {
        const std::uint8_t *row = buffer.pixels.data()
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
        for (int x = 0; x < buffer.width; ++x) {
            const std::uint8_t alpha = row[static_cast<std::size_t>(x) * 4u + 3u];
            if (alpha != 0) {
                hasMeaningfulAlpha = true;
                break;
            }
        }
    }

    if (hasMeaningfulAlpha)
        return buffer;

    for (int y = 0; y < buffer.height; ++y) {
        std::uint8_t *row = buffer.pixels.data()
            + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
        for (int x = 0; x < buffer.width; ++x)
            row[static_cast<std::size_t>(x) * 4u + 3u] = 0xFF;
    }

    return buffer;
}

#ifdef _WIN32
HCURSOR createCursorHandleFromFrame(const FrameBuffer &remoteImage, PointI hotspot)
{
    if (!isValidFrameBuffer(remoteImage))
        return nullptr;

    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = remoteImage.width;
    bi.bV5Height = -remoteImage.height;
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
    HBITMAP colorBitmap = screenDc
        ? CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0)
        : nullptr;
    HCURSOR cursor = nullptr;

    if (colorBitmap && bits && memDc) {
        HGDIOBJ oldBitmap = SelectObject(memDc, colorBitmap);

        const std::size_t rowBytes = static_cast<std::size_t>(remoteImage.width) * 4u;
        for (int y = 0; y < remoteImage.height; ++y) {
            const std::uint8_t *srcRow = remoteImage.pixels.data()
                + static_cast<std::size_t>(y) * static_cast<std::size_t>(remoteImage.stride);
            std::memcpy(static_cast<std::uint8_t *>(bits) + static_cast<std::size_t>(y) * rowBytes,
                        srcRow,
                        rowBytes);
        }

        // The AND mask is all zero on purpose: for a 32bpp cursor with a
        // real alpha channel Windows uses the alpha and the mask is the
        // standard all-zero layout for such icons. It is only consulted
        // when the alpha channel is all zero, which must not happen for a
        // frame that reached here -- the production path reconstructs the
        // alpha from the server AND mask first (see RdpPointerAlpha), and
        // opaqueCursorFrame above forces it opaque as a last resort when
        // no mask was available.
        const int maskStride = ((remoteImage.width + 31) / 32) * 4;
        std::vector<std::uint8_t> maskBytes(
            static_cast<std::size_t>(maskStride) * static_cast<std::size_t>(remoteImage.height), 0x00);
        HBITMAP maskBitmap = CreateBitmap(remoteImage.width, remoteImage.height, 1, 1, maskBytes.data());

        ICONINFO iconInfo = {};
        iconInfo.fIcon = FALSE;
        iconInfo.xHotspot = static_cast<DWORD>(std::clamp(hotspot.x, 0, remoteImage.width - 1));
        iconInfo.yHotspot = static_cast<DWORD>(std::clamp(hotspot.y, 0, remoteImage.height - 1));
        iconInfo.hbmMask = maskBitmap;
        iconInfo.hbmColor = colorBitmap;

        cursor = CreateIconIndirect(&iconInfo);

        if (maskBitmap)
            DeleteObject(maskBitmap);
        SelectObject(memDc, oldBitmap);
    }

    if (colorBitmap)
        DeleteObject(colorBitmap);
    if (memDc)
        DeleteDC(memDc);
    if (screenDc)
        ReleaseDC(nullptr, screenDc);

    return cursor;
}

#endif // _WIN32
}

namespace RdpCursorClassifier
{
#ifdef _WIN32
CursorInfo createCursor(const FrameBuffer &remoteImage, PointI hotspot)
{
    const FrameBuffer cursorFrame = opaqueCursorFrame(remoteImage);
    const HCURSOR cursorHandle = createCursorHandleFromFrame(cursorFrame, hotspot);
    return CursorInfo{CursorKind::Custom, cursorHandle, cursorHandle != nullptr};
}

CursorInfo cursorInfoForState(const CursorInfo &cursorInfo)
{
    return CursorInfo{cursorInfo.kind, nullptr, false};
}

HCURSOR cursorHandleFromInfo(const CursorInfo &cursorInfo)
{
    if (cursorInfo.kind == CursorKind::Hidden)
        return nullptr;

    if (cursorInfo.handle)
        return cursorInfo.handle;

    return LoadCursor(nullptr, IDC_ARROW);
}
#else
// ponytail: non-Windows falls back to built-in cursor shapes (no pixel
// cursors); remap Custom to Arrow so the Qt widget picks a sensible shape.
CursorInfo createCursor(const FrameBuffer &remoteImage, PointI hotspot)
{
    static_cast<void>(remoteImage);
    static_cast<void>(hotspot);
    return CursorInfo{CursorKind::Arrow, nullptr, true};
}

CursorInfo cursorInfoForState(const CursorInfo &cursorInfo)
{
    return CursorInfo{cursorInfo.kind, nullptr, false};
}

HCURSOR cursorHandleFromInfo(const CursorInfo &)
{
    return nullptr;
}
#endif
}
