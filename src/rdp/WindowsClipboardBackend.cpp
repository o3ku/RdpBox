#include "WindowsClipboardBackend.h"

#define CINTERFACE
#define COBJMACROS

#include <freerdp/client/cliprdr.h>
#include <freerdp/channels/cliprdr.h>

#include <winpr/assert.h>
#include <winpr/library.h>
#include <winpr/string.h>

#include <objidl.h>
#include <ole2.h>
#include <shlobj.h>
#include <strsafe.h>
#include <windows.h>
#include <winuser.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
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

struct ClipboardContext;

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

constexpr UINT WM_CLIPRDR_MESSAGE = WM_USER + 156;
constexpr WPARAM OLE_SETCLIPBOARD = 1;
constexpr DWORD kClipboardRequestTimeoutMs = 30000;
constexpr size_t kMaxClipboardFileCount = 10000;
constexpr int kMaxClipboardDirectoryDepth = 32;

static BOOL tryOpenClipboard(HWND hwnd)
{
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(hwnd))
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

static UINT32 getRemoteFormatId(ClipboardContext *clipboard, UINT32 localFormat);
static UINT sendDataRequest(ClipboardContext *clipboard, UINT32 formatId);
static UINT sendFileContentsRequest(ClipboardContext *clipboard, const void *streamId, ULONG index,
                                    UINT32 flags, UINT64 position, ULONG requestSize);
static UINT waitForClipboardResponse(ClipboardContext *clipboard, HANDLE responseEvent);
static BOOL createFileDataObject(ClipboardContext *clipboard, IDataObject **dataObject);
static void destroyFileDataObject(IDataObject *instance);
static BOOL clearFormatMap(ClipboardContext *clipboard);
static void clearFileArray(ClipboardContext *clipboard);

static void formatDeepCopy(FORMATETC *destination, const FORMATETC *source)
{
    *destination = *source;
    if (!source->ptd)
        return;

    destination->ptd = static_cast<DVTARGETDEVICE*>(CoTaskMemAlloc(sizeof(DVTARGETDEVICE)));
    if (destination->ptd)
        *destination->ptd = *source->ptd;
}

static void deleteEnumFormatEtc(CliprdrEnumFORMATETC *instance)
{
    if (!instance)
        return;

    free(instance->iface.lpVtbl);
    if (instance->formats) {
        for (LONG i = 0; i < instance->count; ++i) {
            if (instance->formats[i].ptd)
                CoTaskMemFree(instance->formats[i].ptd);
        }
        free(instance->formats);
    }
    free(instance);
}

static HRESULT STDMETHODCALLTYPE enumFormatEtcQueryInterface(IEnumFORMATETC *self, REFIID riid,
                                                             void **object)
{
    if (!object)
        return E_INVALIDARG;

    if (InlineIsEqualGUID(riid, IID_IEnumFORMATETC) || InlineIsEqualGUID(riid, IID_IUnknown)) {
        IEnumFORMATETC_AddRef(self);
        *object = self;
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enumFormatEtcAddRef(IEnumFORMATETC *self)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    return instance ? InterlockedIncrement(&instance->refCount) : 0;
}

static ULONG STDMETHODCALLTYPE enumFormatEtcRelease(IEnumFORMATETC *self)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance)
        return 0;

    const LONG count = InterlockedDecrement(&instance->refCount);
    if (count == 0)
        deleteEnumFormatEtc(instance);
    return (count > 0) ? static_cast<ULONG>(count) : 0;
}

static HRESULT STDMETHODCALLTYPE enumFormatEtcNext(IEnumFORMATETC *self, ULONG count,
                                                   FORMATETC *items, ULONG *fetched)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance || !count || !items)
        return E_INVALIDARG;

    ULONG copied = 0;
    while ((instance->index < instance->count) && (copied < count)) {
        formatDeepCopy(&items[copied++], &instance->formats[instance->index++]);
    }

    if (fetched)
        *fetched = copied;

    return (copied == count) ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE enumFormatEtcSkip(IEnumFORMATETC *self, ULONG count)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance)
        return E_INVALIDARG;
    if (instance->index + static_cast<LONG>(count) > instance->count)
        return E_FAIL;
    instance->index += static_cast<LONG>(count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enumFormatEtcReset(IEnumFORMATETC *self)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance)
        return E_INVALIDARG;
    instance->index = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enumFormatEtcClone(IEnumFORMATETC *self, IEnumFORMATETC **clone);

static CliprdrEnumFORMATETC *newEnumFormatEtc(ULONG count, FORMATETC *formats)
{
    auto *instance = static_cast<CliprdrEnumFORMATETC*>(calloc(1, sizeof(CliprdrEnumFORMATETC)));
    if (!instance)
        return nullptr;

    auto *vtable = static_cast<IEnumFORMATETCVtbl*>(calloc(1, sizeof(IEnumFORMATETCVtbl)));
    if (!vtable) {
        free(instance);
        return nullptr;
    }

    vtable->QueryInterface = enumFormatEtcQueryInterface;
    vtable->AddRef = enumFormatEtcAddRef;
    vtable->Release = enumFormatEtcRelease;
    vtable->Next = enumFormatEtcNext;
    vtable->Skip = enumFormatEtcSkip;
    vtable->Reset = enumFormatEtcReset;
    vtable->Clone = enumFormatEtcClone;

    instance->iface.lpVtbl = vtable;
    instance->refCount = 1;
    instance->index = 0;
    instance->count = static_cast<LONG>(count);

    if (count > 0) {
        instance->formats = static_cast<FORMATETC*>(calloc(count, sizeof(FORMATETC)));
        if (!instance->formats) {
            deleteEnumFormatEtc(instance);
            return nullptr;
        }
        for (ULONG i = 0; i < count; ++i)
            formatDeepCopy(&instance->formats[i], &formats[i]);
    }

    return instance;
}

static HRESULT STDMETHODCALLTYPE enumFormatEtcClone(IEnumFORMATETC *self, IEnumFORMATETC **clone)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance || !clone)
        return E_INVALIDARG;

    auto *copy = newEnumFormatEtc(static_cast<ULONG>(instance->count), instance->formats);
    if (!copy)
        return E_OUTOFMEMORY;

    copy->index = instance->index;
    *clone = &copy->iface;
    return S_OK;
}

static void deleteStream(CliprdrStream *instance)
{
    if (!instance)
        return;
    free(instance->iface.lpVtbl);
    free(instance);
}

static HRESULT STDMETHODCALLTYPE streamQueryInterface(IStream *self, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;

    if (InlineIsEqualGUID(riid, IID_IStream) || InlineIsEqualGUID(riid, IID_IUnknown)) {
        IStream_AddRef(self);
        *object = self;
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE streamAddRef(IStream *self)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    return instance ? InterlockedIncrement(&instance->refCount) : 0;
}

static ULONG STDMETHODCALLTYPE streamRelease(IStream *self)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    if (!instance)
        return 0;
    const LONG count = InterlockedDecrement(&instance->refCount);
    if (count == 0)
        deleteStream(instance);
    return (count > 0) ? static_cast<ULONG>(count) : 0;
}

