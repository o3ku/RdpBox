#include "WindowsClipboardBackend.h"
#include "WindowsClipboardBackendInternal.h"

#include <winpr/assert.h>
#include <winpr/library.h>
#include <winpr/string.h>

#include <shlobj.h>
#include <strsafe.h>
#include <winuser.h>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace
{
constexpr DWORD kClipboardRequestTimeoutMs = 30000;
constexpr size_t kMaxClipboardFileCount = 10000;
constexpr int kMaxClipboardDirectoryDepth = 32;

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

UINT clipboardSendFormatList(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFormatList)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FORMAT_LIST formatList = {};
    CLIPRDR_FORMAT *formats = nullptr;
    UINT32 numFormats = 0;

    if (clipboardTryOpenClipboard(clipboard->hwnd)) {
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

UINT clipboardSendDataRequest(ClipboardContext *clipboard, UINT32 formatId)
{
    if (!clipboard || !clipboard->context || !clipboard->context->ClientFormatDataRequest)
        return ERROR_INTERNAL_ERROR;

    CLIPRDR_FORMAT_DATA_REQUEST request = {};
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

    const size_t newSize = (clipboard->fileArraySize == 0) ? 16 : (clipboard->fileArraySize * 2);
    auto *newDescriptors = static_cast<FILEDESCRIPTORW**>(calloc(newSize, sizeof(FILEDESCRIPTORW*)));
    auto *newNames = static_cast<WCHAR**>(calloc(newSize, sizeof(WCHAR*)));

    if (!newDescriptors || !newNames)
        return FALSE;

    if (clipboard->fileDescriptors)
        CopyMemory(newDescriptors, clipboard->fileDescriptors, clipboard->fileCount * sizeof(FILEDESCRIPTORW*));
    if (clipboard->fileNames)
        CopyMemory(newNames, clipboard->fileNames, clipboard->fileCount * sizeof(WCHAR*));

    free(clipboard->fileDescriptors);
    free(clipboard->fileNames);

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

    const size_t fileNameLength = wcslen(fullFileName) + 1;
    clipboard->fileNames[clipboard->fileCount] = static_cast<WCHAR*>(malloc(fileNameLength * sizeof(WCHAR)));
    if (!clipboard->fileNames[clipboard->fileCount])
        return FALSE;
    wcscpy_s(clipboard->fileNames[clipboard->fileCount], fileNameLength, fullFileName);

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
        return PostMessage(clipboard->hwnd, WM_USER + 156, 1, 0)
            ? CHANNEL_RC_OK
            : ERROR_INTERNAL_ERROR;
    }

    if (!clipboardTryOpenClipboard(clipboard->hwnd))
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
