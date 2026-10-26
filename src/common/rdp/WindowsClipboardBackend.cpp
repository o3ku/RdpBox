#include "WindowsClipboardBackend.h"
#include "ClipboardWindowReady.h"
#include "WindowsClipboardBackendInternal.h"
#include "WindowsClipboardBehavior.h"

#include <winpr/assert.h>
#include <winpr/library.h>
#include <winpr/string.h>

#include <winuser.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

namespace
{
constexpr DWORD kClipboardRequestTimeoutMs = 30000;

constexpr UINT32 kCliprdrGeneralCapabilityPayloadSize = sizeof(UINT32) + CB_CAPSTYPE_GENERAL_LEN;
constexpr UINT32 kCliprdrFormatDataRequestPayloadSize = sizeof(UINT32);
constexpr UINT32 kCliprdrFileContentsResponseBasePayloadSize = sizeof(UINT32) + sizeof(UINT32);

static std::vector<BYTE> readGlobalClipboardData(UINT formatId)
{
    HANDLE handle = GetClipboardData(formatId);
    if (!handle)
        return {};

    const SIZE_T size = GlobalSize(handle);
    const auto *memory = static_cast<const BYTE*>(GlobalLock(handle));
    if (!memory || size == 0) {
        if (memory)
            GlobalUnlock(handle);
        return {};
    }

    std::vector<BYTE> data(memory, memory + size);
    GlobalUnlock(handle);
    return data;
}

static bool normalizeUnicodeText(std::vector<BYTE> &data)
{
    for (size_t i = 0; i + sizeof(WCHAR) <= data.size(); i += sizeof(WCHAR)) {
        if (data[i] == 0 && data[i + 1] == 0) {
            data.resize(i + sizeof(WCHAR));
            return true;
        }
    }
    data.clear();
    return false;
}

static bool looksLikeHtmlClipboardData(const std::vector<BYTE> &data)
{
    constexpr std::string_view prefix = "Version:";
    return data.size() >= prefix.size()
        && std::memcmp(data.data(), prefix.data(), prefix.size()) == 0;
}

static std::vector<BYTE> unicodeTextFromAnsiClipboard()
{
    std::vector<BYTE> ansiData = readGlobalClipboardData(CF_TEXT);
    const auto terminator = std::find(ansiData.begin(), ansiData.end(), BYTE{0});
    if (terminator == ansiData.end())
        return {};

    const int sourceLength = static_cast<int>(terminator - ansiData.begin());
    const int wideLength = MultiByteToWideChar(CP_ACP,
                                                0,
                                                reinterpret_cast<const char*>(ansiData.data()),
                                                sourceLength,
                                                nullptr,
                                                0);
    if (sourceLength > 0 && wideLength <= 0)
        return {};

    std::vector<BYTE> result((static_cast<size_t>(wideLength) + 1) * sizeof(WCHAR), 0);
    if (wideLength > 0) {
        MultiByteToWideChar(CP_ACP,
                            0,
                            reinterpret_cast<const char*>(ansiData.data()),
                            sourceLength,
                            reinterpret_cast<WCHAR*>(result.data()),
                            wideLength);
    }
    return result;
}

static void updateCachedUnicodeText(ClipboardContext *clipboard, bool unicodeTextAvailable)
{
    std::vector<BYTE> data;
    if (unicodeTextAvailable) {
        data = readGlobalClipboardData(CF_UNICODETEXT);
        if (looksLikeHtmlClipboardData(data) || !normalizeUnicodeText(data))
            data = unicodeTextFromAnsiClipboard();
    }

    std::scoped_lock lock(clipboard->cachedUnicodeTextMutex);
    clipboard->cachedUnicodeText = std::move(data);
}

static SSIZE_T copyCachedUnicodeText(ClipboardContext *clipboard, BYTE **data)
{
    std::scoped_lock lock(clipboard->cachedUnicodeTextMutex);
    if (clipboard->cachedUnicodeText.empty())
        return -1;

    *data = static_cast<BYTE*>(malloc(clipboard->cachedUnicodeText.size()));
    if (!*data)
        return -1;

    CopyMemory(*data, clipboard->cachedUnicodeText.data(), clipboard->cachedUnicodeText.size());
    return static_cast<SSIZE_T>(clipboard->cachedUnicodeText.size());
}

static BOOL fileTransferring(ClipboardContext *clipboard)
{
    return clipboardGetLocalFormatIdByName(clipboard, CFSTR_FILEDESCRIPTORW) ? TRUE : FALSE;
}

static bool waitForClipboardWindowReady(ClipboardContext *clipboard)
{
    if (!clipboard)
        return false;

    if (clipboard->windowReady.load(std::memory_order_acquire))
        return true;

    if (!clipboard->readyEvent)
        return false;

    const DWORD waitResult = WaitForSingleObject(clipboard->readyEvent, rdp::kClipboardWindowReadyTimeoutMs);
    return rdp::didClipboardWindowInitialize(waitResult, clipboard->windowReady.load(std::memory_order_acquire));
}

}