static HRESULT STDMETHODCALLTYPE streamRead(IStream *self, void *buffer, ULONG bytes,
                                            ULONG *bytesRead)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    if (!instance || !buffer || !bytesRead)
        return E_INVALIDARG;

    auto *clipboard = instance->clipboard;
    *bytesRead = 0;

    if (instance->offset.QuadPart >= instance->size.QuadPart)
        return S_FALSE;

    const UINT rc = sendFileContentsRequest(clipboard, self, instance->listIndex, FILECONTENTS_RANGE,
                                            instance->offset.QuadPart, bytes);
    if (rc != CHANNEL_RC_OK)
        return E_FAIL;

    ULONG actualRead = 0;
    if (clipboard->requestFileData) {
        actualRead = std::min(clipboard->requestFileSize, bytes);
        CopyMemory(buffer, clipboard->requestFileData, actualRead);
        free(clipboard->requestFileData);
        clipboard->requestFileData = nullptr;
    }

    *bytesRead = actualRead;
    instance->offset.QuadPart += actualRead;

    return (actualRead < bytes) ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE streamWrite(IStream *, const void *, ULONG, ULONG *)
{
    return STG_E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE streamSeek(IStream *self, LARGE_INTEGER move, DWORD origin,
                                            ULARGE_INTEGER *newPosition)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    if (!instance)
        return E_INVALIDARG;

    ULONGLONG offset = instance->offset.QuadPart;
    switch (origin) {
    case STREAM_SEEK_SET:
        offset = move.QuadPart;
        break;
    case STREAM_SEEK_CUR:
        offset += move.QuadPart;
        break;
    case STREAM_SEEK_END:
        offset = instance->size.QuadPart + move.QuadPart;
        break;
    default:
        return E_INVALIDARG;
    }

    if (offset >= instance->size.QuadPart)
        return E_FAIL;

    instance->offset.QuadPart = offset;
    if (newPosition)
        newPosition->QuadPart = instance->offset.QuadPart;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE streamSetSize(IStream *, ULARGE_INTEGER) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE streamCopyTo(IStream *, IStream *, ULARGE_INTEGER, ULARGE_INTEGER *,
                                              ULARGE_INTEGER *) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE streamCommit(IStream *, DWORD) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE streamRevert(IStream *) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE streamLockRegion(IStream *, ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
{
    return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE streamUnlockRegion(IStream *, ULARGE_INTEGER, ULARGE_INTEGER, DWORD)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE streamStat(IStream *self, STATSTG *stat, DWORD flags)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    if (!instance || !stat)
        return STG_E_INVALIDPOINTER;

    ZeroMemory(stat, sizeof(STATSTG));
    if (flags == STATFLAG_DEFAULT)
        return STG_E_INSUFFICIENTMEMORY;
    if (flags == STATFLAG_NOOPEN)
        return STG_E_INVALIDFLAG;

    stat->cbSize.QuadPart = instance->size.QuadPart;
    stat->grfLocksSupported = LOCK_EXCLUSIVE;
    stat->grfMode = GENERIC_READ;
    stat->type = STGTY_STREAM;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE streamClone(IStream *, IStream **) { return E_NOTIMPL; }

static CliprdrStream *newStream(ULONG index, ClipboardContext *clipboard,
                                const FILEDESCRIPTORW *descriptor)
{
    auto *instance = static_cast<CliprdrStream*>(calloc(1, sizeof(CliprdrStream)));
    if (!instance)
        return nullptr;

    auto *vtable = static_cast<IStreamVtbl*>(calloc(1, sizeof(IStreamVtbl)));
    if (!vtable) {
        free(instance);
        return nullptr;
    }

    vtable->QueryInterface = streamQueryInterface;
    vtable->AddRef = streamAddRef;
    vtable->Release = streamRelease;
    vtable->Read = streamRead;
    vtable->Write = streamWrite;
    vtable->Seek = streamSeek;
    vtable->SetSize = streamSetSize;
    vtable->CopyTo = streamCopyTo;
    vtable->Commit = streamCommit;
    vtable->Revert = streamRevert;
    vtable->LockRegion = streamLockRegion;
    vtable->UnlockRegion = streamUnlockRegion;
    vtable->Stat = streamStat;
    vtable->Clone = streamClone;

    instance->iface.lpVtbl = vtable;
    instance->refCount = 1;
    instance->listIndex = index;
    instance->clipboard = clipboard;
    instance->descriptor = *descriptor;
    instance->offset.QuadPart = 0;
    instance->size.QuadPart =
        (static_cast<UINT64>(descriptor->nFileSizeHigh) << 32) | descriptor->nFileSizeLow;

    const bool isDirectory =
        (descriptor->dwFlags & FD_ATTRIBUTES) && (descriptor->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

    if (((descriptor->dwFlags & FD_FILESIZE) == 0) && !isDirectory) {
        if (sendFileContentsRequest(clipboard, instance, index, FILECONTENTS_SIZE, 0, 8) != CHANNEL_RC_OK) {
            deleteStream(instance);
            return nullptr;
        }

        if (!clipboard->requestFileData) {
            deleteStream(instance);
            return nullptr;
        }

        instance->size.QuadPart = *reinterpret_cast<UINT64*>(clipboard->requestFileData);
        free(clipboard->requestFileData);
        clipboard->requestFileData = nullptr;
    }

    return instance;
}

static LONG lookupFormat(CliprdrDataObject *instance, FORMATETC *format)
{
    if (!instance || !format)
        return -1;

    for (ULONG i = 0; i < instance->formatCount; ++i) {
        const auto &candidate = instance->formats[i];
        if ((format->tymed & candidate.tymed) &&
            format->cfFormat == candidate.cfFormat &&
            (format->dwAspect & candidate.dwAspect)) {
            return static_cast<LONG>(i);
        }
    }

    return -1;
}

static void deleteDataObject(CliprdrDataObject *instance)
{
    if (!instance)
        return;

    free(instance->iface.lpVtbl);
    free(instance->formats);
    free(instance->mediums);

    if (instance->streams) {
        for (ULONG i = 0; i < instance->streamCount; ++i) {
            if (instance->streams[i])
                IStream_Release(instance->streams[i]);
        }
        free(instance->streams);
    }

    free(instance);
}

static HRESULT STDMETHODCALLTYPE dataObjectQueryInterface(IDataObject *self, REFIID riid,
                                                          void **object)
{
    if (!object)
        return E_INVALIDARG;

    if (InlineIsEqualGUID(riid, IID_IDataObject) || InlineIsEqualGUID(riid, IID_IUnknown)) {
        IDataObject_AddRef(self);
        *object = self;
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dataObjectAddRef(IDataObject *self)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    return instance ? InterlockedIncrement(&instance->refCount) : 0;
}

static ULONG STDMETHODCALLTYPE dataObjectRelease(IDataObject *self)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    if (!instance)
        return 0;

    const LONG count = InterlockedDecrement(&instance->refCount);
    if (count == 0)
        deleteDataObject(instance);
    return (count > 0) ? static_cast<ULONG>(count) : 0;
}

static HRESULT STDMETHODCALLTYPE dataObjectGetData(IDataObject *self, FORMATETC *format,
                                                   STGMEDIUM *medium)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    if (!instance || !format || !medium)
        return E_INVALIDARG;

    auto *clipboard = instance->clipboard;
    if (!clipboard)
        return E_INVALIDARG;

    const LONG index = lookupFormat(instance, format);
    if (index < 0)
        return DV_E_FORMATETC;

    medium->tymed = instance->formats[index].tymed;
    medium->pUnkForRelease = nullptr;

    const UINT fileDescriptorFormat = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    const UINT fileContentsFormat = RegisterClipboardFormat(CFSTR_FILECONTENTS);

    if (instance->formats[index].cfFormat == fileDescriptorFormat) {
        const UINT remoteFormat = getRemoteFormatId(clipboard, instance->formats[index].cfFormat);
        if (sendDataRequest(clipboard, remoteFormat) != CHANNEL_RC_OK)
            return E_UNEXPECTED;

        STGMEDIUM_HGLOBAL((*medium)) = clipboard->responseData;
        auto *group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(clipboard->responseData));
        if (!group)
            return E_UNEXPECTED;

        instance->streamCount = group->cItems;
        GlobalUnlock(clipboard->responseData);

        if (!instance->streams) {
            instance->streams = static_cast<IStream**>(calloc(instance->streamCount, sizeof(IStream*)));
            if (!instance->streams)
                return E_OUTOFMEMORY;

            for (ULONG i = 0; i < instance->streamCount; ++i) {
                instance->streams[i] = reinterpret_cast<IStream*>(newStream(i, clipboard, &group->fgd[i]));
                if (!instance->streams[i])
                    return E_OUTOFMEMORY;
            }
        }
    } else if (instance->formats[index].cfFormat == fileContentsFormat) {
        if (format->lindex < 0 || static_cast<ULONG>(format->lindex) >= instance->streamCount)
            return E_INVALIDARG;

        STGMEDIUM_PSTM((*medium)) = instance->streams[format->lindex];
        IStream_AddRef(instance->streams[format->lindex]);
    } else {
        return E_UNEXPECTED;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dataObjectGetDataHere(IDataObject *, FORMATETC *, STGMEDIUM *)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dataObjectQueryGetData(IDataObject *self, FORMATETC *format)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    if (!instance || !format)
        return E_INVALIDARG;
    return (lookupFormat(instance, format) >= 0) ? S_OK : DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE dataObjectGetCanonicalFormatEtc(IDataObject *, FORMATETC *,
                                                                 FORMATETC *output)
{
    if (!output)
        return E_INVALIDARG;
    output->ptd = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dataObjectSetData(IDataObject *, FORMATETC *, STGMEDIUM *, BOOL)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dataObjectEnumFormatEtc(IDataObject *self, DWORD direction,
                                                         IEnumFORMATETC **enumerator)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    if (!instance || !enumerator)
        return E_INVALIDARG;
    if (direction != DATADIR_GET)
        return E_NOTIMPL;

    auto *value = newEnumFormatEtc(instance->formatCount, instance->formats);
    if (!value)
        return E_OUTOFMEMORY;
    *enumerator = &value->iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dataObjectDAdvise(IDataObject *, FORMATETC *, DWORD, IAdviseSink *, DWORD *)
{
    return OLE_E_ADVISENOTSUPPORTED;
}
static HRESULT STDMETHODCALLTYPE dataObjectDUnadvise(IDataObject *, DWORD)
{
    return OLE_E_ADVISENOTSUPPORTED;
}
static HRESULT STDMETHODCALLTYPE dataObjectEnumDAdvise(IDataObject *, IEnumSTATDATA **)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

static CliprdrDataObject *newDataObject(FORMATETC *formats, STGMEDIUM *mediums, ULONG count,
                                        ClipboardContext *clipboard)
{
    auto *instance = static_cast<CliprdrDataObject*>(calloc(1, sizeof(CliprdrDataObject)));
    if (!instance)
        return nullptr;

    auto *vtable = static_cast<IDataObjectVtbl*>(calloc(1, sizeof(IDataObjectVtbl)));
    if (!vtable) {
        free(instance);
        return nullptr;
    }

    vtable->QueryInterface = dataObjectQueryInterface;
    vtable->AddRef = dataObjectAddRef;
    vtable->Release = dataObjectRelease;
    vtable->GetData = dataObjectGetData;
    vtable->GetDataHere = dataObjectGetDataHere;
    vtable->QueryGetData = dataObjectQueryGetData;
    vtable->GetCanonicalFormatEtc = dataObjectGetCanonicalFormatEtc;
    vtable->SetData = dataObjectSetData;
    vtable->EnumFormatEtc = dataObjectEnumFormatEtc;
    vtable->DAdvise = dataObjectDAdvise;
    vtable->DUnadvise = dataObjectDUnadvise;
    vtable->EnumDAdvise = dataObjectEnumDAdvise;

    instance->iface.lpVtbl = vtable;
    instance->refCount = 1;
    instance->clipboard = clipboard;
    instance->formatCount = count;

    if (count > 0) {
        instance->formats = static_cast<FORMATETC*>(calloc(count, sizeof(FORMATETC)));
        instance->mediums = static_cast<STGMEDIUM*>(calloc(count, sizeof(STGMEDIUM)));
        if (!instance->formats || !instance->mediums) {
            deleteDataObject(instance);
            return nullptr;
        }
        for (ULONG i = 0; i < count; ++i) {
            instance->formats[i] = formats[i];
            instance->mediums[i] = mediums[i];
        }
    }

    return instance;
}

static BOOL createFileDataObject(ClipboardContext *clipboard, IDataObject **dataObject)
{
    if (!dataObject)
        return FALSE;

    std::array<FORMATETC, 2> formats = {};
    std::array<STGMEDIUM, 2> mediums = {};

    formats[0].cfFormat = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    formats[0].dwAspect = DVASPECT_CONTENT;
    formats[0].tymed = TYMED_HGLOBAL;
    mediums[0].tymed = TYMED_HGLOBAL;

    formats[1].cfFormat = RegisterClipboardFormat(CFSTR_FILECONTENTS);
    formats[1].dwAspect = DVASPECT_CONTENT;
    formats[1].tymed = TYMED_ISTREAM;
    mediums[1].tymed = TYMED_ISTREAM;

    auto *instance = newDataObject(formats.data(), mediums.data(), 2, clipboard);
    if (!instance)
        return FALSE;

    *dataObject = &instance->iface;
    return TRUE;
}

static void destroyFileDataObject(IDataObject *instance)
{
    if (instance)
        IDataObject_Release(instance);
}

static UINT32 getLocalFormatIdByName(ClipboardContext *clipboard, const TCHAR *formatName)
{
    if (!clipboard || !formatName)
        return 0;

#if defined(UNICODE)
    WCHAR *unicodeName = _wcsdup(formatName);
#else
    WCHAR *unicodeName = ConvertUtf8ToWCharAlloc(formatName, nullptr);
#endif
    if (!unicodeName)
        return 0;

    for (size_t i = 0; i < clipboard->mapSize; ++i) {
        auto &mapping = clipboard->formatMappings[i];
        if (mapping.name && wcscmp(mapping.name, unicodeName) == 0) {
            free(unicodeName);
            return mapping.localFormatId;
        }
    }

    free(unicodeName);
    return 0;
}

static BOOL fileTransferring(ClipboardContext *clipboard)
{
    return getLocalFormatIdByName(clipboard, CFSTR_FILEDESCRIPTORW) ? TRUE : FALSE;
}

static UINT32 getRemoteFormatId(ClipboardContext *clipboard, UINT32 localFormat)
{
    if (!clipboard)
        return 0;
    for (size_t i = 0; i < clipboard->mapSize; ++i) {
        auto &mapping = clipboard->formatMappings[i];
        if (mapping.localFormatId == localFormat)
            return mapping.remoteFormatId;
    }
    return localFormat;
}

static void ensureMapCapacity(ClipboardContext *clipboard)
{
    if (!clipboard || clipboard->mapSize < clipboard->mapCapacity)
        return;

    size_t newSize = clipboard->mapCapacity;
    do {
        newSize += 128;
    } while (newSize <= clipboard->mapSize);

    auto *newMap = static_cast<FormatMapping*>(realloc(clipboard->formatMappings, sizeof(FormatMapping) * newSize));
    if (!newMap)
        return;

    ZeroMemory(newMap + clipboard->mapCapacity, sizeof(FormatMapping) * (newSize - clipboard->mapCapacity));
    clipboard->formatMappings = newMap;
    clipboard->mapCapacity = newSize;
}

static BOOL clearFormatMap(ClipboardContext *clipboard)
{
    if (!clipboard)
        return FALSE;

    if (clipboard->formatMappings) {
        for (size_t i = 0; i < clipboard->mapCapacity; ++i) {
            auto &mapping = clipboard->formatMappings[i];
            mapping.remoteFormatId = 0;
            mapping.localFormatId = 0;
            free(mapping.name);
            mapping.name = nullptr;
        }
    }

    clipboard->mapSize = 0;
    return TRUE;
}

static UINT sendTempDirectory(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->context || !clipboard->context->TempDirectory)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_TEMP_DIRECTORY tempDirectory = {};
    if (GetEnvironmentVariableA("TEMP", tempDirectory.szTempDir, ARRAYSIZE(tempDirectory.szTempDir)) == 0)
        return ERROR_INTERNAL_ERROR;
    return clipboard->context->TempDirectory(clipboard->context, &tempDirectory);
}

static UINT sendFormatList(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFormatList)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FORMAT_LIST formatList = {};
    CLIPRDR_FORMAT *formats = nullptr;
    UINT32 numFormats = 0;

    if (tryOpenClipboard(clipboard->hwnd)) {
        const int count = CountClipboardFormats();
        numFormats = static_cast<UINT32>(count);
        formats = static_cast<CLIPRDR_FORMAT*>(calloc(numFormats ? numFormats : 1, sizeof(CLIPRDR_FORMAT)));
        if (!formats) {
            CloseClipboard();
            return CHANNEL_RC_NO_MEMORY;
        }

        UINT32 index = 0;
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            formats[index++].formatId = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
            formats[index++].formatId = RegisterClipboardFormat(CFSTR_FILECONTENTS);
        } else {
            UINT32 formatId = 0;
            while ((formatId = EnumClipboardFormats(formatId)) != 0)
                formats[index++].formatId = formatId;
        }

        numFormats = index;
        if (!CloseClipboard() && GetLastError()) {
            free(formats);
            return ERROR_INTERNAL_ERROR;
        }

        char formatName[1024] = {};
        for (UINT32 i = 0; i < numFormats; ++i) {
            if (GetClipboardFormatNameA(formats[i].formatId, formatName, sizeof(formatName)))
                formats[i].formatName = _strdup(formatName);
        }
    }

    formatList.common.msgType = CB_FORMAT_LIST;
    formatList.numFormats = numFormats;
    formatList.formats = formats;
    const UINT rc = clipboard->context->ClientFormatList(clipboard->context, &formatList);

    for (UINT32 i = 0; i < numFormats; ++i)
        free(formats[i].formatName);
    free(formats);
    return rc;
}

static UINT sendDataRequest(ClipboardContext *clipboard, UINT32 formatId)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFormatDataRequest)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FORMAT_DATA_REQUEST request = {};
    request.requestedFormatId = getRemoteFormatId(clipboard, formatId);
    clipboard->requestedFormatId = formatId;

    if (!ResetEvent(clipboard->responseDataEvent))
        return ERROR_INTERNAL_ERROR;

    UINT rc = clipboard->context->ClientFormatDataRequest(clipboard->context, &request);
    if (rc == CHANNEL_RC_OK)
        rc = waitForClipboardResponse(clipboard, clipboard->responseDataEvent);
    return rc;
}

