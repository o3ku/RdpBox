#pragma once

#include "FreeRdpProcess.h"

#include <atomic>
#include <memory>

#include <freerdp/channels/channels.h>
#include <freerdp/client.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/freerdp.h>
#include <freerdp/graphics.h>

#ifndef _WIN32
#include <winpr/winpr.h>
#endif

struct NativeRdpContext
{
    rdpClientContext common;
    // Every freerdp callback reaches back to the owning process through
    // `owner`, and an abandoned worker outlives it. `locked` is the contract:
    // callbacks hold it across the call, whoever clears it has released the
    // context, so no new call can start while the owner is being taken apart.
    struct InvokeLock
    {
        std::atomic_bool locked{false};
    };

    FreeRdpProcess *owner = nullptr;
    std::shared_ptr<InvokeLock> locked;
    DispClientContext *disp = nullptr;
    bool ignoreCertificate = true;
    std::atomic<UINT32> rtt{0};
};

struct NativePointer : rdpPointer
{
    CursorInfo cursor;
    // Non-zero once nativefreerdp_pointer_new finished; the Free callback only
    // touches `cursor` when it was really set.
    unsigned long long cursorSerial = 0;
};

NativeRdpContext *toNativeContext(rdpContext *context);
const NativeRdpContext *toNativeContext(const rdpContext *context);
NativePointer *toNativePointer(rdpPointer *pointer);

CursorInfo defaultCursorInfo();
void destroyCursorInfo(CursorInfo &cursor);
CursorInfo duplicateCursorInfo(const CursorInfo &cursor);
PointI clampToDesktop(PointI point, SizeI desktop);
UINT16 currentToggleState();

BOOL nativefreerdp_global_init();
void nativefreerdp_global_uninit();
BOOL nativefreerdp_client_new(freerdp *instance, rdpContext *context);
void nativefreerdp_client_free(freerdp *instance, rdpContext *context);
int nativefreerdp_client_start(rdpContext *context);
int nativefreerdp_client_stop(rdpContext *context);
