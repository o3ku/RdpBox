#pragma once

#define CINTERFACE
#define COBJMACROS

#include "WindowsClipboardBackend.h"

#include <freerdp/client/cliprdr.h>
#include <freerdp/channels/cliprdr.h>

#include <objidl.h>
#include <ole2.h>
#include <windows.h>

#if defined(NONAMELESSUNION)
#define STGMEDIUM_HGLOBAL(value) ((value).u.hGlobal)
#define STGMEDIUM_PSTM(value) ((value).u.pstm)
#else
#define STGMEDIUM_HGLOBAL(value) ((value).hGlobal)
#define STGMEDIUM_PSTM(value) ((value).pstm)
#endif

typedef BOOL(WINAPI* fnAddClipboardFormatListener)(HWND hwnd);
typedef BOOL(WINAPI* fnRemoveClipboardFormatListener)(HWND hwnd);
typedef BOOL(WINAPI* fnGetUpdatedClipboardFormats)(PUINT lpuiFormats, UINT cFormats,
                                                   PUINT pcFormatsOut);

struct FormatMapping
{
    UINT32 remoteFormatId = 0;
    UINT32 localFormatId = 0;
    WCHAR *name = nullptr;
};

struct ClipboardContext
{
    CliprdrClientContext *context = nullptr;
    bool sync = false;
    UINT32 capabilities = 0;

    size_t mapSize = 0;
    size_t mapCapacity = 32;
    FormatMapping *formatMappings = nullptr;

    UINT32 requestedFormatId = 0;

    HWND hwnd = nullptr;
    HANDLE responseData = nullptr;
    HANDLE responseDataEvent = nullptr;
    HANDLE requestFileEvent = nullptr;
    HANDLE abortEvent = nullptr;
    HANDLE thread = nullptr;

    IDataObject *dataObject = nullptr;
    ULONG requestFileSize = 0;
    char *requestFileData = nullptr;

    size_t fileCount = 0;
    size_t fileArraySize = 0;
    WCHAR **fileNames = nullptr;
    FILEDESCRIPTORW **fileDescriptors = nullptr;

    bool legacyApi = false;
    HMODULE user32 = nullptr;
    HWND nextViewer = nullptr;
    fnAddClipboardFormatListener addClipboardFormatListener = nullptr;
    fnRemoveClipboardFormatListener removeClipboardFormatListener = nullptr;
    fnGetUpdatedClipboardFormats getUpdatedClipboardFormats = nullptr;
};

struct CliprdrEnumFORMATETC
{
    IEnumFORMATETC iface;
    LONG refCount = 1;
    LONG index = 0;
    LONG count = 0;
    FORMATETC *formats = nullptr;
};

struct CliprdrStream
{
    IStream iface;
    LONG refCount = 1;
    ULONG listIndex = 0;
    ULARGE_INTEGER size = {};
    ULARGE_INTEGER offset = {};
    FILEDESCRIPTORW descriptor = {};
    ClipboardContext *clipboard = nullptr;
};

struct CliprdrDataObject
{
    IDataObject iface;
    LONG refCount = 1;
    FORMATETC *formats = nullptr;
    STGMEDIUM *mediums = nullptr;
    ULONG formatCount = 0;
    ULONG streamCount = 0;
    IStream **streams = nullptr;
    ClipboardContext *clipboard = nullptr;
};

UINT32 clipboardGetRemoteFormatId(ClipboardContext *clipboard, UINT32 localFormat);
BOOL clipboardTryOpenClipboard(HWND hwnd);
UINT clipboardSendFormatList(ClipboardContext *clipboard);
UINT clipboardSendDataRequest(ClipboardContext *clipboard, UINT32 formatId);
UINT clipboardSendFileContentsRequest(ClipboardContext *clipboard, const void *streamId, ULONG index,
                                      UINT32 flags, UINT64 position, ULONG requestSize);
DWORD WINAPI clipboardThreadProc(LPVOID argument);
BOOL createFileDataObject(ClipboardContext *clipboard, IDataObject **dataObject);
void destroyFileDataObject(IDataObject *instance);