static UINT sendFileContentsRequest(ClipboardContext *clipboard, const void *streamId, ULONG index,
                                    UINT32 flags, UINT64 position, ULONG requestSize)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFileContentsRequest)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FILE_CONTENTS_REQUEST request = {};
    request.streamId = static_cast<UINT32>(reinterpret_cast<ULONG_PTR>(streamId));
    request.listIndex = index;
    request.dwFlags = flags;
    request.nPositionLow = static_cast<UINT32>(position & 0xFFFFFFFFULL);
    request.nPositionHigh = static_cast<UINT32>((position >> 32) & 0xFFFFFFFFULL);
    request.cbRequested = requestSize;
    request.clipDataId = 0;

    if (!ResetEvent(clipboard->requestFileEvent))
        return ERROR_INTERNAL_ERROR;

    UINT rc = clipboard->context->ClientFileContentsRequest(clipboard->context, &request);
    if (rc == CHANNEL_RC_OK)
        rc = waitForClipboardResponse(clipboard, clipboard->requestFileEvent);
    return rc;
}

static UINT sendFileContentsResponse(ClipboardContext *clipboard, UINT32 streamId, UINT32 size,
                                     BYTE *data)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFileContentsResponse)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FILE_CONTENTS_RESPONSE response = {};
    response.streamId = streamId;
    response.cbRequested = size;
    response.requestedData = data;
    response.common.msgFlags = CB_RESPONSE_OK;
    return clipboard->context->ClientFileContentsResponse(clipboard->context, &response);
}

