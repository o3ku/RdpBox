#pragma once

#include "FreeRdpProcess.h"

#include <atomic>
#include <memory>

#include <freerdp3/freerdp/channels/channels.h>
#include <freerdp3/freerdp/client.h>
#include <freerdp3/freerdp/client/channels.h>
#include <freerdp3/freerdp/client/cliprdr.h>
#include <freerdp3/freerdp/client/disp.h>
#include <freerdp3/freerdp/freerdp.h>
#include <freerdp3/freerdp/graphics.h>

#ifndef _WIN32
#include <winpr/winpr.h>
#endif

struct NativeRdpContext
{
    // The worker outlives the process when stop() abandons it, and every
    // freerdp callback reaches back through `owner`. `alive` is the contract:
    // whoever clears it has released the context, so the owner pointer may be
    // read but not followed.
    struct AliveFlag
    {
        std::atomic_bool alive{true};
    };

    rdpClientContext common;
    FreeRdpProcess *owner = nullptr;
    std::shared_ptr<AliveFlag> alive;
    DispClientContext *disp = nullptr;
    bool ignoreCertificate = true;
    std::atomic<UINT32> rtt{0};
};

struct NativePointer : rdpPointer
{
    CursorInfo cursor;
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
