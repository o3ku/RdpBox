#include "FreeRdpProcess.h"

#include "common/Win32String.h"
#include "rdp/FrameBufferMemory.h"
#include "rdp/FreeRdpProcessNative.h"
#include "RdpClipboardBridge.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include <freerdp3/freerdp/autodetect.h>
#include <freerdp3/freerdp/client/cliprdr.h>
#include <freerdp3/freerdp/codecs.h>
#include <freerdp3/freerdp/constants.h>
#include <freerdp3/freerdp/input.h>

#include <windows.h>

namespace
{
constexpr DWORD kEventWaitTimeoutMs = 10;
}

struct FreeRdpProcess::Private
{
    rdpContext *context = nullptr;
    std::thread worker;
    std::atomic_bool stopRequested = false;
    mutable std::mutex mutex;
    FrameBuffer frame;
    uint64_t frameGeneration = 0;
    SizeI desktopSize;
    CursorInfo cursor;
    bool hasFirstFrame = false;
    std::unique_ptr<RdpClipboardBridge> clipboard;
    State state = State::Idle;
    std::string lastDisconnectError;

    std::function<void(State)> stateChanged;
    std::function<void()> frameUpdated;
    std::function<void(const SizeI &)> desktopResized;
    std::function<void()> cursorUpdated;
    std::function<void(const CertificateChallenge &)> certificateChallenge;

    HANDLE certDecidedEvent = nullptr;
    HANDLE certAbortEvent = nullptr;
    std::atomic_bool certDecisionAccepted = false;
};