static UINT waitForClipboardResponse(ClipboardContext *clipboard, HANDLE responseEvent)
{
    if (!clipboard || !responseEvent || !clipboard->abortEvent)
        return ERROR_INTERNAL_ERROR;

    HANDLE handles[2] = { responseEvent, clipboard->abortEvent };
    const DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, kClipboardRequestTimeoutMs);
    if (waitResult == WAIT_OBJECT_0) {
        return ResetEvent(responseEvent) ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
    }
    if (waitResult == WAIT_OBJECT_0 + 1)
        return ERROR_CANCELLED;
    if (waitResult == WAIT_TIMEOUT)
        return ERROR_TIMEOUT;
    return ERROR_INTERNAL_ERROR;
}

static void clearFileArray(ClipboardContext *clipboard)
{
    if (!clipboard)
        return;

    if (clipboard->fileNames) {
        for (size_t i = 0; i < clipboard->fileCount; ++i)
            free(clipboard->fileNames[i]);
        free(clipboard->fileNames);
        clipboard->fileNames = nullptr;
    }

    if (clipboard->fileDescriptors) {
        for (size_t i = 0; i < clipboard->fileCount; ++i)
            free(clipboard->fileDescriptors[i]);
        free(clipboard->fileDescriptors);
        clipboard->fileDescriptors = nullptr;
    }

    clipboard->fileArraySize = 0;
    clipboard->fileCount = 0;
}

