#include "rdp/RdpCursorClassifier.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
constexpr int kCursorMaskSize = 32;

struct CursorCandidate
{
    CursorKind kind;
    LPCTSTR idcName;
    std::vector<std::uint8_t> mask;
    double maxDistance = 0.22;
};

struct CursorMaskFeatures
{
    bool valid = false;
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;
    int occupiedPixels = 0;
    double maxColumnFill = 0.0;
    double maxRowFill = 0.0;
    double mainDiagonalFill = 0.0;
    double antiDiagonalFill = 0.0;
    int maxColumnIndex = 0;
    int maxRowIndex = 0;
};

double cursorMaskDistance(const std::vector<std::uint8_t> &left, const std::vector<std::uint8_t> &right);
CursorMaskFeatures analyzeCursorMask(const std::vector<std::uint8_t> &mask);
bool isCandidateCompatible(CursorKind kind, const CursorMaskFeatures &features);
bool shouldUseLocalIBeamCursor(const CursorMaskFeatures &features, PointI hotspot);
const std::vector<CursorCandidate> &systemCursorCandidates();

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

FrameBuffer opaqueCursorFrame(FrameBuffer buffer)
{
    if (!isValidFrameBuffer(buffer))
        return {};

    bool hasMeaningfulAlpha = false;
    for (int y = 0; y < buffer.height && !hasMeaningfulAlpha; ++y) {
        const std::uint8_t *row = buffer.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
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
        std::uint8_t *row = buffer.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride);
        for (int x = 0; x < buffer.width; ++x)
            row[static_cast<std::size_t>(x) * 4u + 3u] = 0xFF;
    }

    return buffer;
}

