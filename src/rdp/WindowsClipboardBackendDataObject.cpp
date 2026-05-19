#include "WindowsClipboardBackendInternal.h"

#include <array>
#include <cstdlib>

namespace
{
void formatDeepCopy(FORMATETC *destination, const FORMATETC *source)
{
    *destination = *source;
    if (!source->ptd)
        return;

    destination->ptd = static_cast<DVTARGETDEVICE*>(CoTaskMemAlloc(sizeof(DVTARGETDEVICE)));
    if (destination->ptd)
        *destination->ptd = *source->ptd;
}

void deleteEnumFormatEtc(CliprdrEnumFORMATETC *instance)
{
    if (!instance)
        return;

    if (instance->formats) {
        for (LONG i = 0; i < instance->count; ++i) {
            if (instance->formats[i].ptd)
                CoTaskMemFree(instance->formats[i].ptd);
        }
        free(instance->formats);
    }
    free(instance);
}

HRESULT STDMETHODCALLTYPE enumFormatEtcQueryInterface(IEnumFORMATETC *self, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;

    if (InlineIsEqualGUID(riid, IID_IEnumFORMATETC) || InlineIsEqualGUID(riid, IID_IUnknown)) {
        self->AddRef();
        *object = self;
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE enumFormatEtcAddRef(IEnumFORMATETC *self)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    return instance ? InterlockedIncrement(&instance->refCount) : 0;
}

ULONG STDMETHODCALLTYPE enumFormatEtcRelease(IEnumFORMATETC *self)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance)
        return 0;

    const LONG count = InterlockedDecrement(&instance->refCount);
    if (count == 0)
        deleteEnumFormatEtc(instance);
    return (count > 0) ? static_cast<ULONG>(count) : 0;
}

HRESULT STDMETHODCALLTYPE enumFormatEtcNext(IEnumFORMATETC *self, ULONG count,
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

HRESULT STDMETHODCALLTYPE enumFormatEtcSkip(IEnumFORMATETC *self, ULONG count)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance)
        return E_INVALIDARG;
    if (instance->index + static_cast<LONG>(count) > instance->count)
        return E_FAIL;
    instance->index += static_cast<LONG>(count);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE enumFormatEtcReset(IEnumFORMATETC *self)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance)
        return E_INVALIDARG;
    instance->index = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE enumFormatEtcClone(IEnumFORMATETC *self, IEnumFORMATETC **clone);