static BOOL getFileContents(WCHAR *fileName, BYTE *buffer, LONG positionLow, LONG positionHigh,
                            DWORD requested, DWORD *size)
{
    if (!fileName || !buffer || !size)
        return FALSE;

    HANDLE file = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    DWORD read = 0;
    DWORD result = SetFilePointer(file, positionLow, &positionHigh, FILE_BEGIN);
    BOOL ok = (result != INVALID_SET_FILE_POINTER) && ReadFile(file, buffer, requested, &read, nullptr);
    CloseHandle(file);
    if (ok)
        *size = read;
    return ok;
}

static FILEDESCRIPTORW *createFileDescriptor(WCHAR *fileName, size_t pathLength)
{
    auto *descriptor = static_cast<FILEDESCRIPTORW*>(calloc(1, sizeof(FILEDESCRIPTORW)));
    if (!descriptor)
        return nullptr;

    HANDLE file = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        free(descriptor);
        return nullptr;
    }

    descriptor->dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_WRITESTIME | FD_PROGRESSUI;
    descriptor->dwFileAttributes = GetFileAttributesW(fileName);
    if (!GetFileTime(file, nullptr, nullptr, &descriptor->ftLastWriteTime))
        descriptor->dwFlags &= ~FD_WRITESTIME;
    descriptor->nFileSizeLow = GetFileSize(file, &descriptor->nFileSizeHigh);
    wcscpy_s(descriptor->cFileName, ARRAYSIZE(descriptor->cFileName), fileName + pathLength);
    CloseHandle(file);
    return descriptor;
}

static BOOL ensureFileArrayCapacity(ClipboardContext *clipboard)
{
    if (!clipboard)
        return FALSE;
    if (clipboard->fileCount != clipboard->fileArraySize)
        return TRUE;

    const size_t newSize = (clipboard->fileArraySize + 1) * 2;
    auto *newDescriptors = static_cast<FILEDESCRIPTORW**>(
        realloc(clipboard->fileDescriptors, newSize * sizeof(FILEDESCRIPTORW*)));
    auto *newNames = static_cast<WCHAR**>(
        realloc(clipboard->fileNames, newSize * sizeof(WCHAR*)));

    if (!newDescriptors || !newNames)
        return FALSE;

    clipboard->fileDescriptors = newDescriptors;
    clipboard->fileNames = newNames;
    clipboard->fileArraySize = newSize;
    return TRUE;
}

static BOOL addToFileArrays(ClipboardContext *clipboard, WCHAR *fullFileName, size_t pathLength)
{
    if (!clipboard || clipboard->fileCount >= kMaxClipboardFileCount)
        return FALSE;

    if (!ensureFileArrayCapacity(clipboard))
        return FALSE;

    clipboard->fileNames[clipboard->fileCount] = static_cast<WCHAR*>(malloc(MAX_PATH * sizeof(WCHAR)));
    if (!clipboard->fileNames[clipboard->fileCount])
        return FALSE;
    wcscpy_s(clipboard->fileNames[clipboard->fileCount], MAX_PATH, fullFileName);

    clipboard->fileDescriptors[clipboard->fileCount] = createFileDescriptor(fullFileName, pathLength);
    if (!clipboard->fileDescriptors[clipboard->fileCount]) {
        free(clipboard->fileNames[clipboard->fileCount]);
        clipboard->fileNames[clipboard->fileCount] = nullptr;
        return FALSE;
    }

    ++clipboard->fileCount;
    return TRUE;
}

static BOOL traverseDirectory(ClipboardContext *clipboard, WCHAR *directory, size_t pathLength, int depth)
{
    if (!clipboard || !directory)
        return FALSE;
    if (depth >= kMaxClipboardDirectoryDepth)
        return FALSE;

    WIN32_FIND_DATAW findData = {};
    WCHAR pattern[MAX_PATH] = {};
    StringCchCopyW(pattern, ARRAYSIZE(pattern), directory);
    StringCchCatW(pattern, ARRAYSIZE(pattern), L"\\*");

    HANDLE find = FindFirstFileW(pattern, &findData);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;

    BOOL success = TRUE;
    do {
        if (((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
             wcscmp(findData.cFileName, L".") == 0) ||
            wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            continue;

        WCHAR path[MAX_PATH] = {};
        StringCchCopyW(path, ARRAYSIZE(path), directory);
        StringCchCatW(path, ARRAYSIZE(path), L"\\");
        StringCchCatW(path, ARRAYSIZE(path), findData.cFileName);

        if (!addToFileArrays(clipboard, path, pathLength)) {
            success = FALSE;
            break;
        }

        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!traverseDirectory(clipboard, path, pathLength, depth + 1)) {
                success = FALSE;
                break;
            }
        }
    } while (FindNextFileW(find, &findData));

    FindClose(find);
    return success;
}

static BOOL processClipboardFilename(ClipboardContext *clipboard, WCHAR *fileName, size_t length)
{
    if (!clipboard || !fileName)
        return FALSE;

    size_t offset = length;
    while (offset > 0) {
        if (fileName[offset] == L'\\')
            break;
        --offset;
    }

    const size_t pathLength = offset + 1;
    if (!addToFileArrays(clipboard, fileName, pathLength))
        return FALSE;

    const DWORD attributes = clipboard->fileDescriptors[clipboard->fileCount - 1]->dwFileAttributes;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        return traverseDirectory(clipboard, fileName, pathLength, 0);
    }

    return TRUE;
}

static SSIZE_T tryOpenClipboardData(ClipboardContext *clipboard, UINT32 requestedFormatId, BYTE **data)
{
    WINPR_ASSERT(clipboard);
    WINPR_ASSERT(data);
    *data = nullptr;

    if (!tryOpenClipboard(clipboard->hwnd))
        return 0;

    HANDLE clipData = GetClipboardData(requestedFormatId);
    if (!clipData) {
        CloseClipboard();
        return -1;
    }

    char *memory = static_cast<char*>(GlobalLock(clipData));
    const SSIZE_T size = static_cast<SSIZE_T>(GlobalSize(clipData));
    if (!memory || size <= 0) {
        if (memory)
            GlobalUnlock(clipData);
        CloseClipboard();
        return -1;
    }

    BYTE *buffer = static_cast<BYTE*>(malloc(size));
    if (!buffer) {
        GlobalUnlock(clipData);
        CloseClipboard();
        return -1;
    }

    CopyMemory(buffer, memory, size);
    *data = buffer;
    GlobalUnlock(clipData);
    CloseClipboard();
    return size;
}

