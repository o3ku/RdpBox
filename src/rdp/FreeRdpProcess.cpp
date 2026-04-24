#include "FreeRdpProcess.h"
#include "RdpClipboardBridge.h"

#include <cerrno>
#include <QCursor>
#include <QColor>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>
#include <QRect>
#include <QSize>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <freerdp3/freerdp/channels/channels.h>
#include <freerdp3/freerdp/client.h>
#include <freerdp3/freerdp/client/channels.h>
#include <freerdp3/freerdp/client/cliprdr.h>
#include <freerdp3/freerdp/client/disp.h>
#include <freerdp3/freerdp/codec/color.h>
#include <freerdp3/freerdp/constants.h>
#include <freerdp3/freerdp/freerdp.h>
#include <freerdp3/freerdp/gdi/gdi.h>
#include <freerdp3/freerdp/graphics.h>
#include <freerdp3/freerdp/input.h>
#include <freerdp3/freerdp/locale/locale.h>
#include <freerdp3/freerdp/locale/keyboard.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

namespace
{
struct QtRdpContext
{
    rdpClientContext common;
    FreeRdpProcess *owner = nullptr;
    DispClientContext *disp = nullptr;
    bool ignoreCertificate = true;
};

struct QtPointer : rdpPointer
{
    QCursor cursor;
};

struct CursorCandidate
{
    Qt::CursorShape shape;
    LPCTSTR idcName;
    QImage mask;
    double maxDistance = 0.22;
};

struct CursorMaskFeatures
{
    bool valid = false;
    QRect bounds;
    int occupiedPixels = 0;
    double maxColumnFill = 0.0;
    double maxRowFill = 0.0;
    double mainDiagonalFill = 0.0;
    double antiDiagonalFill = 0.0;
    int maxColumnIndex = 0;
    int maxRowIndex = 0;
};

struct CursorPattern
{
    CursorMaskFeatures features;
    QPoint hotspot;
};

static QtRdpContext *toQtContext(rdpContext *context)
{
    return reinterpret_cast<QtRdpContext*>(context);
}

static const QtRdpContext *toQtContext(const rdpContext *context)
{
    return reinterpret_cast<const QtRdpContext*>(context);
}

static QtPointer *toQtPointer(rdpPointer *pointer)
{
    return reinterpret_cast<QtPointer*>(pointer);
}

static const QtPointer *toQtPointer(const rdpPointer *pointer)
{
    return reinterpret_cast<const QtPointer*>(pointer);
}

static QImage cursorImageFromHandle(HCURSOR cursorHandle, QPoint *hotspot = nullptr)
{
    if (!cursorHandle)
        return {};

    ICONINFO iconInfo = {};
    if (!GetIconInfo(cursorHandle, &iconInfo))
        return {};

    if (hotspot)
        *hotspot = QPoint(static_cast<int>(iconInfo.xHotspot), static_cast<int>(iconInfo.yHotspot));

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
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP dib = CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                   &bits, nullptr, 0);
    QImage result;

