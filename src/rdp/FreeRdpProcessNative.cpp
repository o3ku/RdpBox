#include "FreeRdpProcessNative.h"

#include "common/Win32String.h"
#include "RdpCursorClassifier.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <freerdp3/freerdp/autodetect.h>
#include <freerdp3/freerdp/codec/color.h>
#include <freerdp3/freerdp/constants.h>
#include <freerdp3/freerdp/gdi/gdi.h>
#include <freerdp3/freerdp/input.h>
#include <freerdp3/freerdp/locale/keyboard.h>
#include <freerdp3/freerdp/locale/locale.h>

namespace
{
void notifyStateUpdate(FreeRdpProcess *owner, FreeRdpProcess::State state)
{
    if (!owner)
        return;

    owner->updateStateFromBackend(state);
}

void notifyCursorUpdate(FreeRdpProcess *owner, const CursorInfo &cursor)
{
    if (!owner)
        return;

    owner->updateCursorFromBackend(cursor);
}

void notifyCursorReset(FreeRdpProcess *owner)
{
    if (!owner)
        return;

    owner->resetCursorFromBackend();
}

BOOL nativefreerdp_begin_paint(rdpContext *context)
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

void nativefreerdp_copy_frame(rdpContext *context)
{
    if (!context)
        return;

    NativeRdpContext *nativeContext = toNativeContext(context);
    if (!nativeContext || !nativeContext->owner)
        return;

    nativeContext->owner->writeFrameFromContext(context);
}

BOOL nativefreerdp_end_paint(rdpContext *context)
{
    if (!context || !context->gdi || !context->gdi->primary || !context->gdi->primary->hdc)
        return FALSE;

    HGDI_DC hdc = context->gdi->primary->hdc;
    if (!hdc || !hdc->hwnd || !hdc->hwnd->invalid)
        return FALSE;

    if (hdc->hwnd->invalid->null)
        return TRUE;

    nativefreerdp_copy_frame(context);
    return TRUE;
}

BOOL nativefreerdp_desktop_resize(rdpContext *context)
{
    if (!context || !context->gdi || !context->settings)
        return FALSE;

    if (!gdi_resize(context->gdi,
                    freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                    freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight))) {
        return FALSE;
    }

    nativefreerdp_copy_frame(context);
    return TRUE;
}

BOOL nativefreerdp_pointer_new(rdpContext *context, rdpPointer *pointer)
{
    if (!context || !pointer || !context->gdi)
        return FALSE;

    NativePointer *nativePointer = toNativePointer(pointer);
    const int width = static_cast<int>(pointer->width);
    const int height = static_cast<int>(pointer->height);
    if (width <= 0 || height <= 0)
        return FALSE;

    FrameBuffer cursorFrame;
    cursorFrame.width = width;
    cursorFrame.height = height;
    cursorFrame.stride = width * 4;
    cursorFrame.pixels.resize(static_cast<std::size_t>(cursorFrame.stride) * static_cast<std::size_t>(height));

    if (!freerdp_image_copy_from_pointer_data(
            cursorFrame.pixels.data(), PIXEL_FORMAT_BGRA32, static_cast<UINT32>(cursorFrame.stride), 0, 0,
            pointer->width, pointer->height, pointer->xorMaskData, pointer->lengthXorMask,
            pointer->andMaskData, pointer->lengthAndMask, pointer->xorBpp, &context->gdi->palette)) {
        cursorFrame = {};
        return FALSE;
    }

    destroyCursorInfo(nativePointer->cursor);
    nativePointer->cursor = RdpCursorClassifier::createCursor(
        cursorFrame,
        PointI{static_cast<int>(pointer->xPos), static_cast<int>(pointer->yPos)});
    return TRUE;
}

void nativefreerdp_pointer_free(rdpContext *context, rdpPointer *pointer)
{
    static_cast<void>(context);

    if (!pointer)
        return;

    destroyCursorInfo(toNativePointer(pointer)->cursor);
}