static SSIZE_T buildFileDescriptorList(ClipboardContext *clipboard, BYTE **data)
{
    WINPR_ASSERT(clipboard);
    WINPR_ASSERT(data);
    *data = nullptr;

    IDataObject *dataObject = nullptr;
    FORMATETC format = {};
    STGMEDIUM medium = {};
    const HRESULT result = OleGetClipboard(&dataObject);
    if (FAILED(result))
        return -1;

    format.cfFormat = CF_HDROP;
    format.tymed = TYMED_HGLOBAL;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = -1;

    if (FAILED(IDataObject_GetData(dataObject, &format, &medium))) {
        IDataObject_Release(dataObject);
        return -1;
    }

    auto *dropFiles = static_cast<DROPFILES*>(GlobalLock(STGMEDIUM_HGLOBAL(medium)));
    if (!dropFiles) {
        ReleaseStgMedium(&medium);
        IDataObject_Release(dataObject);
        return -1;
    }

    clearFileArray(clipboard);

    if (dropFiles->fWide) {
        auto *base = reinterpret_cast<BYTE*>(dropFiles);
        for (WCHAR *fileName = reinterpret_cast<WCHAR*>(base + dropFiles->pFiles);
             *fileName; fileName += wcslen(fileName) + 1) {
            if (!processClipboardFilename(clipboard, fileName, wcslen(fileName))) {
                GlobalUnlock(STGMEDIUM_HGLOBAL(medium));
                ReleaseStgMedium(&medium);
                IDataObject_Release(dataObject);
                return -1;
            }
        }
    } else {
        auto *base = reinterpret_cast<BYTE*>(dropFiles);
        for (char *fileName = reinterpret_cast<char*>(base + dropFiles->pFiles);
             *fileName; fileName += strlen(fileName) + 1) {
            WCHAR *wideName = ConvertUtf8ToWCharAlloc(fileName, nullptr);
            if (wideName) {
                if (!processClipboardFilename(clipboard, wideName, wcslen(wideName))) {
                    free(wideName);
                    GlobalUnlock(STGMEDIUM_HGLOBAL(medium));
                    ReleaseStgMedium(&medium);
                    IDataObject_Release(dataObject);
                    return -1;
                }
                free(wideName);
            }
        }
    }

    GlobalUnlock(STGMEDIUM_HGLOBAL(medium));
    ReleaseStgMedium(&medium);

    const size_t size = 4ull + clipboard->fileCount * sizeof(FILEDESCRIPTORW);
    auto *group = static_cast<FILEGROUPDESCRIPTORW*>(calloc(size, 1));
    if (!group) {
        IDataObject_Release(dataObject);
        return -1;
    }

    group->cItems = static_cast<UINT>(clipboard->fileCount);
    for (size_t i = 0; i < clipboard->fileCount; ++i) {
        if (clipboard->fileDescriptors[i])
            group->fgd[i] = *clipboard->fileDescriptors[i];
    }

    *data = reinterpret_cast<BYTE*>(group);
    IDataObject_Release(dataObject);
    return static_cast<SSIZE_T>(size);
}

static UINT sendClientCapabilities(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientCapabilities)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_GENERAL_CAPABILITY_SET general = {};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES | CB_STREAM_FILECLIP_ENABLED | CB_FILECLIP_NO_FILE_PATHS;

    CLIPRDR_CAPABILITIES capabilities = {};
    capabilities.cCapabilitiesSets = 1;
    capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);
    return clipboard->context->ClientCapabilities(clipboard->context, &capabilities);
}

static UINT CALLBACK onMonitorReady(CliprdrClientContext *context, const CLIPRDR_MONITOR_READY *ready)
{
    if (!context || !ready)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    clipboard->sync = true;

    UINT rc = sendClientCapabilities(clipboard);
    if (rc != CHANNEL_RC_OK)
        return rc;

    rc = sendTempDirectory(clipboard);
    if (rc != CHANNEL_RC_OK)
        return rc;

    return sendFormatList(clipboard);
}

static UINT CALLBACK onServerCapabilities(CliprdrClientContext *context, const CLIPRDR_CAPABILITIES *capabilities)
{
    if (!context || !capabilities)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    for (UINT32 i = 0; i < capabilities->cCapabilitiesSets; ++i) {
        auto *set = &capabilities->capabilitySets[i];
        if (set->capabilitySetType == CB_CAPSTYPE_GENERAL && set->capabilitySetLength >= CB_CAPSTYPE_GENERAL_LEN) {
            auto *general = reinterpret_cast<CLIPRDR_GENERAL_CAPABILITY_SET*>(set);
            clipboard->capabilities = general->generalFlags;
            break;
        }
    }
    return CHANNEL_RC_OK;
}

static UINT CALLBACK onServerFormatList(CliprdrClientContext *context, const CLIPRDR_FORMAT_LIST *formatList)
{
    if (!context || !formatList)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    clearFormatMap(clipboard);

    for (UINT32 i = 0; i < formatList->numFormats; ++i) {
        ensureMapCapacity(clipboard);
        auto &mapping = clipboard->formatMappings[clipboard->mapSize];
        const auto &format = formatList->formats[i];
        mapping.remoteFormatId = format.formatId;

        if (format.formatName) {
            mapping.name = ConvertUtf8ToWCharAlloc(format.formatName, nullptr);
            if (mapping.name)
                mapping.localFormatId = RegisterClipboardFormatW(mapping.name);
        } else {
            mapping.localFormatId = mapping.remoteFormatId;
        }
        ++clipboard->mapSize;
    }

    if (fileTransferring(clipboard)) {
        return PostMessage(clipboard->hwnd, WM_CLIPRDR_MESSAGE, OLE_SETCLIPBOARD, 0)
            ? CHANNEL_RC_OK
            : ERROR_INTERNAL_ERROR;
    }

    if (!tryOpenClipboard(clipboard->hwnd))
        return CHANNEL_RC_OK;

    UINT rc = ERROR_INTERNAL_ERROR;
    if (EmptyClipboard()) {
        for (size_t i = 0; i < clipboard->mapSize; ++i)
            SetClipboardData(clipboard->formatMappings[i].localFormatId, nullptr);
        rc = CHANNEL_RC_OK;
    }

    if (!CloseClipboard() && GetLastError())
        return ERROR_INTERNAL_ERROR;
    return rc;
}

static UINT CALLBACK onServerFormatListResponse(CliprdrClientContext *, const CLIPRDR_FORMAT_LIST_RESPONSE *)
{
    return CHANNEL_RC_OK;
}

static UINT CALLBACK onServerLockClipboardData(CliprdrClientContext *, const CLIPRDR_LOCK_CLIPBOARD_DATA *)
{
    return CHANNEL_RC_OK;
}

static UINT CALLBACK onServerUnlockClipboardData(CliprdrClientContext *, const CLIPRDR_UNLOCK_CLIPBOARD_DATA *)
{
    return CHANNEL_RC_OK;
}

static UINT CALLBACK onServerFormatDataRequest(CliprdrClientContext *context,
                                               const CLIPRDR_FORMAT_DATA_REQUEST *request)
{
    if (!context || !request)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    BYTE *data = nullptr;
    const UINT fileDescriptorFormat = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    const SSIZE_T size = (request->requestedFormatId == fileDescriptorFormat)
        ? buildFileDescriptorList(clipboard, &data)
        : tryOpenClipboardData(clipboard, request->requestedFormatId, &data);

    UINT rc = ERROR_INTERNAL_ERROR;
    if (size >= 0) {
        CLIPRDR_FORMAT_DATA_RESPONSE response = {};
        response.common.msgType = CB_FORMAT_DATA_RESPONSE;
        response.common.msgFlags = CB_RESPONSE_OK;
        response.common.dataLen = static_cast<UINT32>(size);
        response.requestedFormatData = data;
        rc = clipboard->context->ClientFormatDataResponse(clipboard->context, &response);
    } else {
        CLIPRDR_FORMAT_DATA_RESPONSE response = {};
        response.common.msgType = CB_FORMAT_DATA_RESPONSE;
        response.common.msgFlags = CB_RESPONSE_FAIL;
        rc = clipboard->context->ClientFormatDataResponse(clipboard->context, &response);
    }

    free(data);
    return rc;
}