    if (dib && bits) {
        HGDIOBJ oldBitmap = SelectObject(memDc, dib);
        PatBlt(memDc, 0, 0, width, height, BLACKNESS);
        DrawIconEx(memDc, 0, 0, cursorHandle, width, height, 0, nullptr, DI_NORMAL);
        result = QImage(reinterpret_cast<uchar*>(bits), width, height,
                        QImage::Format_ARGB32_Premultiplied).copy();
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

static QImage normalizedCursorMask(const QImage &source)
{
    if (source.isNull())
        return {};

    QImage normalized(32, 32, QImage::Format_Grayscale8);
    normalized.fill(0);

    QImage scaled = source.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                        .scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    for (int y = 0; y < scaled.height(); ++y) {
        uchar *dst = normalized.scanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            const QColor color = scaled.pixelColor(x, y);
            const bool occupied = color.alpha() > 24
                && (color.red() < 250 || color.green() < 250 || color.blue() < 250 || color.alpha() > 200);
            dst[x] = occupied ? 255 : 0;
        }
    }

    return normalized;
}

static double cursorMaskDistance(const QImage &left, const QImage &right)
{
    if (left.isNull() || right.isNull() || left.size() != right.size())
        return 1.0;

    int diff = 0;
    const int total = left.width() * left.height();
    for (int y = 0; y < left.height(); ++y) {
        const uchar *lhs = left.constScanLine(y);
        const uchar *rhs = right.constScanLine(y);
        for (int x = 0; x < left.width(); ++x) {
            if ((lhs[x] > 0) != (rhs[x] > 0))
                ++diff;
        }
    }

    return static_cast<double>(diff) / static_cast<double>(total);
}

static CursorMaskFeatures analyzeCursorMask(const QImage &mask)
{
    CursorMaskFeatures features;

    if (mask.isNull())
        return features;

    int minX = mask.width();
    int minY = mask.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < mask.height(); ++y) {
        const uchar *row = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (row[x] == 0)
                continue;

            ++features.occupiedPixels;
            if (x < minX)
                minX = x;
            if (y < minY)
                minY = y;
            if (x > maxX)
                maxX = x;
            if (y > maxY)
                maxY = y;
        }
    }

    if (features.occupiedPixels == 0)
        return features;

    features.valid = true;
    features.bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));

    const int boundsWidth = features.bounds.width();
    const int boundsHeight = features.bounds.height();
    std::vector<int> columnHits(boundsWidth, 0);
    std::vector<int> rowHits(boundsHeight, 0);
    int mainDiagonalHits = 0;
    int antiDiagonalHits = 0;

    const double xFromYScale = (boundsHeight > 1)
        ? static_cast<double>(boundsWidth - 1) / static_cast<double>(boundsHeight - 1)
        : 0.0;

    for (int y = minY; y <= maxY; ++y) {
        const uchar *row = mask.constScanLine(y);
        for (int x = minX; x <= maxX; ++x) {
            if (row[x] == 0)
                continue;

            const int localX = x - minX;
            const int localY = y - minY;
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

    const int maxColumnHits = *std::max_element(columnHits.begin(), columnHits.end());
    const int maxRowHits = *std::max_element(rowHits.begin(), rowHits.end());
    features.maxColumnIndex = static_cast<int>(std::distance(
        columnHits.begin(), std::max_element(columnHits.begin(), columnHits.end())));
    features.maxRowIndex = static_cast<int>(std::distance(
        rowHits.begin(), std::max_element(rowHits.begin(), rowHits.end())));
    features.maxColumnFill = static_cast<double>(maxColumnHits) / static_cast<double>(boundsHeight);
    features.maxRowFill = static_cast<double>(maxRowHits) / static_cast<double>(boundsWidth);
    features.mainDiagonalFill = static_cast<double>(mainDiagonalHits) / static_cast<double>(features.occupiedPixels);
    features.antiDiagonalFill = static_cast<double>(antiDiagonalHits) / static_cast<double>(features.occupiedPixels);

    return features;
}

static const CursorPattern &localIBeamPattern()
{
    static const CursorPattern pattern = []() {
        CursorPattern value;
        const QImage image = cursorImageFromHandle(LoadCursor(nullptr, IDC_IBEAM), &value.hotspot);
        value.features = analyzeCursorMask(normalizedCursorMask(image));
        return value;
    }();

    return pattern;
}

static bool isCandidateCompatible(Qt::CursorShape shape, const CursorMaskFeatures &features)
{
    if (!features.valid)
        return false;

    const int width = features.bounds.width();
    const int height = features.bounds.height();

    switch (shape) {
    case Qt::SizeHorCursor:
        return width >= height && features.maxRowFill >= 0.45;
    case Qt::SizeVerCursor:
        return height >= width && features.maxColumnFill >= 0.45;
    default:
        return true;
    }
}

static bool isLikelyIBeam(const CursorMaskFeatures &features, const QPoint &hotspot)
{
    if (!features.valid)
        return false;

    const CursorPattern &localPattern = localIBeamPattern();
    if (!localPattern.features.valid)
        return false;

    const int width = features.bounds.width();
    const int height = features.bounds.height();
    if (width <= 0 || height <= 0)
        return false;

    const double dominantDiagonal = (features.mainDiagonalFill > features.antiDiagonalFill)
        ? features.mainDiagonalFill
        : features.antiDiagonalFill;
    const double diagonalBalance = std::abs(features.mainDiagonalFill - features.antiDiagonalFill);
    const int centerX = width / 2;
    const int columnDistanceToCenter = std::abs(features.maxColumnIndex - centerX);
    const int hotspotX = hotspot.x() - features.bounds.left();
    const int hotspotY = hotspot.y() - features.bounds.top();
    const bool hotspotInsideBounds =
        hotspot.x() >= features.bounds.left() && hotspot.x() <= features.bounds.right()
        && hotspot.y() >= features.bounds.top() && hotspot.y() <= features.bounds.bottom();
    const int hotspotDistanceToStem = std::abs(features.maxColumnIndex - hotspotX);
    const int hotspotDistanceToCenter = std::abs(hotspotX - centerX);
    const int localWidth = localPattern.features.bounds.width();
    const int localHeight = localPattern.features.bounds.height();
    const int localHotspotX = localPattern.hotspot.x() - localPattern.features.bounds.left();
    const int localHotspotY = localPattern.hotspot.y() - localPattern.features.bounds.top();

    return hotspotInsideBounds
        && width <= (localWidth + 6)
        && height >= (localHeight - 8)
        && height >= (width * 2)
        && features.maxColumnFill >= (localPattern.features.maxColumnFill - 0.22)
        && features.maxRowFill >= 0.72
        && dominantDiagonal >= 0.45
        && dominantDiagonal <= 0.85
        && diagonalBalance <= 0.10
        && columnDistanceToCenter <= ((width / 3) + 1)
        && hotspotDistanceToStem <= 2
        && hotspotDistanceToCenter <= ((width / 3) + 1)
        && std::abs(hotspotX - localHotspotX) <= 3
        && std::abs(hotspotY - localHotspotY) <= 8;
}

static bool isKnownRemoteIBeamSignature(const CursorMaskFeatures &features, const QPoint &hotspot)
{
    if (!features.valid)
        return false;

    const int width = features.bounds.width();
    const int height = features.bounds.height();
    const double diagonalBalance = std::abs(features.mainDiagonalFill - features.antiDiagonalFill);

    return width == 7
        && height == 16
        && hotspot.x() == 8
        && hotspot.y() == 9
        && features.bounds.left() == 7
        && features.bounds.top() == 2
        && features.maxColumnIndex == 3
        && features.maxRowIndex == 0
        && features.maxColumnFill >= 0.80
        && features.maxRowFill >= 0.80
        && features.mainDiagonalFill >= 0.65
        && features.antiDiagonalFill >= 0.65
        && diagonalBalance <= 0.05;
}

static bool shouldUseLocalIBeamCursor(const CursorMaskFeatures &features, const QPoint &hotspot)
{
    return isKnownRemoteIBeamSignature(features, hotspot)
        || isLikelyIBeam(features, hotspot);
}

static const std::vector<CursorCandidate> &systemCursorCandidates()
{
    static const std::vector<CursorCandidate> candidates = []() {
        const CursorCandidate seed[] = {
            { Qt::ArrowCursor, IDC_ARROW, {}, 0.12 },
            { Qt::SizeHorCursor, IDC_SIZEWE, {}, 0.20 },
            { Qt::SizeVerCursor, IDC_SIZENS, {}, 0.20 },
            { Qt::SizeAllCursor, IDC_SIZEALL, {}, 0.20 },
            { Qt::CrossCursor, IDC_CROSS, {}, 0.18 },
            { Qt::PointingHandCursor, IDC_HAND, {}, 0.18 },
            { Qt::WaitCursor, IDC_WAIT, {}, 0.18 },
            { Qt::BusyCursor, IDC_APPSTARTING, {}, 0.18 }
        };

        std::vector<CursorCandidate> items(std::begin(seed), std::end(seed));
        for (auto &item : items) {
            QPoint hotspot;
            item.mask = normalizedCursorMask(cursorImageFromHandle(LoadCursor(nullptr, item.idcName), &hotspot));
        }
        return items;
    }();

    return candidates;
}

static std::optional<QCursor> mapToSystemCursor(const QImage &remoteImage, const QPoint &hotspot)
{
    const QImage remoteMask = normalizedCursorMask(remoteImage);
    if (remoteMask.isNull())
        return std::nullopt;

    const CursorMaskFeatures remoteFeatures = analyzeCursorMask(remoteMask);
    if (!remoteFeatures.valid)
        return std::nullopt;

    if (shouldUseLocalIBeamCursor(remoteFeatures, hotspot))
        return QCursor(Qt::IBeamCursor);

    const auto &candidates = systemCursorCandidates();
    double bestScore = 1.0;
    std::optional<Qt::CursorShape> bestShape;
    double bestThreshold = 0.0;

    for (const auto &candidate : candidates) {
        if (candidate.mask.isNull())
            continue;
        if (!isCandidateCompatible(candidate.shape, remoteFeatures))
            continue;

        const double score = cursorMaskDistance(remoteMask, candidate.mask);
        if (score < bestScore) {
            bestScore = score;
            bestShape = candidate.shape;
            bestThreshold = candidate.maxDistance;
        }
    }

    if (!bestShape.has_value() || bestScore > bestThreshold)
        return std::nullopt;

    return QCursor(*bestShape);
}

static UINT16 currentToggleState()
{
    UINT16 syncFlags = 0;

    if (GetKeyState(VK_NUMLOCK) & 0x1)
        syncFlags |= KBD_SYNC_NUM_LOCK;
    if (GetKeyState(VK_CAPITAL) & 0x1)
        syncFlags |= KBD_SYNC_CAPS_LOCK;
    if (GetKeyState(VK_SCROLL) & 0x1)
        syncFlags |= KBD_SYNC_SCROLL_LOCK;
    if (GetKeyState(VK_KANA) & 0x1)
        syncFlags |= KBD_SYNC_KANA_LOCK;

    return syncFlags;
}

static BOOL qtfreerdp_begin_paint(rdpContext *context)
{
    if (!context || !context->gdi || !context->gdi->primary || !context->gdi->primary->hdc)
        return FALSE;

    HGDI_DC hdc = context->gdi->primary->hdc;
    if (!hdc || !hdc->hwnd || !hdc->hwnd->invalid)
        return FALSE;

    hdc->hwnd->invalid->null = TRUE;
    hdc->hwnd->ninvalid = 0;
    return TRUE;
}

static void qtfreerdp_copy_frame(rdpContext *context)
{
    if (!context)
        return;

    auto *qtContext = toQtContext(context);
    if (!qtContext || !qtContext->owner || !context->gdi || !context->gdi->primary_buffer)
        return;

    const int width = context->gdi->width;
    const int height = context->gdi->height;
    const int stride = static_cast<int>(context->gdi->stride);

    if (width <= 0 || height <= 0 || stride <= 0)
        return;

    const QImage image(context->gdi->primary_buffer, width, height, stride, QImage::Format_RGB32);
    const QImage copy = image.copy();

    QMetaObject::invokeMethod(qtContext->owner, [owner = qtContext->owner, copy, width, height]() {
        if (owner)
            owner->updateFrameFromBackend(copy, QSize(width, height));
    }, Qt::QueuedConnection);
}

static BOOL qtfreerdp_end_paint(rdpContext *context)
{
    if (!context || !context->gdi || !context->gdi->primary || !context->gdi->primary->hdc)
        return FALSE;

    HGDI_DC hdc = context->gdi->primary->hdc;
    if (!hdc || !hdc->hwnd || !hdc->hwnd->invalid)
        return FALSE;

    if (hdc->hwnd->invalid->null)
        return TRUE;

    qtfreerdp_copy_frame(context);
    return TRUE;
}

static BOOL qtfreerdp_desktop_resize(rdpContext *context)
{
    if (!context || !context->gdi || !context->settings)
        return FALSE;

    if (!gdi_resize(context->gdi,
                    freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                    freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight))) {
        return FALSE;
    }

    qtfreerdp_copy_frame(context);
    return TRUE;
}