FrameBuffer cursorFrameBufferFromHandle(HCURSOR cursorHandle, PointI *hotspot = nullptr)
{
    if (!cursorHandle)
        return {};

    ICONINFO iconInfo = {};
    if (!GetIconInfo(cursorHandle, &iconInfo))
        return {};

    if (hotspot) {
        hotspot->x = static_cast<int>(iconInfo.xHotspot);
        hotspot->y = static_cast<int>(iconInfo.yHotspot);
    }

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

        const std::size_t copyBytes = static_cast<std::size_t>(result.stride)
            * static_cast<std::size_t>(result.height);
        std::memcpy(result.pixels.data(), bits, copyBytes);

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

std::vector<std::uint8_t> normalizedCursorMask(const FrameBuffer &source)
{
    if (!isValidFrameBuffer(source))
        return {};

    std::vector<std::uint8_t> normalized(kCursorMaskSize * kCursorMaskSize, 0);

    for (int y = 0; y < kCursorMaskSize; ++y) {
        int sourceY = (y * source.height) / kCursorMaskSize;
        if (sourceY >= source.height)
            sourceY = source.height - 1;

        for (int x = 0; x < kCursorMaskSize; ++x) {
            int sourceX = (x * source.width) / kCursorMaskSize;
            if (sourceX >= source.width)
                sourceX = source.width - 1;

            const std::size_t offset = static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(source.stride)
                + static_cast<std::size_t>(sourceX) * 4u;
            const std::uint8_t blue = source.pixels[offset + 0];
            const std::uint8_t green = source.pixels[offset + 1];
            const std::uint8_t red = source.pixels[offset + 2];
            const std::uint8_t alpha = source.pixels[offset + 3];
            const bool occupied = alpha > 24
                && (red < 250 || green < 250 || blue < 250 || alpha > 200);
            normalized[static_cast<std::size_t>(y) * kCursorMaskSize + static_cast<std::size_t>(x)] =
                occupied ? 255 : 0;
        }
    }

    return normalized;
}

std::optional<CursorKind> classifyShapeFromPreparedFrame(const FrameBuffer &cursorFrame, PointI hotspot)
{
    const std::vector<std::uint8_t> remoteMask = normalizedCursorMask(cursorFrame);
    if (remoteMask.empty())
        return std::nullopt;

    const CursorMaskFeatures remoteFeatures = analyzeCursorMask(remoteMask);
    if (!remoteFeatures.valid)
        return std::nullopt;

    if (shouldUseLocalIBeamCursor(remoteFeatures, hotspot))
        return CursorKind::IBeam;

    const auto &candidates = systemCursorCandidates();
    double bestScore = 1.0;
    std::optional<CursorKind> bestShape;
    double bestThreshold = 0.0;

    for (const auto &candidate : candidates) {
        if (candidate.mask.empty())
            continue;
        if (!isCandidateCompatible(candidate.kind, remoteFeatures))
            continue;

        const double score = cursorMaskDistance(remoteMask, candidate.mask);
        if (score < bestScore) {
            bestScore = score;
            bestShape = candidate.kind;
            bestThreshold = candidate.maxDistance;
        }
    }

    if (!bestShape.has_value() || bestScore > bestThreshold)
        return std::nullopt;

    return bestShape;
}

CursorMaskFeatures analyzeCursorMask(const std::vector<std::uint8_t> &mask)
{
    CursorMaskFeatures features;

    if (mask.empty())
        return features;

    int minX = kCursorMaskSize;
    int minY = kCursorMaskSize;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < kCursorMaskSize; ++y) {
        for (int x = 0; x < kCursorMaskSize; ++x) {
            if (mask[static_cast<std::size_t>(y) * kCursorMaskSize + static_cast<std::size_t>(x)] == 0)
                continue;

            ++features.occupiedPixels;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (features.occupiedPixels == 0)
        return features;

    features.valid = true;
    features.left = minX;
    features.top = minY;
    features.right = maxX;
    features.bottom = maxY;

    const int boundsWidth = features.right - features.left + 1;
    const int boundsHeight = features.bottom - features.top + 1;
    std::vector<int> columnHits(boundsWidth, 0);
    std::vector<int> rowHits(boundsHeight, 0);
    int mainDiagonalHits = 0;
    int antiDiagonalHits = 0;

    const double xFromYScale = (boundsHeight > 1)
        ? static_cast<double>(boundsWidth - 1) / static_cast<double>(boundsHeight - 1)
        : 0.0;

    for (int y = features.top; y <= features.bottom; ++y) {
        for (int x = features.left; x <= features.right; ++x) {
            if (mask[static_cast<std::size_t>(y) * kCursorMaskSize + static_cast<std::size_t>(x)] == 0)
                continue;

            const int localX = x - features.left;
            const int localY = y - features.top;
            ++columnHits[localX];
            ++rowHits[localY];

            const double mainDiagonalX = localY * xFromYScale;
            const double antiDiagonalX = (boundsWidth - 1) - mainDiagonalX;
            if (std::abs(static_cast<double>(localX) - mainDiagonalX) <= 2.5)
                ++mainDiagonalHits;
            if (std::abs(static_cast<double>(localX) - antiDiagonalX) <= 2.5)
                ++antiDiagonalHits;
        }
    }

    const auto maxColumnIt = std::max_element(columnHits.begin(), columnHits.end());
    const auto maxRowIt = std::max_element(rowHits.begin(), rowHits.end());
    const int maxColumnHits = *maxColumnIt;
    const int maxRowHits = *maxRowIt;
    features.maxColumnIndex = static_cast<int>(std::distance(columnHits.begin(), maxColumnIt));
    features.maxRowIndex = static_cast<int>(std::distance(rowHits.begin(), maxRowIt));
    features.maxColumnFill = static_cast<double>(maxColumnHits) / static_cast<double>(boundsHeight);
    features.maxRowFill = static_cast<double>(maxRowHits) / static_cast<double>(boundsWidth);
    features.mainDiagonalFill = static_cast<double>(mainDiagonalHits) / static_cast<double>(features.occupiedPixels);
    features.antiDiagonalFill = static_cast<double>(antiDiagonalHits) / static_cast<double>(features.occupiedPixels);

    return features;
}

double cursorMaskDistance(const std::vector<std::uint8_t> &left, const std::vector<std::uint8_t> &right)
{
    if (left.empty() || right.empty() || left.size() != right.size())
        return 1.0;

    int diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if ((left[i] > 0) != (right[i] > 0))
            ++diff;
    }

    return static_cast<double>(diff) / static_cast<double>(left.size());
}

bool isCandidateCompatible(CursorKind kind, const CursorMaskFeatures &features)
{
    if (!features.valid)
        return false;

    const int width = features.right - features.left + 1;
    const int height = features.bottom - features.top + 1;
    const double diagonalGap = std::abs(features.mainDiagonalFill - features.antiDiagonalFill);

    switch (kind) {
    case CursorKind::IBeam:
        return true;
    case CursorKind::SizeWE:
        return width >= height && features.maxRowFill >= 0.45;
    case CursorKind::SizeNS:
        return height >= width && features.maxColumnFill >= 0.45;
    case CursorKind::SizeNWSE:
        return width >= 10
            && height >= 10
            && features.mainDiagonalFill >= 0.42
            && features.mainDiagonalFill > features.antiDiagonalFill
            && diagonalGap >= 0.05;
    case CursorKind::SizeNESW:
        return width >= 10
            && height >= 10
            && features.antiDiagonalFill >= 0.42
            && features.antiDiagonalFill > features.mainDiagonalFill
            && diagonalGap >= 0.05;
    case CursorKind::Cross:
        return std::abs(width - height) <= 6
            && features.mainDiagonalFill >= 0.32
            && features.antiDiagonalFill >= 0.32;
    default:
        return true;
    }
}

bool isKnownRemoteIBeamSignature(const CursorMaskFeatures &features, PointI hotspot)
{
    if (!features.valid)
        return false;

    const int width = features.right - features.left + 1;
    const int height = features.bottom - features.top + 1;
    const double diagonalBalance = std::abs(features.mainDiagonalFill - features.antiDiagonalFill);

    return width == 7
        && height == 16
        && hotspot.x == 8
        && hotspot.y == 9
        && features.left == 7
        && features.top == 2
        && features.maxColumnIndex == 3
        && features.maxRowIndex == 0
        && features.maxColumnFill >= 0.80
        && features.maxRowFill >= 0.80
        && features.mainDiagonalFill >= 0.65
        && features.antiDiagonalFill >= 0.65
        && diagonalBalance <= 0.05;
}

bool isGenericRemoteIBeamSignature(const CursorMaskFeatures &features, PointI hotspot)
{
    if (!features.valid)
        return false;

    const int width = features.right - features.left + 1;
    const int height = features.bottom - features.top + 1;
    const int centerX = features.left + (width / 2);
    const int hotspotDistanceFromCenter = std::abs(hotspot.x - centerX);

    return width <= 9
        && height >= 14
        && (height >= (width * 2))
        && hotspotDistanceFromCenter <= 3
        && hotspot.y >= features.top
        && hotspot.y <= features.bottom
        && features.maxColumnFill >= 0.78
        && features.maxRowFill >= 0.55;
}

bool isLooseIBeamSignature(const CursorMaskFeatures &features)
{
    if (!features.valid)
        return false;

    const int width = features.right - features.left + 1;
    const int height = features.bottom - features.top + 1;

    return width <= 12
        && height >= 14
        && height >= (width * 2)
        && features.maxColumnFill >= 0.78
        && features.maxRowFill >= 0.35;
}

bool shouldUseLocalIBeamCursor(const CursorMaskFeatures &features, PointI hotspot)
{
    return isKnownRemoteIBeamSignature(features, hotspot)
        || isGenericRemoteIBeamSignature(features, hotspot)
        || isLooseIBeamSignature(features);
}

HCURSOR loadSystemCursor(CursorKind kind)
{
    switch (kind) {
    case CursorKind::IBeam: return LoadCursor(nullptr, IDC_IBEAM);
    case CursorKind::Cross: return LoadCursor(nullptr, IDC_CROSS);
    case CursorKind::Wait: return LoadCursor(nullptr, IDC_WAIT);
    case CursorKind::AppStarting: return LoadCursor(nullptr, IDC_APPSTARTING);
    case CursorKind::Hand: return LoadCursor(nullptr, IDC_HAND);
    case CursorKind::SizeWE: return LoadCursor(nullptr, IDC_SIZEWE);
    case CursorKind::SizeNS: return LoadCursor(nullptr, IDC_SIZENS);
    case CursorKind::SizeNWSE: return LoadCursor(nullptr, IDC_SIZENWSE);
    case CursorKind::SizeNESW: return LoadCursor(nullptr, IDC_SIZENESW);
    case CursorKind::SizeAll: return LoadCursor(nullptr, IDC_SIZEALL);
    case CursorKind::Hidden:
        return nullptr;
    case CursorKind::Arrow:
    case CursorKind::Custom:
    default:
        return LoadCursor(nullptr, IDC_ARROW);
    }
}

std::vector<CursorCandidate> makeSystemCursorCandidates()
{
    const CursorCandidate seed[] = {
        { CursorKind::Arrow, IDC_ARROW, {}, 0.12 },
        { CursorKind::IBeam, IDC_IBEAM, {}, 0.18 },
        { CursorKind::SizeWE, IDC_SIZEWE, {}, 0.20 },
        { CursorKind::SizeNS, IDC_SIZENS, {}, 0.20 },
        { CursorKind::SizeNWSE, IDC_SIZENWSE, {}, 0.20 },
        { CursorKind::SizeNESW, IDC_SIZENESW, {}, 0.20 },
        { CursorKind::SizeAll, IDC_SIZEALL, {}, 0.20 },
        { CursorKind::Hand, IDC_HAND, {}, 0.18 },
        { CursorKind::Wait, IDC_WAIT, {}, 0.18 },
        { CursorKind::AppStarting, IDC_APPSTARTING, {}, 0.18 }
    };

    std::vector<CursorCandidate> candidates(std::begin(seed), std::end(seed));
    for (auto &candidate : candidates) {
        PointI hotspot;
        candidate.mask = normalizedCursorMask(cursorFrameBufferFromHandle(LoadCursor(nullptr, candidate.idcName), &hotspot));
    }

    return candidates;
}

const std::vector<CursorCandidate> &systemCursorCandidates()
{
    static const std::vector<CursorCandidate> candidates = makeSystemCursorCandidates();
    return candidates;
}

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
    HBITMAP colorBitmap = screenDc ? CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS,
                                                     &bits, nullptr, 0) : nullptr;
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

        const int maskStride = ((remoteImage.width + 31) / 32) * 4;
        std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>(maskStride)
            * static_cast<std::size_t>(remoteImage.height), 0x00);
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
}

namespace RdpCursorClassifier
{
std::optional<CursorKind> classifyShape(const FrameBuffer &remoteImage, PointI hotspot)
{
    const FrameBuffer cursorFrame = opaqueCursorFrame(remoteImage);
    return classifyShapeFromPreparedFrame(cursorFrame, hotspot);
}

CursorInfo createCursor(const FrameBuffer &remoteImage, PointI hotspot)
{
    const FrameBuffer cursorFrame = opaqueCursorFrame(remoteImage);

    if (const auto kind = classifyShapeFromPreparedFrame(cursorFrame, hotspot))
        return CursorInfo{*kind, nullptr, false};

    const HCURSOR cursorHandle = createCursorHandleFromFrame(cursorFrame, hotspot);
    return CursorInfo{CursorKind::Custom, cursorHandle, cursorHandle != nullptr};
}

HCURSOR cursorHandleFromInfo(const CursorInfo &cursorInfo)
{
    if (cursorInfo.kind == CursorKind::Custom)
        return cursorInfo.handle;

    if (cursorInfo.kind == CursorKind::Hidden)
        return nullptr;

    if (cursorInfo.handle)
        return cursorInfo.handle;

    return loadSystemCursor(cursorInfo.kind);
}
}