FreeRdpProcess::FreeRdpProcess()
    : m_d(std::make_unique<Private>())
{
    m_d->cursor = defaultCursorInfo();
    m_d->certDecidedEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_d->certAbortEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

FreeRdpProcess::~FreeRdpProcess()
{
    stop();

    std::scoped_lock lock(m_d->mutex);
    destroyCursorInfo(m_d->cursor);
    if (m_d->certDecidedEvent) {
        ::CloseHandle(m_d->certDecidedEvent);
        m_d->certDecidedEvent = nullptr;
    }
    if (m_d->certAbortEvent) {
        ::CloseHandle(m_d->certAbortEvent);
        m_d->certAbortEvent = nullptr;
    }
}

void FreeRdpProcess::start(const std::wstring &host,
                           int port,
                           const std::wstring &username,
                           const std::wstring &password,
                           const std::wstring &domain,
                           int width,
                           int height,
                           bool clipboardEnabled,
                           bool ignoreCertificate)
{
    stop();

    RDP_CLIENT_ENTRY_POINTS entryPoints = {};
    entryPoints.Size = sizeof(entryPoints);
    entryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
    entryPoints.GlobalInit = nativefreerdp_global_init;
    entryPoints.GlobalUninit = nativefreerdp_global_uninit;
    entryPoints.ContextSize = sizeof(NativeRdpContext);
    entryPoints.ClientNew = nativefreerdp_client_new;
    entryPoints.ClientFree = nativefreerdp_client_free;
    entryPoints.ClientStart = nativefreerdp_client_start;
    entryPoints.ClientStop = nativefreerdp_client_stop;

    m_d->context = freerdp_client_context_new(&entryPoints);
    if (!m_d->context) {
        updateStateFromBackend(State::Finished);
        return;
    }

    NativeRdpContext *context = toNativeContext(m_d->context);
    context->owner = this;
    context->ignoreCertificate = ignoreCertificate;

    rdpSettings *settings = m_d->context->settings;
    const UINT32 desktopWidth = static_cast<UINT32>((width > 640) ? width : 640);
    const UINT32 desktopHeight = static_cast<UINT32>((height > 480) ? height : 480);
    const std::string hostUtf8 = utf8FromWide(host);
    const std::string usernameUtf8 = utf8FromWide(username);
    const std::string passwordUtf8 = utf8FromWide(password);
    const std::string domainUtf8 = utf8FromWide(domain);
    const UINT32 serverPort = static_cast<UINT32>(std::clamp(port, 1, 65535));

    const bool configured =
        freerdp_settings_set_string(settings, FreeRDP_ServerHostname, hostUtf8.c_str()) &&
        freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, serverPort) &&
        freerdp_settings_set_string(settings, FreeRDP_Username, usernameUtf8.c_str()) &&
        freerdp_settings_set_string(settings, FreeRDP_Password, passwordUtf8.c_str()) &&
        freerdp_settings_set_string(settings, FreeRDP_Domain, domainUtf8.c_str()) &&
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, desktopWidth) &&
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, desktopHeight) &&
        freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32) &&
        freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, ignoreCertificate) &&
        freerdp_settings_set_bool(settings, FreeRDP_SoftwareGdi, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE) &&
        freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, clipboardEnabled);

    if (configured) {
        freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_JpegCodec, TRUE);
        freerdp_settings_set_uint32(settings, FreeRDP_JpegQuality, 75);
        freerdp_settings_set_bool(settings, FreeRDP_GfxH264, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE);
        freerdp_settings_set_bool(settings, FreeRDP_NetworkAutoDetect, TRUE);
    }

    if (!configured || freerdp_client_start(m_d->context) != 0) {
        freerdp_client_context_free(m_d->context);
        m_d->context = nullptr;
        updateStateFromBackend(State::Finished);
        return;
    }

    CursorInfo oldCursor;
    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, defaultCursorInfo());
        FrameBufferMemory::release(m_d->frame);
        m_d->desktopSize = SizeI{static_cast<int>(desktopWidth), static_cast<int>(desktopHeight)};
        m_d->hasFirstFrame = false;
    }
    destroyCursorInfo(oldCursor);

    m_d->stopRequested = false;
    updateStateFromBackend(State::Starting);

    m_d->worker = std::thread([this]() {
        rdpContext *context = m_d->context;
        if (!context || !context->instance) {
            updateStateFromBackend(State::Finished);
            return;
        }

        freerdp *instance = context->instance;
        const BOOL connected = freerdp_connect(instance);

        if (connected) {
            while (!m_d->stopRequested && !freerdp_shall_disconnect_context(context)) {
                HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
                const DWORD handleCount = freerdp_get_event_handles(context, handles, MAXIMUM_WAIT_OBJECTS);
                if (handleCount == 0)
                    break;

                const DWORD waitResult = WaitForMultipleObjects(handleCount, handles, FALSE, kEventWaitTimeoutMs);
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

        m_d->lastDisconnectError.clear();
        const UINT32 lastError = freerdp_get_last_error(context);
        if (lastError != FREERDP_ERROR_SUCCESS) {
            const char *errorStr = freerdp_get_last_error_string(lastError);
            if (errorStr && *errorStr)
                m_d->lastDisconnectError = errorStr;
        }

        updateStateFromBackend(State::Finished);
    });
}

void FreeRdpProcess::stop()
{
    m_d->stopRequested = true;

    if (m_d->certAbortEvent)
        ::SetEvent(m_d->certAbortEvent);

    if (m_d->context)
        freerdp_abort_connect_context(m_d->context);

    if (m_d->worker.joinable())
        m_d->worker.join();

    if (m_d->context) {
        freerdp_client_stop(m_d->context);
        freerdp_client_context_free(m_d->context);
        m_d->context = nullptr;
    }

    if (m_d->certAbortEvent)
        ::ResetEvent(m_d->certAbortEvent);

    CursorInfo oldCursor;
    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, defaultCursorInfo());
        FrameBufferMemory::release(m_d->frame);
        m_d->desktopSize = {};
        m_d->hasFirstFrame = false;
        m_d->frameGeneration = 0;
    }
    destroyCursorInfo(oldCursor);

    if (m_d->clipboard)
        m_d->clipboard->detach();
}

FreeRdpProcess::State FreeRdpProcess::state() const
{
    std::scoped_lock lock(m_d->mutex);
    return m_d->state;
}

std::string FreeRdpProcess::lastDisconnectError() const
{
    return m_d->lastDisconnectError;
}