static BOOL qtfreerdp_pointer_new(rdpContext *context, rdpPointer *pointer)
{
    if (!context || !pointer || !context->gdi)
        return FALSE;

    auto *qtPointer = toQtPointer(pointer);
    const int width = static_cast<int>(pointer->width);
    const int height = static_cast<int>(pointer->height);

    if (width <= 0 || height <= 0)
        return FALSE;

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return FALSE;

    if (!freerdp_image_copy_from_pointer_data(
            image.bits(), PIXEL_FORMAT_BGRA32, static_cast<UINT32>(image.bytesPerLine()), 0, 0,
            pointer->width, pointer->height, pointer->xorMaskData, pointer->lengthXorMask,
            pointer->andMaskData, pointer->lengthAndMask, pointer->xorBpp, &context->gdi->palette)) {
        return FALSE;
    }

    const QPoint hotspot(static_cast<int>(pointer->xPos), static_cast<int>(pointer->yPos));

    if (const auto systemCursor = mapToSystemCursor(image, hotspot)) {
        qtPointer->cursor = *systemCursor;
    } else {
        qtPointer->cursor = QCursor(QPixmap::fromImage(image),
                                    hotspot.x(),
                                    hotspot.y());
    }
    return TRUE;
}

static void qtfreerdp_pointer_free(rdpContext *context, rdpPointer *pointer)
{
    Q_UNUSED(context);

    if (!pointer)
        return;

    toQtPointer(pointer)->cursor = QCursor();
}