BOOL clipboardTryOpenClipboard(HWND hwnd)
{
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(hwnd))
            return TRUE;
        Sleep(10);
    }
    return FALSE;
}

static UINT waitForClipboardResponse(ClipboardContext *clipboard, HANDLE responseEvent);

UINT32 clipboardGetRemoteFormatId(ClipboardContext *clipboard, UINT32 localFormat)
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

UINT clipboardSendFormatList(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFormatList)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FORMAT_LIST formatList = {};
    CLIPRDR_FORMAT *formats = nullptr;
    UINT32 numFormats = 0;

    if (clipboardTryOpenClipboard(clipboard->hwnd)) {
        const int count = CountClipboardFormats();
        const bool hasFileDrop = IsClipboardFormatAvailable(CF_HDROP) != FALSE;
        const bool unicodeTextAvailable = IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE;
        const UINT32 htmlFormatId = clipboardHtmlFormatId();
        updateCachedUnicodeText(clipboard, unicodeTextAvailable);
        numFormats = hasFileDrop ? 2u : static_cast<UINT32>(count);
        formats = static_cast<CLIPRDR_FORMAT*>(calloc(numFormats ? numFormats : 1, sizeof(CLIPRDR_FORMAT)));
        if (!formats) {
            CloseClipboard();
            return CHANNEL_RC_NO_MEMORY;
        }

        UINT32 index = 0;
        if (hasFileDrop) {
            formats[index++].formatId = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
            formats[index++].formatId = RegisterClipboardFormat(CFSTR_FILECONTENTS);
        } else {
            UINT32 formatId = 0;
            while ((formatId = EnumClipboardFormats(formatId)) != 0) {
                if (!rdp::clipboard::shouldAdvertiseClipboardFormat(formatId,
                                                                     unicodeTextAvailable,
                                                                     htmlFormatId)) {
                    continue;
                }
                formats[index++].formatId = formatId;
            }
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

UINT clipboardSendDataRequest(ClipboardContext *clipboard, UINT32 formatId)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFormatDataRequest)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FORMAT_DATA_REQUEST request = {};
    request.common.msgType = CB_FORMAT_DATA_REQUEST;
    request.common.dataLen = kCliprdrFormatDataRequestPayloadSize;
    request.requestedFormatId = clipboardGetRemoteFormatId(clipboard, formatId);

    if (!ResetEvent(clipboard->responseDataEvent))
        return ERROR_INTERNAL_ERROR;

    UINT rc = clipboard->context->ClientFormatDataRequest(clipboard->context, &request);
    if (rc == CHANNEL_RC_OK)
        rc = waitForClipboardResponse(clipboard, clipboard->responseDataEvent);
    return rc;
}

UINT clipboardSendFileContentsRequest(ClipboardContext *clipboard, const void *streamId, ULONG index,
                                      UINT32 flags, UINT64 position, ULONG requestSize)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFileContentsRequest)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FILE_CONTENTS_REQUEST request = {};
    request.common.msgType = CB_FILECONTENTS_REQUEST;
    request.common.dataLen = 28;
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
    response.common.msgType = CB_FILECONTENTS_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    response.common.dataLen = kCliprdrFileContentsResponseBasePayloadSize + size;
    response.streamId = streamId;
    response.cbRequested = size;
    response.requestedData = data;
    return clipboard->context->ClientFileContentsResponse(clipboard->context, &response);
}