bool FreeRdpProcess::visitFrameIfNewer(uint64_t &lastSeenGeneration,
                                       const std::function<void(const FrameBuffer &, uint64_t)> &visitor)
{
    std::scoped_lock lock(m_d->mutex);
    if (m_d->frameGeneration == lastSeenGeneration || m_d->frame.empty())
        return false;

    lastSeenGeneration = m_d->frameGeneration;
    if (visitor)
        visitor(m_d->frame, m_d->frameGeneration);
    return true;
}

bool FreeRdpProcess::consumeFrameIfNewer(uint64_t &lastSeenGeneration, FrameBuffer &target)
{
    std::scoped_lock lock(m_d->mutex);
    if (m_d->frameGeneration == lastSeenGeneration || m_d->frame.empty())
        return false;

    lastSeenGeneration = m_d->frameGeneration;
    target = std::move(m_d->frame);
    m_d->frame = {};
    return true;
}

SizeI FreeRdpProcess::desktopSize() const
{
    std::scoped_lock lock(m_d->mutex);
    return m_d->desktopSize;
}

CursorInfo FreeRdpProcess::cursor() const
{
    std::scoped_lock lock(m_d->mutex);
    return duplicateCursorInfo(m_d->cursor);
}

void FreeRdpProcess::setStateChangedCallback(std::function<void(State)> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->stateChanged = std::move(callback);
}

void FreeRdpProcess::setFrameUpdatedCallback(std::function<void()> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->frameUpdated = std::move(callback);
}

void FreeRdpProcess::setDesktopResizedCallback(std::function<void(const SizeI &)> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->desktopResized = std::move(callback);
}

void FreeRdpProcess::setCursorUpdatedCallback(std::function<void()> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->cursorUpdated = std::move(callback);
}

void FreeRdpProcess::setCertificateChallengeCallback(std::function<void(const CertificateChallenge &)> callback)
{
    std::scoped_lock lock(m_d->mutex);
    m_d->certificateChallenge = std::move(callback);
}

void FreeRdpProcess::resolveCertificateChallenge(bool accept)
{
    m_d->certDecisionAccepted = accept;
    if (m_d->certDecidedEvent)
        ::SetEvent(m_d->certDecidedEvent);
}

bool FreeRdpProcess::challengeCertificate(const CertificateChallenge &challenge)
{
    std::function<void(const CertificateChallenge &)> callback;
    {
        std::scoped_lock lock(m_d->mutex);
        callback = m_d->certificateChallenge;
    }

    if (!callback || !m_d->certDecidedEvent || !m_d->certAbortEvent)
        return false;

    m_d->certDecisionAccepted = false;
    ::ResetEvent(m_d->certDecidedEvent);

    callback(challenge);

    HANDLE handles[2] = { m_d->certDecidedEvent, m_d->certAbortEvent };
    const DWORD waitResult = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (waitResult != WAIT_OBJECT_0)
        return false;

    return m_d->certDecisionAccepted;
}

void FreeRdpProcess::sendFocusIn()
{
    if (!m_d->context || !m_d->context->input)
        return;

    freerdp_input_send_focus_in_event(m_d->context->input, currentToggleState());
}

void FreeRdpProcess::sendKeyMessage(std::uint32_t message, std::uintptr_t wParam, std::intptr_t lParam)
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

void FreeRdpProcess::sendMouseMove(PointI pos, SizeI viewSize)
{
    if (!m_d->context || viewSize.width <= 0 || viewSize.height <= 0)
        return;

    const SizeI desktop = desktopSize();
    if (desktop.width <= 0 || desktop.height <= 0)
        return;

    const PointI mapped = clampToDesktop(PointI{
        pos.x * desktop.width / viewSize.width,
        pos.y * desktop.height / viewSize.height
    }, desktop);

    freerdp_client_send_button_event(&toNativeContext(m_d->context)->common,
                                     FALSE, PTR_FLAGS_MOVE, mapped.x, mapped.y);
}