CliprdrEnumFORMATETC *newEnumFormatEtc(ULONG count, FORMATETC *formats)
{
    auto *instance = static_cast<CliprdrEnumFORMATETC*>(calloc(1, sizeof(CliprdrEnumFORMATETC)));
    if (!instance)
        return nullptr;
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

HRESULT STDMETHODCALLTYPE enumFormatEtcClone(IEnumFORMATETC *self, IEnumFORMATETC **clone)
{
    auto *instance = reinterpret_cast<CliprdrEnumFORMATETC*>(self);
    if (!instance || !clone)
        return E_INVALIDARG;

    auto *copy = newEnumFormatEtc(static_cast<ULONG>(instance->count), instance->formats);
    if (!copy)
        return E_OUTOFMEMORY;

    copy->index = instance->index;
    *clone = copy;
    return S_OK;
}

void deleteStream(CliprdrStream *instance)
{
    if (!instance)
        return;
    free(instance);
}

HRESULT STDMETHODCALLTYPE streamQueryInterface(IStream *self, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;

    if (InlineIsEqualGUID(riid, IID_IStream) || InlineIsEqualGUID(riid, IID_IUnknown)) {
        self->AddRef();
        *object = self;
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE streamAddRef(IStream *self)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    return instance ? InterlockedIncrement(&instance->refCount) : 0;
}

ULONG STDMETHODCALLTYPE streamRelease(IStream *self)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    if (!instance)
        return 0;
    const LONG count = InterlockedDecrement(&instance->refCount);
    if (count == 0)
        deleteStream(instance);
    return (count > 0) ? static_cast<ULONG>(count) : 0;
}

HRESULT STDMETHODCALLTYPE streamRead(IStream *self, void *buffer, ULONG bytes, ULONG *bytesRead)
{
    auto *instance = reinterpret_cast<CliprdrStream*>(self);
    if (!instance || !buffer || !bytesRead)
        return E_INVALIDARG;

    auto *clipboard = instance->clipboard;
    *bytesRead = 0;

    if (instance->offset.QuadPart >= instance->size.QuadPart)
        return S_FALSE;

    const UINT rc = clipboardSendFileContentsRequest(clipboard, self, instance->listIndex, FILECONTENTS_RANGE,
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

HRESULT STDMETHODCALLTYPE streamWrite(IStream *, const void *, ULONG, ULONG *)
{
    return STG_E_ACCESSDENIED;
}

HRESULT STDMETHODCALLTYPE streamSeek(IStream *self, LARGE_INTEGER move, DWORD origin,
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

HRESULT STDMETHODCALLTYPE streamSetSize(IStream *, ULARGE_INTEGER) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE streamCopyTo(IStream *, IStream *, ULARGE_INTEGER, ULARGE_INTEGER *,
                                       ULARGE_INTEGER *) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE streamCommit(IStream *, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE streamRevert(IStream *) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE streamLockRegion(IStream *, ULARGE_INTEGER, ULARGE_INTEGER, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE streamUnlockRegion(IStream *, ULARGE_INTEGER, ULARGE_INTEGER, DWORD) { return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE streamStat(IStream *self, STATSTG *stat, DWORD flags)
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

HRESULT STDMETHODCALLTYPE streamClone(IStream *, IStream **) { return E_NOTIMPL; }

CliprdrStream *newStream(ULONG index, ClipboardContext *clipboard, const FILEDESCRIPTORW *descriptor)
{
    auto *instance = static_cast<CliprdrStream*>(calloc(1, sizeof(CliprdrStream)));
    if (!instance)
        return nullptr;
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
        if (clipboardSendFileContentsRequest(clipboard, instance, index, FILECONTENTS_SIZE, 0, 8) != CHANNEL_RC_OK) {
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

LONG lookupFormat(CliprdrDataObject *instance, FORMATETC *format)
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

void deleteDataObject(CliprdrDataObject *instance)
{
    if (!instance)
        return;

    free(instance->formats);
    free(instance->mediums);

    if (instance->streams) {
        for (ULONG i = 0; i < instance->streamCount; ++i) {
            if (instance->streams[i])
                instance->streams[i]->Release();
        }
        free(instance->streams);
    }

    free(instance);
}

HRESULT STDMETHODCALLTYPE dataObjectQueryInterface(IDataObject *self, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;

    if (InlineIsEqualGUID(riid, IID_IDataObject) || InlineIsEqualGUID(riid, IID_IUnknown)) {
        self->AddRef();
        *object = self;
        return S_OK;
    }

    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE dataObjectAddRef(IDataObject *self)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    return instance ? InterlockedIncrement(&instance->refCount) : 0;
}

ULONG STDMETHODCALLTYPE dataObjectRelease(IDataObject *self)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    if (!instance)
        return 0;

    const LONG count = InterlockedDecrement(&instance->refCount);
    if (count == 0)
        deleteDataObject(instance);
    return (count > 0) ? static_cast<ULONG>(count) : 0;
}

HRESULT STDMETHODCALLTYPE dataObjectGetData(IDataObject *self, FORMATETC *format, STGMEDIUM *medium)
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
        const UINT remoteFormat = clipboardGetRemoteFormatId(clipboard, instance->formats[index].cfFormat);
        if (clipboardSendDataRequest(clipboard, remoteFormat) != CHANNEL_RC_OK)
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
        instance->streams[format->lindex]->AddRef();
    } else {
        return E_UNEXPECTED;
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE dataObjectGetDataHere(IDataObject *, FORMATETC *, STGMEDIUM *)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE dataObjectQueryGetData(IDataObject *self, FORMATETC *format)
{
    auto *instance = reinterpret_cast<CliprdrDataObject*>(self);
    if (!instance || !format)
        return E_INVALIDARG;
    return (lookupFormat(instance, format) >= 0) ? S_OK : DV_E_FORMATETC;
}

HRESULT STDMETHODCALLTYPE dataObjectGetCanonicalFormatEtc(IDataObject *, FORMATETC *, FORMATETC *output)
{
    if (!output)
        return E_INVALIDARG;
    output->ptd = nullptr;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE dataObjectSetData(IDataObject *, FORMATETC *, STGMEDIUM *, BOOL)
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE dataObjectEnumFormatEtc(IDataObject *self, DWORD direction,
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
    *enumerator = value;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE dataObjectDAdvise(IDataObject *, FORMATETC *, DWORD, IAdviseSink *, DWORD *)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE dataObjectDUnadvise(IDataObject *, DWORD)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE dataObjectEnumDAdvise(IDataObject *, IEnumSTATDATA **)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

CliprdrDataObject *newDataObject(FORMATETC *formats, STGMEDIUM *mediums, ULONG count,
                                 ClipboardContext *clipboard)
{
    auto *instance = static_cast<CliprdrDataObject*>(calloc(1, sizeof(CliprdrDataObject)));
    if (!instance)
        return nullptr;
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
}

BOOL createFileDataObject(ClipboardContext *clipboard, IDataObject **dataObject)
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

    *dataObject = instance;
    return TRUE;
}

void destroyFileDataObject(IDataObject *instance)
{
    if (instance)
        instance->Release();
}