// Directory entries have no byte stream. Failing the request lets the remote
// side handle the entry as a directory instead of writing a zero-byte file.
static UINT sendFileContentsFailure(ClipboardContext *clipboard, UINT32 streamId)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFileContentsResponse)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FILE_CONTENTS_RESPONSE response = {};
    response.common.msgType = CB_FILECONTENTS_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_FAIL;
    response.common.dataLen = kCliprdrFileContentsResponseBasePayloadSize;
    response.streamId = streamId;
    response.cbRequested = 0;
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

static SSIZE_T tryOpenClipboardData(ClipboardContext *clipboard, UINT32 requestedFormatId, BYTE **data)
{
    WINPR_ASSERT(clipboard);
    WINPR_ASSERT(data);
    *data = nullptr;

    if (!clipboardTryOpenClipboard(clipboard->hwnd))
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
    capabilities.common.msgType = CB_CLIP_CAPS;
    capabilities.common.dataLen = kCliprdrGeneralCapabilityPayloadSize;
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
    if (!waitForClipboardWindowReady(clipboard))
        return ERROR_INTERNAL_ERROR;
    clipboard->sync = true;

    UINT rc = sendClientCapabilities(clipboard);
    if (rc != CHANNEL_RC_OK)
        return rc;

    return clipboardSendFormatList(clipboard);
}

static UINT CALLBACK onServerCapabilities(CliprdrClientContext *context, const CLIPRDR_CAPABILITIES *capabilities)
{
    if (!context || !capabilities)
        return ERROR_INTERNAL_ERROR;
    return CHANNEL_RC_OK;
}