BOOL nativefreerdp_pointer_set(rdpContext *context, rdpPointer *pointer)
{
    if (!context || !pointer)
        return FALSE;

    NativeRdpContext *nativeContext = toNativeContext(context);
    if (!nativeContext || !nativeContext->owner)
        return FALSE;

    notifyCursorUpdate(nativeContext->owner, toNativePointer(pointer)->cursor);
    return TRUE;
}

BOOL nativefreerdp_pointer_set_null(rdpContext *context)
{
    NativeRdpContext *nativeContext = toNativeContext(context);
    if (!nativeContext || !nativeContext->owner)
        return FALSE;

    notifyCursorUpdate(nativeContext->owner, CursorInfo{CursorKind::Hidden, nullptr, false});
    return TRUE;
}

BOOL nativefreerdp_pointer_set_default(rdpContext *context)
{
    NativeRdpContext *nativeContext = toNativeContext(context);
    if (!nativeContext || !nativeContext->owner)
        return FALSE;

    notifyCursorReset(nativeContext->owner);
    return TRUE;
}

BOOL nativefreerdp_pointer_set_position(rdpContext *context, UINT32 x, UINT32 y)
{
    static_cast<void>(context);
    static_cast<void>(x);
    static_cast<void>(y);
    return TRUE;
}

BOOL nativefreerdp_register_pointer(rdpGraphics *graphics)
{
    if (!graphics)
        return FALSE;

    rdpPointer pointer = {};
    pointer.size = sizeof(NativePointer);
    pointer.New = nativefreerdp_pointer_new;
    pointer.Free = nativefreerdp_pointer_free;
    pointer.Set = nativefreerdp_pointer_set;
    pointer.SetNull = nativefreerdp_pointer_set_null;
    pointer.SetDefault = nativefreerdp_pointer_set_default;
    pointer.SetPosition = nativefreerdp_pointer_set_position;
    graphics_register_pointer(graphics, &pointer);
    return TRUE;
}

void nativefreerdp_on_channel_connected(void *context, const ChannelConnectedEventArgs *eventArgs)
{
    NativeRdpContext *nativeContext = reinterpret_cast<NativeRdpContext *>(context);
    if (!nativeContext || !eventArgs)
        return;

    if (std::strcmp(eventArgs->name, DISP_DVC_CHANNEL_NAME) == 0) {
        nativeContext->disp = reinterpret_cast<DispClientContext *>(eventArgs->pInterface);
        return;
    }

    if (std::strcmp(eventArgs->name, CLIPRDR_CHANNEL_NAME) == 0) {
        if (nativeContext->owner)
            nativeContext->owner->attachClipboardChannel(eventArgs->pInterface);
        return;
    }

    freerdp_client_OnChannelConnectedEventHandler(context, eventArgs);
}

void nativefreerdp_on_channel_disconnected(void *context, const ChannelDisconnectedEventArgs *eventArgs)
{
    NativeRdpContext *nativeContext = reinterpret_cast<NativeRdpContext *>(context);
    if (!nativeContext || !eventArgs)
        return;

    if (std::strcmp(eventArgs->name, DISP_DVC_CHANNEL_NAME) == 0) {
        nativeContext->disp = nullptr;
        return;
    }

    if (std::strcmp(eventArgs->name, CLIPRDR_CHANNEL_NAME) == 0) {
        if (nativeContext->owner)
            nativeContext->owner->detachClipboardChannel();
        return;
    }

    freerdp_client_OnChannelDisconnectedEventHandler(context, eventArgs);
}
}

NativeRdpContext *toNativeContext(rdpContext *context)
{
    return reinterpret_cast<NativeRdpContext *>(context);
}

const NativeRdpContext *toNativeContext(const rdpContext *context)
{
    return reinterpret_cast<const NativeRdpContext *>(context);
}

NativePointer *toNativePointer(rdpPointer *pointer)
{
    return reinterpret_cast<NativePointer *>(pointer);
}

CursorInfo defaultCursorInfo()
{
    return CursorInfo{CursorKind::Arrow, nullptr, false};
}