void FreeRdpProcess::sendMouseButton(MouseButton button, bool down, PointI pos, SizeI viewSize)
{
    if (!m_d->context)
        return;

    const SizeI desktop = desktopSize();
    if (desktop.width <= 0 || desktop.height <= 0 || viewSize.width <= 0 || viewSize.height <= 0)
        return;

    const PointI mapped = clampToDesktop(PointI{
        pos.x * desktop.width / viewSize.width,
        pos.y * desktop.height / viewSize.height
    }, desktop);

    if (button == MouseButton::Back || button == MouseButton::Forward) {
        UINT16 flags = (button == MouseButton::Back) ? PTR_XFLAGS_BUTTON1 : PTR_XFLAGS_BUTTON2;
        if (down)
            flags |= PTR_XFLAGS_DOWN;

        freerdp_client_send_extended_button_event(&toNativeContext(m_d->context)->common,
                                                  FALSE, flags, mapped.x, mapped.y);
        return;
    }

    UINT16 flags = 0;
    switch (button) {
    case MouseButton::Left:
        flags |= PTR_FLAGS_BUTTON1;
        break;
    case MouseButton::Right:
        flags |= PTR_FLAGS_BUTTON2;
        break;
    case MouseButton::Middle:
        flags |= PTR_FLAGS_BUTTON3;
        break;
    default:
        return;
    }

    if (down)
        flags |= PTR_FLAGS_DOWN;

    freerdp_client_send_button_event(&toNativeContext(m_d->context)->common,
                                     FALSE, flags, mapped.x, mapped.y);
}

void FreeRdpProcess::sendWheel(PointI angleDelta, PointI pos, SizeI viewSize)
{
    if (!m_d->context)
        return;

    sendMouseMove(pos, viewSize);

    const auto sendOne = [this](int delta, bool horizontal) {
        if (delta == 0)
            return;

        delta = std::clamp(delta, -255, 255);

        UINT16 flags = horizontal ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL;
        if (delta < 0) {
            flags |= PTR_FLAGS_WHEEL_NEGATIVE;
            delta = 0x100 + delta;
        }

        flags |= static_cast<UINT16>(delta);
        freerdp_client_send_wheel_event(&toNativeContext(m_d->context)->common, flags);
    };

    if (angleDelta.y != 0)
        sendOne(angleDelta.y, false);
    else if (angleDelta.x != 0)
        sendOne(angleDelta.x, true);
}

void FreeRdpProcess::requestResize(SizeI size)
{
    if (!m_d->context || size.width <= 0 || size.height <= 0)
        return;

    NativeRdpContext *context = toNativeContext(m_d->context);
    if (!context || !context->disp || !context->disp->SendMonitorLayout)
        return;

    DISPLAY_CONTROL_MONITOR_LAYOUT layout = {};
    layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
    layout.Left = 0;
    layout.Top = 0;
    layout.Width = static_cast<UINT32>((size.width + 3) & ~3);
    layout.Height = static_cast<UINT32>(size.height);
    layout.Orientation = ORIENTATION_LANDSCAPE;
    layout.DesktopScaleFactor = 100;
    layout.DeviceScaleFactor = 100;
    layout.PhysicalWidth = layout.Width;
    layout.PhysicalHeight = layout.Height;

    context->disp->SendMonitorLayout(context->disp, 1, &layout);
}

void FreeRdpProcess::writeFrameFromContext(void *rawContext)
{
    const rdpContext *context = static_cast<rdpContext *>(rawContext);
    if (!context || !context->gdi || !context->gdi->primary_buffer)
        return;

    const int width = context->gdi->width;
    const int height = context->gdi->height;
    const int stride = static_cast<int>(context->gdi->stride);
    if (width <= 0 || height <= 0 || stride <= 0)
        return;

    const std::size_t requiredBytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);

    std::function<void(const SizeI &)> desktopCallback;
    std::function<void()> frameCallback;
    std::function<void()> cursorCallback;
    bool firstFrameArrived = false;
    SizeI desktopSize{width, height};

    {
        std::scoped_lock lock(m_d->mutex);

        if (m_d->frame.pixels.size() != requiredBytes
            || m_d->frame.width != width
            || m_d->frame.height != height
            || m_d->frame.stride != stride) {
            FrameBufferMemory::resize(m_d->frame, width, height, stride, requiredBytes);
        }

        std::memcpy(m_d->frame.pixels.data(), context->gdi->primary_buffer, requiredBytes);
        m_d->desktopSize = desktopSize;
        ++m_d->frameGeneration;
        firstFrameArrived = !m_d->hasFirstFrame;
        m_d->hasFirstFrame = true;
        desktopCallback = m_d->desktopResized;
        frameCallback = m_d->frameUpdated;
        cursorCallback = m_d->cursorUpdated;
    }

    if (desktopCallback)
        desktopCallback(desktopSize);
    if (frameCallback)
        frameCallback();
    if (firstFrameArrived && cursorCallback)
        cursorCallback();
}