static UINT CALLBACK onServerFormatList(CliprdrClientContext *context, const CLIPRDR_FORMAT_LIST *formatList)
{
    if (!context || !formatList)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;
    if (!waitForClipboardWindowReady(clipboard))
        return ERROR_INTERNAL_ERROR;
    clipboardClearFormatMap(clipboard);

    bool unicodeTextAdvertised = false;
    const UINT32 htmlFormatId = clipboardHtmlFormatId();
    for (UINT32 i = 0; i < formatList->numFormats; ++i) {
        if (clipboardLocalFormatIdForRemoteFormat(formatList->formats[i]) == CF_UNICODETEXT) {
            unicodeTextAdvertised = true;
            break;
        }
    }

    for (UINT32 i = 0; i < formatList->numFormats; ++i) {
        const auto &format = formatList->formats[i];
        const UINT32 localFormatId = clipboardLocalFormatIdForRemoteFormat(format);
        if (!rdp::clipboard::shouldAcceptRemoteClipboardFormat(localFormatId,
                                                               unicodeTextAdvertised,
                                                               htmlFormatId)) {
            continue;
        }

        clipboardEnsureMapCapacity(clipboard);
        auto &mapping = clipboard->formatMappings[clipboard->mapSize];
        mapping.remoteFormatId = format.formatId;

        if (format.formatName) {
            mapping.name = ConvertUtf8ToWCharAlloc(format.formatName, nullptr);
            if (mapping.name)
                mapping.localFormatId = RegisterClipboardFormatW(mapping.name);
        } else {
            mapping.localFormatId = localFormatId;
        }
        ++clipboard->mapSize;
    }

    UINT rc = CHANNEL_RC_OK;
    if (fileTransferring(clipboard)) {
        rc = PostMessage(clipboard->hwnd, WM_USER + 156, 1, 0)
            ? CHANNEL_RC_OK
            : ERROR_INTERNAL_ERROR;
    } else if (clipboardTryOpenClipboard(clipboard->hwnd)) {
        rc = ERROR_INTERNAL_ERROR;
        if (EmptyClipboard()) {
            for (size_t i = 0; i < clipboard->mapSize; ++i) {
                if (clipboard->formatMappings[i].localFormatId != 0)
                    SetClipboardData(clipboard->formatMappings[i].localFormatId, nullptr);
            }
            rc = CHANNEL_RC_OK;
        }

        if (!CloseClipboard() && GetLastError())
            return ERROR_INTERNAL_ERROR;
    }

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
    SSIZE_T size = -1;
    if (request->requestedFormatId == CF_UNICODETEXT)
        size = copyCachedUnicodeText(clipboard, &data);
    if (size < 0) {
        size = (request->requestedFormatId == fileDescriptorFormat)
            ? clipboardBuildFileDescriptorList(clipboard, &data)
            : tryOpenClipboardData(clipboard, request->requestedFormatId, &data);
    }

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

// OLE path of a file-contents request: read through the explorer-provided
// IStream (CFSTR_FILECONTENTS). Returns CHANNEL_RC_OK with actualSize set, or
// ERROR_INTERNAL_ERROR so the caller can fall back to the descriptor arrays.
static UINT readFileContentsViaDataObject(const CLIPRDR_FILE_CONTENTS_REQUEST *request,
                                          UINT32 requestedSize, BYTE *data, UINT *actualSize)
{
    IDataObject *dataObject = nullptr;
    if (FAILED(OleGetClipboard(&dataObject)))
        return ERROR_INTERNAL_ERROR;

    FORMATETC format = {};
    format.cfFormat = RegisterClipboardFormat(CFSTR_FILECONTENTS);
    format.tymed = TYMED_ISTREAM;
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = request->listIndex;

    STGMEDIUM medium = {};
    const HRESULT result = IDataObject_GetData(dataObject, &format, &medium);
    IDataObject_Release(dataObject);
    if (FAILED(result) || medium.tymed != TYMED_ISTREAM || !STGMEDIUM_PSTM(medium))
        return ERROR_INTERNAL_ERROR;

    UINT rc = ERROR_INTERNAL_ERROR;
    IStream *stream = STGMEDIUM_PSTM(medium);
    if (request->dwFlags == FILECONTENTS_SIZE) {
        STATSTG stat = {};
        if (SUCCEEDED(IStream_Stat(stream, &stat, STATFLAG_NONAME))) {
            *reinterpret_cast<UINT64*>(data) = stat.cbSize.QuadPart;
            *actualSize = requestedSize;
            rc = CHANNEL_RC_OK;
        }
    } else if (request->dwFlags == FILECONTENTS_RANGE) {
        LARGE_INTEGER offset = {};
        offset.QuadPart =
            (static_cast<UINT64>(request->nPositionHigh) << 32) | request->nPositionLow;
        ULARGE_INTEGER newPosition = {};
        if (SUCCEEDED(IStream_Seek(stream, offset, STREAM_SEEK_SET, &newPosition))) {
            ULONG bytesRead = 0;
            if (SUCCEEDED(IStream_Read(stream, data, requestedSize, &bytesRead))) {
                *actualSize = bytesRead;
                rc = CHANNEL_RC_OK;
            }
        }
    }
    ReleaseStgMedium(&medium);
    return rc;
}

static UINT CALLBACK onServerFileContentsRequest(CliprdrClientContext *context,
                                                 const CLIPRDR_FILE_CONTENTS_REQUEST *request)
{
    if (!context || !request)
        return ERROR_INTERNAL_ERROR;

    auto *clipboard = static_cast<ClipboardContext*>(context->custom);
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;

    // Validate the index before it reaches FormatEtc or the fallback arrays: both index fileCount
    // sized storage, so an out of range index must fail once, up front, instead of per branch.
    const bool knownFile = request->listIndex < clipboard->fileCount;

    UINT32 requestedSize =
        rdp::clipboard::fileContentsRequestBufferSize(request->dwFlags == FILECONTENTS_SIZE,
                                                      request->cbRequested);

    BYTE *data = static_cast<BYTE*>(calloc(1, requestedSize));
    if (!data)
        return ERROR_INTERNAL_ERROR;

    if (knownFile && rdp::clipboard::isDirectoryDescriptorEntry(
                         clipboard->fileDescriptors[request->listIndex]->dwFileAttributes)) {
        const UINT failRc = sendFileContentsFailure(clipboard, request->streamId);
        free(data);
        return failRc;
    }

    UINT actualSize = 0;
    UINT rc = ERROR_INTERNAL_ERROR;

    if (knownFile)
        rc = readFileContentsViaDataObject(request, requestedSize, data, &actualSize);

    // Fallback: no explorer data object (or it declined the stream) - answer
    // from the descriptor arrays we built when the file list arrived.
    if (rc != CHANNEL_RC_OK && knownFile) {
        if (request->dwFlags == FILECONTENTS_SIZE) {
            *reinterpret_cast<UINT32*>(&data[0]) = clipboard->fileDescriptors[request->listIndex]->nFileSizeLow;
            *reinterpret_cast<UINT32*>(&data[4]) = clipboard->fileDescriptors[request->listIndex]->nFileSizeHigh;
            actualSize = requestedSize;
            rc = CHANNEL_RC_OK;
        } else if (request->dwFlags == FILECONTENTS_RANGE) {
            DWORD fileSize = 0;
            if (clipboardGetFileContents(clipboard->fileNames[request->listIndex], data,
                                static_cast<LONG>(request->nPositionLow),
                                static_cast<LONG>(request->nPositionHigh),
                                requestedSize, &fileSize)) {
                actualSize = fileSize;
                rc = CHANNEL_RC_OK;
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

    const UINT32 dataSize =
        rdp::clipboard::boundedFileContentsResponseSize(true,
                                                        response->cbRequested,
                                                        response->common.dataLen);
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
    clipboard->readyEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!clipboard->responseDataEvent || !clipboard->requestFileEvent || !clipboard->abortEvent ||
        !clipboard->readyEvent) {
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

    clipboard->thread = CreateThread(nullptr, 0, clipboardThreadProc, clipboard.get(), 0, &clipboard->threadId);
    if (!clipboard->thread) {
        detach();
        return false;
    }

    const DWORD readyResult = WaitForSingleObject(clipboard->readyEvent, rdp::kClipboardWindowReadyTimeoutMs);
    if (!rdp::didClipboardWindowInitialize(readyResult, clipboard->windowReady.load(std::memory_order_acquire))) {
        detach();
        return false;
    }

    return true;
}

void WindowsClipboardBackend::detach()
{
    if (!m_d->clipboard)
        return;

    auto &clipboard = m_d->clipboard;
    if (clipboard->context) {
        clipboard->context->MonitorReady = nullptr;
        clipboard->context->ServerCapabilities = nullptr;
        clipboard->context->ServerFormatList = nullptr;
        clipboard->context->ServerFormatListResponse = nullptr;
        clipboard->context->ServerLockClipboardData = nullptr;
        clipboard->context->ServerUnlockClipboardData = nullptr;
        clipboard->context->ServerFormatDataRequest = nullptr;
        clipboard->context->ServerFormatDataResponse = nullptr;
        clipboard->context->ServerFileContentsRequest = nullptr;
        clipboard->context->ServerFileContentsResponse = nullptr;
        clipboard->context->custom = nullptr;
    }

    if (clipboard->abortEvent)
        SetEvent(clipboard->abortEvent);

    if (clipboard->hwnd)
        PostMessage(clipboard->hwnd, WM_QUIT, 0, 0);
    else if (clipboard->threadId != 0)
        PostThreadMessage(clipboard->threadId, WM_QUIT, 0, 0);

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
    if (clipboard->readyEvent)
        CloseHandle(clipboard->readyEvent);
    if (clipboard->responseData)
        GlobalFree(clipboard->responseData);
    if (clipboard->requestFileData)
        free(clipboard->requestFileData);
    if (clipboard->dataObject)
        destroyFileDataObject(clipboard->dataObject);

    clipboardClearFileArray(clipboard.get());
    clipboardClearFormatMap(clipboard.get());
    free(clipboard->formatMappings);

    if (clipboard->user32)
        FreeLibrary(clipboard->user32);

    m_d->clipboard.reset();
}