static UINT CALLBACK onServerFormatDataResponse(CliprdrClientContext *context,
                                                const CLIPRDR_FORMAT_DATA_RESPONSE *response)
{
    if (!context || !response)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    if (clipboard->responseData) {
        GlobalFree(clipboard->responseData);
        clipboard->responseData = nullptr;
    }

    if (response->common.msgFlags != CB_RESPONSE_OK) {
        return SetEvent(clipboard->responseDataEvent) ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
    }

    HANDLE data = GlobalAlloc(GMEM_MOVEABLE, response->common.dataLen);
    if (!data)
        return ERROR_INTERNAL_ERROR;

    BYTE *memory = static_cast<BYTE*>(GlobalLock(data));
    if (!memory) {
        GlobalFree(data);
        return ERROR_INTERNAL_ERROR;
    }

    CopyMemory(memory, response->requestedFormatData, response->common.dataLen);
    if (!GlobalUnlock(data) && GetLastError()) {
        GlobalFree(data);
        return ERROR_INTERNAL_ERROR;
    }

    clipboard->responseData = data;
    return SetEvent(clipboard->responseDataEvent) ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
}

static UINT CALLBACK onServerFileContentsRequest(CliprdrClientContext *context,
                                                 const CLIPRDR_FILE_CONTENTS_REQUEST *request)
{
    if (!context || !request)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    UINT32 requestedSize = request->cbRequested;
    if (request->dwFlags == FILECONTENTS_SIZE)
        requestedSize = sizeof(UINT64);

    BYTE *data = static_cast<BYTE*>(calloc(1, requestedSize));
    if (!data)
        return ERROR_INTERNAL_ERROR;

    UINT actualSize = 0;
    UINT rc = ERROR_INTERNAL_ERROR;

    IDataObject *dataObject = nullptr;
    if (SUCCEEDED(OleGetClipboard(&dataObject))) {
        FORMATETC format = {};
        format.cfFormat = RegisterClipboardFormat(CFSTR_FILECONTENTS);
        format.tymed = TYMED_ISTREAM;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = request->listIndex;

        STGMEDIUM medium = {};
        const HRESULT result = IDataObject_GetData(dataObject, &format, &medium);
        if (SUCCEEDED(result) && medium.tymed == TYMED_ISTREAM && STGMEDIUM_PSTM(medium)) {
            if (request->dwFlags == FILECONTENTS_SIZE) {
                STATSTG stat = {};
                if (SUCCEEDED(IStream_Stat(STGMEDIUM_PSTM(medium), &stat, STATFLAG_NONAME))) {
                    *reinterpret_cast<UINT64*>(data) = stat.cbSize.QuadPart;
                    actualSize = requestedSize;
                    rc = CHANNEL_RC_OK;
                }
            } else if (request->dwFlags == FILECONTENTS_RANGE) {
                LARGE_INTEGER offset = {};
                offset.QuadPart = (static_cast<UINT64>(request->nPositionHigh) << 32) | request->nPositionLow;
                ULARGE_INTEGER newPosition = {};
                if (SUCCEEDED(IStream_Seek(STGMEDIUM_PSTM(medium), offset, STREAM_SEEK_SET, &newPosition))) {
                    ULONG bytesRead = 0;
                    if (SUCCEEDED(IStream_Read(STGMEDIUM_PSTM(medium), data, requestedSize, &bytesRead))) {
                        actualSize = bytesRead;
                        rc = CHANNEL_RC_OK;
                    }
                }
            }
            ReleaseStgMedium(&medium);
        }

        IDataObject_Release(dataObject);
    }

    if (rc != CHANNEL_RC_OK) {
        if (request->listIndex < clipboard->fileCount) {
            if (request->dwFlags == FILECONTENTS_SIZE) {
                *reinterpret_cast<UINT32*>(&data[0]) = clipboard->fileDescriptors[request->listIndex]->nFileSizeLow;
                *reinterpret_cast<UINT32*>(&data[4]) = clipboard->fileDescriptors[request->listIndex]->nFileSizeHigh;
                actualSize = requestedSize;
                rc = CHANNEL_RC_OK;
            } else if (request->dwFlags == FILECONTENTS_RANGE) {
                DWORD fileSize = 0;
                if (getFileContents(clipboard->fileNames[request->listIndex], data,
                                    static_cast<LONG>(request->nPositionLow),
                                    static_cast<LONG>(request->nPositionHigh),
                                    requestedSize, &fileSize)) {
                    actualSize = fileSize;
                    rc = CHANNEL_RC_OK;
                }
            }
        }
    }

    UINT sendRc = sendFileContentsResponse(clipboard, request->streamId, actualSize,
                                           (actualSize > 0) ? data : nullptr);
    free(data);
    return (sendRc == CHANNEL_RC_OK) ? rc : sendRc;
}

static UINT CALLBACK onServerFileContentsResponse(CliprdrClientContext *context,
                                                  const CLIPRDR_FILE_CONTENTS_RESPONSE *response)
{
    if (!context || !response)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    if (response->common.msgFlags != CB_RESPONSE_OK) {
        if (clipboard->requestFileData) {
            free(clipboard->requestFileData);
            clipboard->requestFileData = nullptr;
        }
        clipboard->requestFileSize = 0;
        return SetEvent(clipboard->requestFileEvent) ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
    }

    const UINT32 dataSize = std::min(response->cbRequested, response->common.dataLen);
    if (clipboard->requestFileData) {
        free(clipboard->requestFileData);
        clipboard->requestFileData = nullptr;
    }
    clipboard->requestFileSize = dataSize;
    if (dataSize == 0)
        return SetEvent(clipboard->requestFileEvent) ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;

    clipboard->requestFileData = static_cast<char*>(malloc(dataSize));
    if (!clipboard->requestFileData)
        return ERROR_INTERNAL_ERROR;

    CopyMemory(clipboard->requestFileData, response->requestedData, dataSize);
    if (!SetEvent(clipboard->requestFileEvent)) {
        free(clipboard->requestFileData);
        clipboard->requestFileData = nullptr;
        return ERROR_INTERNAL_ERROR;
    }

    return CHANNEL_RC_OK;
}