static BOOL qtfreerdp_pointer_set(rdpContext *context, rdpPointer *pointer)
{
    if (!context || !pointer)
        return FALSE;

    auto *qtContext = toQtContext(context);
    if (!qtContext || !qtContext->owner)
        return FALSE;

    const QCursor cursor = toQtPointer(pointer)->cursor;
    QMetaObject::invokeMethod(qtContext->owner, [owner = qtContext->owner, cursor]() {
        if (owner)
            owner->updateCursorFromBackend(cursor, false);
    }, Qt::QueuedConnection);
    return TRUE;
}

static BOOL qtfreerdp_pointer_set_null(rdpContext *context)
{
    auto *qtContext = toQtContext(context);
    if (!qtContext || !qtContext->owner)
        return FALSE;

    QMetaObject::invokeMethod(qtContext->owner, [owner = qtContext->owner]() {
        if (owner)
            owner->updateCursorFromBackend(QCursor(Qt::BlankCursor), true);
    }, Qt::QueuedConnection);
    return TRUE;
}

static BOOL qtfreerdp_pointer_set_default(rdpContext *context)
{
    auto *qtContext = toQtContext(context);
    if (!qtContext || !qtContext->owner)
        return FALSE;

    QMetaObject::invokeMethod(qtContext->owner, [owner = qtContext->owner]() {
        if (owner)
            owner->resetCursorFromBackend();
    }, Qt::QueuedConnection);
    return TRUE;
}

static BOOL qtfreerdp_pointer_set_position(rdpContext *context, UINT32 x, UINT32 y)
{
    Q_UNUSED(context);
    Q_UNUSED(x);
    Q_UNUSED(y);
    return TRUE;
}

static BOOL qtfreerdp_register_pointer(rdpGraphics *graphics)
{
    if (!graphics)
        return FALSE;

    rdpPointer pointer = {};
    pointer.size = sizeof(QtPointer);
    pointer.New = qtfreerdp_pointer_new;
    pointer.Free = qtfreerdp_pointer_free;
    pointer.Set = qtfreerdp_pointer_set;
    pointer.SetNull = qtfreerdp_pointer_set_null;
    pointer.SetDefault = qtfreerdp_pointer_set_default;
    pointer.SetPosition = qtfreerdp_pointer_set_position;
    graphics_register_pointer(graphics, &pointer);
    return TRUE;
}

static void qtfreerdp_on_channel_connected(void *context, const ChannelConnectedEventArgs *e)
{
    auto *qtContext = reinterpret_cast<QtRdpContext*>(context);
    if (!qtContext || !e)
        return;

    if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
        qtContext->disp = reinterpret_cast<DispClientContext*>(e->pInterface);
        return;
    }

    if (strcmp(e->name, CLIPRDR_CHANNEL_NAME) == 0) {
        if (qtContext->owner)
            qtContext->owner->attachClipboardChannel(e->pInterface);
        return;
    }

    freerdp_client_OnChannelConnectedEventHandler(context, e);
}

static void qtfreerdp_on_channel_disconnected(void *context, const ChannelDisconnectedEventArgs *e)
{
    auto *qtContext = reinterpret_cast<QtRdpContext*>(context);
    if (!qtContext || !e)
        return;

    if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
        qtContext->disp = nullptr;
        return;
    }

    if (strcmp(e->name, CLIPRDR_CHANNEL_NAME) == 0) {
        if (qtContext->owner)
            qtContext->owner->detachClipboardChannel();
        return;
    }

    freerdp_client_OnChannelDisconnectedEventHandler(context, e);
}