void destroyCursorInfo(CursorInfo &cursor)
{
    if (cursor.ownsHandle && cursor.handle)
        DestroyCursor(cursor.handle);

    cursor.handle = nullptr;
    cursor.ownsHandle = false;
    if (cursor.kind == CursorKind::Custom)
        cursor.kind = CursorKind::Arrow;
}

CursorInfo duplicateCursorInfo(const CursorInfo &cursor)
{
    if (!cursor.handle)
        return CursorInfo{cursor.kind, nullptr, false};

    HCURSOR copyHandle = CopyCursor(cursor.handle);
    if (!copyHandle)
        return CursorInfo{cursor.kind, nullptr, false};

    return CursorInfo{cursor.kind, copyHandle, true};
}

PointI clampToDesktop(PointI point, SizeI desktop)
{
    if (desktop.width <= 0 || desktop.height <= 0)
        return {};

    return PointI{
        std::clamp(point.x, 0, desktop.width - 1),
        std::clamp(point.y, 0, desktop.height - 1)
    };
}

UINT16 currentToggleState()
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

BOOL nativefreerdp_pre_connect(freerdp *instance)
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
        const unsigned long layout = std::strtoul(name, nullptr, 16);
        if (errno == 0)
            keyboardLayoutId = static_cast<DWORD>(layout);
    }

    if (keyboardLayoutId == 0) {
        const HKL layout = GetKeyboardLayout(0);
        keyboardLayoutId = static_cast<DWORD>((reinterpret_cast<std::uintptr_t>(layout) >> 16) & 0xFFFF);
    }

    if (keyboardLayoutId == 0)
        freerdp_detect_keyboard_layout_from_system_locale(&keyboardLayoutId);
    if (keyboardLayoutId == 0)
        keyboardLayoutId = ENGLISH_UNITED_STATES;
    if (!freerdp_settings_set_uint32(settings, FreeRDP_KeyboardLayout, keyboardLayoutId))
        return FALSE;

    PubSub_SubscribeChannelConnected(instance->context->pubSub, nativefreerdp_on_channel_connected);
    PubSub_SubscribeChannelDisconnected(instance->context->pubSub, nativefreerdp_on_channel_disconnected);

    rdpAutoDetect *ad = ::autodetect_get(instance->context);
    if (ad) {
        ad->NetworkCharacteristicsResult = [](
            rdpAutoDetect *autodetect, RDP_TRANSPORT_TYPE,
            UINT16, const rdpNetworkCharacteristicsResult *result) -> BOOL {
            if (result && result->averageRTT > 0) {
                auto *ctx = toNativeContext(autodetect->context);
                if (ctx)
                    ctx->rtt.store(result->averageRTT, std::memory_order_relaxed);
            }
            return TRUE;
        };
        ad->BandwidthMeasureResults = [](
            rdpAutoDetect *autodetect, RDP_TRANSPORT_TYPE,
            UINT16, UINT16, UINT32 timeDelta, UINT32) -> BOOL {
            if (timeDelta > 0) {
                auto *ctx = toNativeContext(autodetect->context);
                if (ctx)
                    ctx->rtt.store(timeDelta, std::memory_order_relaxed);
            }
            return TRUE;
        };
    }

    return TRUE;
}

BOOL nativefreerdp_post_connect(freerdp *instance)
{
    if (!instance || !instance->context || !instance->context->update)
        return FALSE;

    if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
        return FALSE;

    instance->context->update->BeginPaint = nativefreerdp_begin_paint;
    instance->context->update->EndPaint = nativefreerdp_end_paint;
    instance->context->update->DesktopResize = nativefreerdp_desktop_resize;
    nativefreerdp_register_pointer(instance->context->graphics);

    NativeRdpContext *nativeContext = toNativeContext(instance->context);
    if (nativeContext && nativeContext->owner)
        notifyStateUpdate(nativeContext->owner, FreeRdpProcess::State::Running);

    nativefreerdp_copy_frame(instance->context);
    return TRUE;
}