static LRESULT CALLBACK clipboardWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *clipboard = reinterpret_cast<ClipboardContext*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        auto *create = reinterpret_cast<CREATESTRUCT*>(lParam);
        clipboard = static_cast<ClipboardContext*>(create->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(clipboard));
        clipboard->hwnd = hwnd;
        if (!clipboard->legacyApi)
            clipboard->addClipboardFormatListener(hwnd);
        else
            clipboard->nextViewer = SetClipboardViewer(hwnd);
        return 0;
    }
    case WM_CLOSE:
        if (clipboard && !clipboard->legacyApi)
            clipboard->removeClipboardFormatListener(hwnd);
        return 0;
    case WM_DESTROY:
        if (clipboard && clipboard->legacyApi)
            ChangeClipboardChain(hwnd, clipboard->nextViewer);
        return 0;
    case WM_CLIPBOARDUPDATE:
        if (clipboard && clipboard->sync &&
            GetClipboardOwner() != clipboard->hwnd &&
            OleIsCurrentClipboard(clipboard->dataObject) == S_FALSE) {
            if (clipboard->responseData) {
                GlobalFree(clipboard->responseData);
                clipboard->responseData = nullptr;
            }
            sendFormatList(clipboard);
        }
        return 0;
    case WM_RENDERALLFORMATS:
        if (clipboard && tryOpenClipboard(clipboard->hwnd)) {
            EmptyClipboard();
            CloseClipboard();
        }
        return 0;
    case WM_RENDERFORMAT:
        if (clipboard && sendDataRequest(clipboard, static_cast<UINT32>(wParam)) == CHANNEL_RC_OK) {
            if (!SetClipboardData(static_cast<UINT>(wParam), clipboard->responseData) && clipboard->responseData) {
                GlobalFree(clipboard->responseData);
                clipboard->responseData = nullptr;
            }
        }
        return 0;
    case WM_DRAWCLIPBOARD:
        if (clipboard && clipboard->legacyApi) {
            if (GetClipboardOwner() != clipboard->hwnd &&
                OleIsCurrentClipboard(clipboard->dataObject) == S_FALSE) {
                sendFormatList(clipboard);
            }
            SendMessage(clipboard->nextViewer, message, wParam, lParam);
        }
        return 0;
    case WM_CHANGECBCHAIN:
        if (clipboard && clipboard->legacyApi) {
            if (reinterpret_cast<HWND>(wParam) == clipboard->nextViewer)
                clipboard->nextViewer = reinterpret_cast<HWND>(lParam);
            else if (clipboard->nextViewer)
                SendMessage(clipboard->nextViewer, message, wParam, lParam);
        }
        return 0;
    case WM_CLIPRDR_MESSAGE:
        if (clipboard && wParam == OLE_SETCLIPBOARD) {
            destroyFileDataObject(clipboard->dataObject);
            clipboard->dataObject = nullptr;
            if (createFileDataObject(clipboard, &clipboard->dataObject))
                OleSetClipboard(clipboard->dataObject);
        }
        return 0;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

static int createClipboardWindow(ClipboardContext *clipboard)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = clipboardWindowProc;
    windowClass.hInstance = GetModuleHandle(nullptr);
    windowClass.lpszClassName = L"RdpBoxClipboardHiddenWindow";
    RegisterClassExW(&windowClass);

    clipboard->hwnd = CreateWindowExW(WS_EX_LEFT, windowClass.lpszClassName, L"rdpclip", 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), clipboard);
    return clipboard->hwnd ? 0 : -1;
}

static DWORD WINAPI clipboardThreadProc(LPVOID argument)
{
    auto *clipboard = static_cast<ClipboardContext*>(argument);
    OleInitialize(nullptr);

    if (createClipboardWindow(clipboard) != 0) {
        OleUninitialize();
        return 0;
    }

    MSG message = {};
    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    OleUninitialize();
    return 0;
}
}

struct WindowsClipboardBackend::Private
{
    std::unique_ptr<ClipboardContext> clipboard;
};

WindowsClipboardBackend::WindowsClipboardBackend()
    : m_d(std::make_unique<Private>())
{
}

WindowsClipboardBackend::~WindowsClipboardBackend()
{
    detach();
}

bool WindowsClipboardBackend::attach(CliprdrClientContext *context)
{
    detach();

    if (!context)
        return false;

    m_d->clipboard = std::make_unique<ClipboardContext>();
    auto &clipboard = m_d->clipboard;
    clipboard->context = context;
    clipboard->mapCapacity = 32;
    clipboard->formatMappings = static_cast<FormatMapping*>(calloc(clipboard->mapCapacity, sizeof(FormatMapping)));
    if (!clipboard->formatMappings) {
        detach();
        return false;
    }

    clipboard->user32 = LoadLibraryA("user32.dll");
    if (clipboard->user32) {
        clipboard->addClipboardFormatListener =
            GetProcAddressAs(clipboard->user32, "AddClipboardFormatListener", fnAddClipboardFormatListener);
        clipboard->removeClipboardFormatListener =
            GetProcAddressAs(clipboard->user32, "RemoveClipboardFormatListener", fnRemoveClipboardFormatListener);
        clipboard->getUpdatedClipboardFormats =
            GetProcAddressAs(clipboard->user32, "GetUpdatedClipboardFormats", fnGetUpdatedClipboardFormats);
    }

    if (!(clipboard->user32 && clipboard->addClipboardFormatListener &&
          clipboard->removeClipboardFormatListener && clipboard->getUpdatedClipboardFormats)) {
        clipboard->legacyApi = true;
    }

    clipboard->responseDataEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    clipboard->requestFileEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    clipboard->abortEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!clipboard->responseDataEvent || !clipboard->requestFileEvent || !clipboard->abortEvent) {
        detach();
        return false;
    }

    clipboard->thread = CreateThread(nullptr, 0, clipboardThreadProc, clipboard.get(), 0, nullptr);
    if (!clipboard->thread) {
        detach();
        return false;
    }

    context->MonitorReady = onMonitorReady;
    context->ServerCapabilities = onServerCapabilities;
    context->ServerFormatList = onServerFormatList;
    context->ServerFormatListResponse = onServerFormatListResponse;
    context->ServerLockClipboardData = onServerLockClipboardData;
    context->ServerUnlockClipboardData = onServerUnlockClipboardData;
    context->ServerFormatDataRequest = onServerFormatDataRequest;
    context->ServerFormatDataResponse = onServerFormatDataResponse;
    context->ServerFileContentsRequest = onServerFileContentsRequest;
    context->ServerFileContentsResponse = onServerFileContentsResponse;
    context->custom = clipboard.get();

    return true;
}

void WindowsClipboardBackend::detach()
{
    if (!m_d->clipboard)
        return;

    auto &clipboard = m_d->clipboard;
    if (clipboard->context)
        clipboard->context->custom = nullptr;

    if (clipboard->abortEvent)
        SetEvent(clipboard->abortEvent);

    if (clipboard->hwnd)
        PostMessage(clipboard->hwnd, WM_QUIT, 0, 0);

    if (clipboard->thread) {
        WaitForSingleObject(clipboard->thread, INFINITE);
        CloseHandle(clipboard->thread);
    }

    if (clipboard->responseDataEvent)
        CloseHandle(clipboard->responseDataEvent);
    if (clipboard->requestFileEvent)
        CloseHandle(clipboard->requestFileEvent);
    if (clipboard->abortEvent)
        CloseHandle(clipboard->abortEvent);
    if (clipboard->responseData)
        GlobalFree(clipboard->responseData);
    if (clipboard->requestFileData)
        free(clipboard->requestFileData);
    if (clipboard->dataObject)
        destroyFileDataObject(clipboard->dataObject);

    clearFileArray(clipboard.get());
    clearFormatMap(clipboard.get());
    free(clipboard->formatMappings);

    if (clipboard->user32)
        FreeLibrary(clipboard->user32);

    m_d->clipboard.reset();
}