static BOOL qtfreerdp_pre_connect(freerdp *instance)
{
    if (!instance || !instance->context || !instance->context->settings)
        return FALSE;

    rdpSettings *settings = instance->context->settings;

    if (!freerdp_settings_set_uint32(settings, FreeRDP_OsMajorType, OSMAJORTYPE_WINDOWS))
        return FALSE;
    if (!freerdp_settings_set_uint32(settings, FreeRDP_OsMinorType, OSMINORTYPE_WINDOWS_NT))
        return FALSE;

    UINT32 width = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
    if (width > 0) {
        width = (width + 3U) & ~3U;
        if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, width))
            return FALSE;
    }

    DWORD keyboardLayoutId = freerdp_settings_get_uint32(settings, FreeRDP_KeyboardLayout);
    CHAR name[KL_NAMELENGTH + 1] = {};
    if (GetKeyboardLayoutNameA(name)) {
        errno = 0;
        const unsigned long layout = strtoul(name, nullptr, 16);
        if (errno == 0)
            keyboardLayoutId = static_cast<DWORD>(layout);
    }

    if (keyboardLayoutId == 0) {
        const HKL layout = GetKeyboardLayout(0);
        keyboardLayoutId = static_cast<DWORD>((reinterpret_cast<uintptr_t>(layout) >> 16) & 0xFFFF);
    }

    if (keyboardLayoutId == 0)
        freerdp_detect_keyboard_layout_from_system_locale(&keyboardLayoutId);
    if (keyboardLayoutId == 0)
        keyboardLayoutId = ENGLISH_UNITED_STATES;
    if (!freerdp_settings_set_uint32(settings, FreeRDP_KeyboardLayout, keyboardLayoutId))
        return FALSE;

    PubSub_SubscribeChannelConnected(instance->context->pubSub, qtfreerdp_on_channel_connected);
    PubSub_SubscribeChannelDisconnected(instance->context->pubSub, qtfreerdp_on_channel_disconnected);
    return TRUE;
}

static BOOL qtfreerdp_post_connect(freerdp *instance)
{
    if (!instance || !instance->context || !instance->context->update)
        return FALSE;

    if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
        return FALSE;

    instance->context->update->BeginPaint = qtfreerdp_begin_paint;
    instance->context->update->EndPaint = qtfreerdp_end_paint;
    instance->context->update->DesktopResize = qtfreerdp_desktop_resize;
    qtfreerdp_register_pointer(instance->context->graphics);

    auto *qtContext = toQtContext(instance->context);
    if (qtContext && qtContext->owner) {
        QMetaObject::invokeMethod(qtContext->owner, [owner = qtContext->owner]() {
            if (owner)
                owner->updateStateFromBackend(FreeRdpProcess::State::Running);
        }, Qt::QueuedConnection);
    }

    qtfreerdp_copy_frame(instance->context);
    return TRUE;
}

static void qtfreerdp_post_disconnect(freerdp *instance)
{
    if (!instance || !instance->context)
        return;

    PubSub_UnsubscribeChannelConnected(instance->context->pubSub, qtfreerdp_on_channel_connected);
    PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, qtfreerdp_on_channel_disconnected);
    gdi_free(instance);
}

static BOOL qtfreerdp_authenticate_ex(freerdp *instance, char **username, char **password,
                                      char **domain, rdp_auth_reason reason)
{
    Q_UNUSED(reason);

    if (!instance || !instance->context || !instance->context->settings)
        return FALSE;

    const char *settingUser = freerdp_settings_get_string(instance->context->settings, FreeRDP_Username);
    const char *settingPassword = freerdp_settings_get_string(instance->context->settings, FreeRDP_Password);
    const char *settingDomain = freerdp_settings_get_string(instance->context->settings, FreeRDP_Domain);

    if (!settingUser || !settingPassword)
        return FALSE;

    free(*username);
    free(*password);
    free(*domain);

    *username = _strdup(settingUser);
    *password = _strdup(settingPassword);
    *domain = settingDomain ? _strdup(settingDomain) : _strdup("");

    return *username && *password && *domain;
}

static DWORD qtfreerdp_verify_certificate_ex(freerdp *instance, const char *host, UINT16 port,
                                             const char *commonName, const char *subject,
                                             const char *issuer, const char *fingerprint, DWORD flags)
{
    Q_UNUSED(host);
    Q_UNUSED(port);
    Q_UNUSED(commonName);
    Q_UNUSED(subject);
    Q_UNUSED(issuer);
    Q_UNUSED(fingerprint);
    Q_UNUSED(flags);

    const auto *qtContext = instance ? toQtContext(instance->context) : nullptr;
    return (qtContext && qtContext->ignoreCertificate) ? 2 : 0;
}

