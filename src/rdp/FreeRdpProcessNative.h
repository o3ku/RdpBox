#pragma once

#include "FreeRdpProcess.h"

#include <atomic>

#include <freerdp3/freerdp/channels/channels.h>
#include <freerdp3/freerdp/client.h>
#include <freerdp3/freerdp/client/channels.h>
#include <freerdp3/freerdp/client/cliprdr.h>
#include <freerdp3/freerdp/client/disp.h>
#include <freerdp3/freerdp/freerdp.h>
#include <freerdp3/freerdp/graphics.h>

#include <windows.h>

struct NativeRdpContext
{
    rdpClientContext common;
    FreeRdpProcess *owner = nullptr;
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