void FreeRdpProcess::updateStateFromBackend(State state)
{
    std::function<void(State)> callback;

    {
        std::scoped_lock lock(m_d->mutex);
        if (m_d->state == state)
            return;

        m_d->state = state;
        callback = m_d->stateChanged;
    }

    if (callback)
        callback(state);
}

void FreeRdpProcess::updateCursorFromBackend(const CursorInfo &cursor)
{
    CursorInfo oldCursor;
    std::function<void()> callback;
    bool emitNow = false;

    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, duplicateCursorInfo(cursor));
        emitNow = m_d->hasFirstFrame;
        callback = m_d->cursorUpdated;
    }

    destroyCursorInfo(oldCursor);
    if (emitNow && callback)
        callback();
}

void FreeRdpProcess::resetCursorFromBackend()
{
    CursorInfo oldCursor;
    std::function<void()> callback;
    bool emitNow = false;

    {
        std::scoped_lock lock(m_d->mutex);
        oldCursor = std::exchange(m_d->cursor, defaultCursorInfo());
        emitNow = m_d->hasFirstFrame;
        callback = m_d->cursorUpdated;
    }

    destroyCursorInfo(oldCursor);
    if (emitNow && callback)
        callback();
}

void FreeRdpProcess::attachClipboardChannel(void *channelContext)
{
    CliprdrClientContext *cliprdr = reinterpret_cast<CliprdrClientContext *>(channelContext);
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

FreeRdpProcess::ConnectionInfo FreeRdpProcess::connectionInfo() const
{
    ConnectionInfo info;
    std::scoped_lock lock(m_d->mutex);

    if (!m_d->context)
        return info;

    rdpSettings *settings = m_d->context->settings;
    if (settings) {
        if (freerdp_settings_get_bool(settings, FreeRDP_SupportGraphicsPipeline)) {
            if (freerdp_settings_get_bool(settings, FreeRDP_GfxAVC444) ||
                freerdp_settings_get_bool(settings, FreeRDP_GfxAVC444v2))
                info.codecName = "H.264 AVC 444";
            else if (freerdp_settings_get_bool(settings, FreeRDP_GfxH264))
                info.codecName = "H.264 AVC 420";
            else
                info.codecName = "GFX";
        } else {
            const UINT32 codecId = freerdp_settings_get_uint32(
                settings, FreeRDP_BitmapCacheV3CodecId);
            switch (codecId) {
            case 1: info.codecName = "NSCodec"; break;
            case 2: info.codecName = "RemoteFX"; break;
            case 3: info.codecName = "ClearCodec"; break;
            default: info.codecName = "Bitmap"; break;
            }
        }
    }

    NativeRdpContext *nativeCtx = toNativeContext(m_d->context);
    if (nativeCtx) {
        UINT32 rtt = nativeCtx->rtt.load(std::memory_order_relaxed);
        if (rtt > 0) {
            info.rtt = rtt;
            info.rttAvailable = true;
        }
    }

    if (!info.rttAvailable) {
        rdpAutoDetect *ad = ::autodetect_get(m_d->context);
        if (ad) {
            if (ad->netCharAverageRTT > 0) {
                info.rtt = ad->netCharAverageRTT;
                info.rttAvailable = true;
            } else if (ad->netCharBaseRTT > 0) {
                info.rtt = ad->netCharBaseRTT;
                info.rttAvailable = true;
            }
        }
    }

    return info;
}