static DWORD qtfreerdp_verify_changed_certificate_ex(freerdp *instance, const char *host, UINT16 port,
                                                     const char *commonName, const char *subject,
                                                     const char *issuer, const char *newFingerprint,
                                                     const char *oldSubject, const char *oldIssuer,
                                                     const char *oldFingerprint, DWORD flags)
{
    Q_UNUSED(host);
    Q_UNUSED(port);
    Q_UNUSED(commonName);
    Q_UNUSED(subject);
    Q_UNUSED(issuer);
    Q_UNUSED(newFingerprint);
    Q_UNUSED(oldSubject);
    Q_UNUSED(oldIssuer);
    Q_UNUSED(oldFingerprint);
    Q_UNUSED(flags);

    const auto *qtContext = instance ? toQtContext(instance->context) : nullptr;
    return (qtContext && qtContext->ignoreCertificate) ? 2 : 0;
}

static BOOL qtfreerdp_global_init()
{
    WSADATA data = {};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

static void qtfreerdp_global_uninit()
{
    WSACleanup();
}

static BOOL qtfreerdp_client_new(freerdp *instance, rdpContext *context)
{
    if (!instance || !context)
        return FALSE;

    instance->PreConnect = qtfreerdp_pre_connect;
    instance->PostConnect = qtfreerdp_post_connect;
    instance->PostDisconnect = qtfreerdp_post_disconnect;
    instance->AuthenticateEx = qtfreerdp_authenticate_ex;
    instance->VerifyCertificateEx = qtfreerdp_verify_certificate_ex;
    instance->VerifyChangedCertificateEx = qtfreerdp_verify_changed_certificate_ex;
    return TRUE;
}

static void qtfreerdp_client_free(freerdp *instance, rdpContext *context)
{
    Q_UNUSED(instance);
    Q_UNUSED(context);
}

static int qtfreerdp_client_start(rdpContext *context)
{
    if (!context || !context->instance)
        return -1;

    return freerdp_client_load_channels(context->instance) ? 0 : -1;
}

static int qtfreerdp_client_stop(rdpContext *context)
{
    Q_UNUSED(context);
    return 0;
}

static QPoint clampToDesktop(const QPoint &point, const QSize &desktop)
{
    if (desktop.width() <= 0 || desktop.height() <= 0)
        return {};

    return {
        qBound(0, point.x(), desktop.width() - 1),
        qBound(0, point.y(), desktop.height() - 1)
    };
}
}

struct FreeRdpProcess::Private
{
    rdpContext *context = nullptr;
    std::thread worker;
    std::atomic_bool stopRequested = false;
    mutable QMutex mutex;
    QImage frame;
    QSize desktopSize;
    QCursor cursor;
    bool cursorHidden = false;
    bool hasFirstFrame = false;
    std::unique_ptr<RdpClipboardBridge> clipboard;
    State state = State::Idle;
};

FreeRdpProcess::FreeRdpProcess(QObject *parent)
    : QObject(parent)
    , m_d(std::make_unique<Private>())
{
}

FreeRdpProcess::~FreeRdpProcess()
{
    stop();
}

FreeRdpProcess::State FreeRdpProcess::state() const
{
    return m_d->state;
}

QImage FreeRdpProcess::frame() const
{
    QMutexLocker locker(&m_d->mutex);
    return m_d->frame;
}

QSize FreeRdpProcess::desktopSize() const
{
    QMutexLocker locker(&m_d->mutex);
    return m_d->desktopSize;
}

QCursor FreeRdpProcess::cursor() const
{
    QMutexLocker locker(&m_d->mutex);
    return m_d->cursor;
}

void FreeRdpProcess::start(const QString &host,
                           int port,
                           const QString &username,
                           const QString &password,
                           int width,
                           int height,
                           bool clipboardEnabled,
                           bool ignoreCertificate)
{
    stop();

    RDP_CLIENT_ENTRY_POINTS entryPoints = {};
    entryPoints.Size = sizeof(entryPoints);
    entryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
    entryPoints.GlobalInit = qtfreerdp_global_init;
    entryPoints.GlobalUninit = qtfreerdp_global_uninit;
    entryPoints.ContextSize = sizeof(QtRdpContext);
    entryPoints.ClientNew = qtfreerdp_client_new;
    entryPoints.ClientFree = qtfreerdp_client_free;
    entryPoints.ClientStart = qtfreerdp_client_start;
    entryPoints.ClientStop = qtfreerdp_client_stop;

    m_d->context = freerdp_client_context_new(&entryPoints);
    if (!m_d->context) {
        setState(State::Finished);
        return;
    }

    auto *context = toQtContext(m_d->context);
    context->owner = this;
    context->ignoreCertificate = ignoreCertificate;

    rdpSettings *settings = m_d->context->settings;
    const UINT32 desktopWidth = static_cast<UINT32>((width > 640) ? width : 640);
    const UINT32 desktopHeight = static_cast<UINT32>((height > 480) ? height : 480);

    const bool configured =
        freerdp_settings_set_string(settings, FreeRDP_ServerHostname, host.toUtf8().constData()) &&
        freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, static_cast<UINT32>(port)) &&
        freerdp_settings_set_string(settings, FreeRDP_Username, username.toUtf8().constData()) &&
        freerdp_settings_set_string(settings, FreeRDP_Password, password.toUtf8().constData()) &&
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, desktopWidth) &&
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, desktopHeight) &&
        freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32) &&
        freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, ignoreCertificate) &&
        freerdp_settings_set_bool(settings, FreeRDP_SoftwareGdi, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, clipboardEnabled);

    if (!configured || freerdp_client_start(m_d->context) != 0) {
        freerdp_client_context_free(m_d->context);
        m_d->context = nullptr;
        setState(State::Finished);
        return;
    }

    {
        QMutexLocker locker(&m_d->mutex);
        m_d->frame = QImage();
        m_d->desktopSize = QSize(static_cast<int>(desktopWidth), static_cast<int>(desktopHeight));
        m_d->cursor = QCursor(Qt::ArrowCursor);
        m_d->cursorHidden = false;
        m_d->hasFirstFrame = false;
    }

    m_d->stopRequested = false;
    setState(State::Starting);

    m_d->worker = std::thread([this]() {
        auto *context = m_d->context;
        if (!context || !context->instance) {
            QMetaObject::invokeMethod(this, [this]() {
                updateStateFromBackend(State::Finished);
            }, Qt::QueuedConnection);
            return;
        }

        freerdp *instance = context->instance;
        const BOOL connected = freerdp_connect(instance);

        if (connected) {
            while (!m_d->stopRequested && !freerdp_shall_disconnect_context(context)) {
                HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
                const DWORD handleCount =
                    freerdp_get_event_handles(context, handles, MAXIMUM_WAIT_OBJECTS);

                if (handleCount == 0)
                    break;

                const DWORD waitResult = WaitForMultipleObjects(handleCount, handles, FALSE, 50);
                if (waitResult == WAIT_TIMEOUT)
                    continue;
                if (waitResult == WAIT_FAILED)
                    break;
                if (!freerdp_check_event_handles(context))
                    break;
            }
        }

        freerdp_abort_connect_context(context);
        freerdp_disconnect(instance);

        QMetaObject::invokeMethod(this, [this]() {
            updateStateFromBackend(State::Finished);
        }, Qt::QueuedConnection);
    });
}