void nativefreerdp_post_disconnect(freerdp *instance)
{
    if (!instance || !instance->context)
        return;

    PubSub_UnsubscribeChannelConnected(instance->context->pubSub, nativefreerdp_on_channel_connected);
    PubSub_UnsubscribeChannelDisconnected(instance->context->pubSub, nativefreerdp_on_channel_disconnected);
    gdi_free(instance);
}

BOOL nativefreerdp_authenticate_ex(freerdp *instance, char **username, char **password,
                                   char **domain, rdp_auth_reason reason)
{
    static_cast<void>(reason);

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

DWORD nativefreerdp_verify_certificate_ex(freerdp *instance, const char *host, UINT16 port,
                                          const char *commonName, const char *subject,
                                          const char *issuer, const char *fingerprint, DWORD flags)
{
    static_cast<void>(flags);

    const NativeRdpContext *nativeContext = instance ? toNativeContext(instance->context) : nullptr;
    if (!nativeContext || !nativeContext->owner)
        return 0;

    if (nativeContext->ignoreCertificate)
        return 2;

    FreeRdpProcess::CertificateChallenge challenge;
    challenge.host = wideFromUtf8(host ? host : "");
    challenge.port = port;
    challenge.commonName = wideFromUtf8(commonName ? commonName : "");
    challenge.subject = wideFromUtf8(subject ? subject : "");
    challenge.issuer = wideFromUtf8(issuer ? issuer : "");
    challenge.fingerprint = wideFromUtf8(fingerprint ? fingerprint : "");
    challenge.changed = false;

    return nativeContext->owner->challengeCertificate(challenge) ? 2 : 0;
}

DWORD nativefreerdp_verify_changed_certificate_ex(freerdp *instance, const char *host, UINT16 port,
                                                  const char *commonName, const char *subject,
                                                  const char *issuer, const char *newFingerprint,
                                                  const char *oldSubject, const char *oldIssuer,
                                                  const char *oldFingerprint, DWORD flags)
{
    static_cast<void>(oldSubject);
    static_cast<void>(oldIssuer);
    static_cast<void>(oldFingerprint);
    static_cast<void>(flags);

    const NativeRdpContext *nativeContext = instance ? toNativeContext(instance->context) : nullptr;
    if (!nativeContext || !nativeContext->owner)
        return 0;

    if (nativeContext->ignoreCertificate)
        return 2;

    FreeRdpProcess::CertificateChallenge challenge;
    challenge.host = wideFromUtf8(host ? host : "");
    challenge.port = port;
    challenge.commonName = wideFromUtf8(commonName ? commonName : "");
    challenge.subject = wideFromUtf8(subject ? subject : "");
    challenge.issuer = wideFromUtf8(issuer ? issuer : "");
    challenge.fingerprint = wideFromUtf8(newFingerprint ? newFingerprint : "");
    challenge.changed = true;

    return nativeContext->owner->challengeCertificate(challenge) ? 2 : 0;
}

BOOL nativefreerdp_global_init()
{
    return TRUE;
}

void nativefreerdp_global_uninit()
{
}

BOOL nativefreerdp_client_new(freerdp *instance, rdpContext *context)
{
    if (!instance || !context)
        return FALSE;

    instance->PreConnect = nativefreerdp_pre_connect;
    instance->PostConnect = nativefreerdp_post_connect;
    instance->PostDisconnect = nativefreerdp_post_disconnect;
    instance->AuthenticateEx = nativefreerdp_authenticate_ex;
    instance->VerifyCertificateEx = nativefreerdp_verify_certificate_ex;
    instance->VerifyChangedCertificateEx = nativefreerdp_verify_changed_certificate_ex;
    return TRUE;
}

void nativefreerdp_client_free(freerdp *instance, rdpContext *context)
{
    static_cast<void>(instance);
    static_cast<void>(context);
}

int nativefreerdp_client_start(rdpContext *context)
{
    if (!context || !context->instance)
        return -1;

    return freerdp_client_load_channels(context->instance) ? 0 : -1;
}

int nativefreerdp_client_stop(rdpContext *context)
{
    static_cast<void>(context);
    return 0;
}