void FreeRdpProcess::stop()
{
    m_d->stopRequested = true;

    if (m_d->context)
        freerdp_abort_connect_context(m_d->context);

    if (m_d->worker.joinable())
        m_d->worker.join();

    if (m_d->context) {
        freerdp_client_stop(m_d->context);
        freerdp_client_context_free(m_d->context);
        m_d->context = nullptr;
    }

    {
        QMutexLocker locker(&m_d->mutex);
        m_d->frame = QImage();
        m_d->desktopSize = {};
        m_d->cursor = QCursor(Qt::ArrowCursor);
        m_d->cursorHidden = false;
        m_d->hasFirstFrame = false;
    }

    if (m_d->clipboard)
        m_d->clipboard->detach();
}

void FreeRdpProcess::sendFocusIn()
{
    if (!m_d->context || !m_d->context->input)
        return;

    freerdp_input_send_focus_in_event(m_d->context->input, currentToggleState());
}

void FreeRdpProcess::sendKeyMessage(quint32 message, quintptr wParam, qintptr lParam)
{
    if (!m_d->context || !m_d->context->input)
        return;

    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
        break;
    default:
        return;
    }

    UINT32 scanCode = (static_cast<UINT32>(lParam) >> 16) & 0xFFU;
    const bool extended = (static_cast<UINT32>(lParam) & 0x01000000U) != 0;
    const bool wasDown = (static_cast<UINT32>(lParam) & 0x40000000U) != 0;
    const bool isRelease = (static_cast<UINT32>(lParam) & 0x80000000U) != 0;

    if (scanCode == 0) {
        const UINT mapped = MapVirtualKeyW(static_cast<UINT>(wParam), MAPVK_VK_TO_VSC);
        scanCode = mapped & 0xFFU;
    }

    if (scanCode == 0)
        return;

    UINT32 rdpScancode = MAKE_RDP_SCANCODE(static_cast<BYTE>(scanCode), extended);

    if (rdpScancode == RDP_SCANCODE_NUMLOCK_EXTENDED) {
        rdpScancode = RDP_SCANCODE_NUMLOCK;
    } else if (rdpScancode == RDP_SCANCODE_NUMLOCK) {
        if (!isRelease)
            freerdp_input_send_keyboard_pause_event(m_d->context->input);
        return;
    } else if (rdpScancode == RDP_SCANCODE_RSHIFT_EXTENDED) {
        rdpScancode = RDP_SCANCODE_RSHIFT;
    }

    freerdp_input_send_keyboard_event_ex(m_d->context->input, !isRelease, wasDown, rdpScancode);
}

void FreeRdpProcess::sendMouseMove(const QPoint &pos, const QSize &viewSize)
{
    if (!m_d->context || viewSize.width() <= 0 || viewSize.height() <= 0)
        return;

    const QSize desktop = desktopSize();
    if (desktop.width() <= 0 || desktop.height() <= 0)
        return;

    const QPoint mapped = clampToDesktop({
        pos.x() * desktop.width() / viewSize.width(),
        pos.y() * desktop.height() / viewSize.height()
    }, desktop);

    freerdp_client_send_button_event(&toQtContext(m_d->context)->common,
                                     FALSE, PTR_FLAGS_MOVE, mapped.x(), mapped.y());
}

void FreeRdpProcess::sendMouseButton(Qt::MouseButton button, bool down,
                                     const QPoint &pos, const QSize &viewSize)
{
    if (!m_d->context)
        return;

    const QSize desktop = desktopSize();
    if (desktop.width() <= 0 || desktop.height() <= 0 || viewSize.width() <= 0 || viewSize.height() <= 0)
        return;

    const QPoint mapped = clampToDesktop({
        pos.x() * desktop.width() / viewSize.width(),
        pos.y() * desktop.height() / viewSize.height()
    }, desktop);

    if (button == Qt::BackButton || button == Qt::ForwardButton) {
        UINT16 flags = 0;
        flags |= (button == Qt::BackButton) ? PTR_XFLAGS_BUTTON1 : PTR_XFLAGS_BUTTON2;
        if (down)
            flags |= PTR_XFLAGS_DOWN;
        freerdp_client_send_extended_button_event(&toQtContext(m_d->context)->common,
                                                  FALSE, flags, mapped.x(), mapped.y());
        return;
    }

    UINT16 flags = 0;
    switch (button) {
    case Qt::LeftButton: flags |= PTR_FLAGS_BUTTON1; break;
    case Qt::RightButton: flags |= PTR_FLAGS_BUTTON2; break;
    case Qt::MiddleButton: flags |= PTR_FLAGS_BUTTON3; break;
    default: return;
    }

    if (down)
        flags |= PTR_FLAGS_DOWN;

    freerdp_client_send_button_event(&toQtContext(m_d->context)->common,
                                     FALSE, flags, mapped.x(), mapped.y());
}

void FreeRdpProcess::sendWheel(const QPoint &angleDelta, const QPoint &pos, const QSize &viewSize)
{
    if (!m_d->context)
        return;

    sendMouseMove(pos, viewSize);

    auto sendOne = [this](int delta, bool horizontal) {
        if (delta == 0)
            return;

        delta = std::clamp(delta, -255, 255);

        UINT16 flags = horizontal ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL;
        if (delta < 0) {
            flags |= PTR_FLAGS_WHEEL_NEGATIVE;
            delta = 0x100 + delta;
        }

        flags |= static_cast<UINT16>(delta);
        freerdp_client_send_wheel_event(&toQtContext(m_d->context)->common, flags);
    };

    if (angleDelta.y() != 0)
        sendOne(angleDelta.y(), false);
    else if (angleDelta.x() != 0)
        sendOne(angleDelta.x(), true);
}

void FreeRdpProcess::requestResize(const QSize &size)
{
    if (!m_d->context || size.width() <= 0 || size.height() <= 0)
        return;

    auto *context = toQtContext(m_d->context);
    if (!context || !context->disp || !context->disp->SendMonitorLayout)
        return;

    DISPLAY_CONTROL_MONITOR_LAYOUT layout = {};
    layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
    layout.Left = 0;
    layout.Top = 0;
    layout.Width = static_cast<UINT32>((size.width() + 3) & ~3);
    layout.Height = static_cast<UINT32>(size.height());
    layout.Orientation = ORIENTATION_LANDSCAPE;
    layout.DesktopScaleFactor = 100;
    layout.DeviceScaleFactor = 100;
    layout.PhysicalWidth = layout.Width;
    layout.PhysicalHeight = layout.Height;

    context->disp->SendMonitorLayout(context->disp, 1, &layout);
}

void FreeRdpProcess::updateFrameFromBackend(const QImage &frame, const QSize &desktopSize)
{
    bool firstFrameArrived = false;
    {
        QMutexLocker locker(&m_d->mutex);
        m_d->frame = frame;
        m_d->desktopSize = desktopSize;
        firstFrameArrived = !m_d->hasFirstFrame && !frame.isNull();
        m_d->hasFirstFrame = m_d->hasFirstFrame || !frame.isNull();
    }

    emit desktopResized(desktopSize);
    emit frameUpdated();
    if (firstFrameArrived)
        emit cursorUpdated();
}

void FreeRdpProcess::updateStateFromBackend(State state)
{
    setState(state);
}

void FreeRdpProcess::updateCursorFromBackend(const QCursor &cursor, bool hidden)
{
    bool emitNow = false;
    {
        QMutexLocker locker(&m_d->mutex);
        m_d->cursor = cursor;
        m_d->cursorHidden = hidden;
        emitNow = m_d->hasFirstFrame;
    }

    if (emitNow)
        emit cursorUpdated();
}

void FreeRdpProcess::resetCursorFromBackend()
{
    bool emitNow = false;
    {
        QMutexLocker locker(&m_d->mutex);
        m_d->cursor = QCursor(Qt::ArrowCursor);
        m_d->cursorHidden = false;
        emitNow = m_d->hasFirstFrame;
    }

    if (emitNow)
        emit cursorUpdated();
}

void FreeRdpProcess::attachClipboardChannel(void *channelContext)
{
    auto *cliprdr = reinterpret_cast<CliprdrClientContext*>(channelContext);
    if (!cliprdr)
        return;

    if (!m_d->clipboard)
        m_d->clipboard = std::make_unique<RdpClipboardBridge>();

    m_d->clipboard->attach(cliprdr);
}

void FreeRdpProcess::detachClipboardChannel()
{
    if (m_d->clipboard)
        m_d->clipboard->detach();
}

void FreeRdpProcess::setState(State newState)
{
    if (m_d->state == newState)
        return;

    m_d->state = newState;
    emit stateChanged(newState);
}
