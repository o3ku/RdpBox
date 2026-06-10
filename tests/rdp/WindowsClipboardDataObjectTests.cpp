#include "rdp/WindowsClipboardBackendInternal.h"

#include <cassert>
#include <cstring>
#include <string_view>

namespace
{
constexpr std::string_view kRemoteFileData = "hello";

ClipboardContext *g_clipboardForStubs = nullptr;
UINT32 g_lastDataRequestFormat = 0;
ULONG g_lastFileRequestIndex = 0;
UINT32 g_lastFileRequestFlags = 0;
UINT64 g_lastFileRequestPosition = 0;
ULONG g_lastFileRequestSize = 0;

HGLOBAL createDescriptorGroup()
{
    const size_t size = sizeof(FILEGROUPDESCRIPTORW);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        return nullptr;

    auto *group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(memory));
    if (!group) {
        GlobalFree(memory);
        return nullptr;
    }

    ZeroMemory(group, size);
    group->cItems = 1;
    group->fgd[0].dwFlags = FD_ATTRIBUTES | FD_FILESIZE;
    group->fgd[0].dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    group->fgd[0].nFileSizeLow = static_cast<DWORD>(kRemoteFileData.size());
    group->fgd[0].nFileSizeHigh = 0;
    wcscpy_s(group->fgd[0].cFileName, L"hello.txt");
    GlobalUnlock(memory);
    return memory;
}

void clearRequestFileData(ClipboardContext *clipboard)
{
    if (!clipboard || !clipboard->requestFileData)
        return;

    free(clipboard->requestFileData);
    clipboard->requestFileData = nullptr;
    clipboard->requestFileSize = 0;
}
}

UINT32 clipboardGetRemoteFormatId(ClipboardContext *, UINT32 localFormat)
{
    return localFormat + 1000;
}

UINT clipboardSendDataRequest(ClipboardContext *clipboard, UINT32 formatId)
{
    g_lastDataRequestFormat = formatId;
    if (!clipboard)
        return ERROR_INTERNAL_ERROR;

    if (clipboard->responseData)
        GlobalFree(clipboard->responseData);
    clipboard->responseData = createDescriptorGroup();
    return clipboard->responseData ? CHANNEL_RC_OK : ERROR_INTERNAL_ERROR;
}

UINT clipboardSendFileContentsRequest(ClipboardContext *clipboard, const void *, ULONG index,
                                      UINT32 flags, UINT64 position, ULONG requestSize)
{
    g_lastFileRequestIndex = index;
    g_lastFileRequestFlags = flags;
    g_lastFileRequestPosition = position;
    g_lastFileRequestSize = requestSize;
    clearRequestFileData(clipboard);

    if (!clipboard || index != 0)
        return ERROR_INTERNAL_ERROR;

    const size_t available = position < kRemoteFileData.size()
        ? kRemoteFileData.size() - static_cast<size_t>(position)
        : 0;
    const size_t actual = std::min<size_t>(available, requestSize);
    clipboard->requestFileData = static_cast<char*>(malloc(actual ? actual : 1));
    if (!clipboard->requestFileData)
        return ERROR_INTERNAL_ERROR;

    if (actual > 0)
        std::memcpy(clipboard->requestFileData, kRemoteFileData.data() + position, actual);
    clipboard->requestFileSize = static_cast<ULONG>(actual);
    return CHANNEL_RC_OK;
}

int main()
{
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    assert(SUCCEEDED(init) || init == RPC_E_CHANGED_MODE);

    ClipboardContext clipboard;
    g_clipboardForStubs = &clipboard;

    IDataObject *dataObject = nullptr;
    assert(createFileDataObject(&clipboard, &dataObject));
    assert(dataObject != nullptr);

    const UINT fileDescriptorFormat = RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
    const UINT fileContentsFormat = RegisterClipboardFormat(CFSTR_FILECONTENTS);

    FORMATETC descriptorFormat = {};
    descriptorFormat.cfFormat = static_cast<CLIPFORMAT>(fileDescriptorFormat);
    descriptorFormat.dwAspect = DVASPECT_CONTENT;
    descriptorFormat.tymed = TYMED_HGLOBAL;
    descriptorFormat.lindex = -1;

    FORMATETC contentsFormat = {};
    contentsFormat.cfFormat = static_cast<CLIPFORMAT>(fileContentsFormat);
    contentsFormat.dwAspect = DVASPECT_CONTENT;
    contentsFormat.tymed = TYMED_ISTREAM;
    contentsFormat.lindex = 0;

    FORMATETC unsupportedFormat = descriptorFormat;
    unsupportedFormat.cfFormat = CF_UNICODETEXT;

    assert(IDataObject_QueryGetData(dataObject, &descriptorFormat) == S_OK);
    assert(IDataObject_QueryGetData(dataObject, &contentsFormat) == S_OK);
    assert(IDataObject_QueryGetData(dataObject, &unsupportedFormat) == DV_E_FORMATETC);

    IEnumFORMATETC *enumerator = nullptr;
    assert(IDataObject_EnumFormatEtc(dataObject, DATADIR_GET, &enumerator) == S_OK);
    FORMATETC enumerated[2] = {};
    ULONG fetched = 0;
    assert(IEnumFORMATETC_Next(enumerator, 2, enumerated, &fetched) == S_OK);
    assert(fetched == 2);
    assert(enumerated[0].cfFormat == fileDescriptorFormat);
    assert(enumerated[0].tymed == TYMED_HGLOBAL);
    assert(enumerated[1].cfFormat == fileContentsFormat);
    assert(enumerated[1].tymed == TYMED_ISTREAM);
    IEnumFORMATETC_Release(enumerator);

    STGMEDIUM descriptorMedium = {};
    assert(IDataObject_GetData(dataObject, &descriptorFormat, &descriptorMedium) == S_OK);
    assert(descriptorMedium.tymed == TYMED_HGLOBAL);
    assert(g_lastDataRequestFormat == fileDescriptorFormat + 1000);

    auto *group = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(STGMEDIUM_HGLOBAL(descriptorMedium)));
    assert(group != nullptr);
    assert(group->cItems == 1);
    assert(group->fgd[0].nFileSizeLow == kRemoteFileData.size());
    assert(wcscmp(group->fgd[0].cFileName, L"hello.txt") == 0);
    GlobalUnlock(STGMEDIUM_HGLOBAL(descriptorMedium));

    STGMEDIUM contentsMedium = {};
    assert(IDataObject_GetData(dataObject, &contentsFormat, &contentsMedium) == S_OK);
    assert(contentsMedium.tymed == TYMED_ISTREAM);
    assert(STGMEDIUM_PSTM(contentsMedium) != nullptr);

    STATSTG stat = {};
    assert(IStream_Stat(STGMEDIUM_PSTM(contentsMedium), &stat, STATFLAG_NONAME) == S_OK);
    assert(stat.type == STGTY_STREAM);
    assert(stat.cbSize.QuadPart == kRemoteFileData.size());

    char buffer[8] = {};
    ULONG bytesRead = 0;
    assert(IStream_Read(STGMEDIUM_PSTM(contentsMedium), buffer, 2, &bytesRead) == S_OK);
    assert(bytesRead == 2);
    assert(std::string_view(buffer, 2) == "he");
    assert(g_lastFileRequestIndex == 0);
    assert(g_lastFileRequestFlags == FILECONTENTS_RANGE);
    assert(g_lastFileRequestPosition == 0);
    assert(g_lastFileRequestSize == 2);

    assert(IStream_Read(STGMEDIUM_PSTM(contentsMedium), buffer, 8, &bytesRead) == S_FALSE);
    assert(bytesRead == 3);
    assert(std::string_view(buffer, 3) == "llo");
    assert(g_lastFileRequestPosition == 2);

    LARGE_INTEGER move = {};
    move.QuadPart = 1;
    ULARGE_INTEGER newPosition = {};
    assert(IStream_Seek(STGMEDIUM_PSTM(contentsMedium), move, STREAM_SEEK_SET, &newPosition) == S_OK);
    assert(newPosition.QuadPart == 1);

    IStream_Release(STGMEDIUM_PSTM(contentsMedium));
    destroyFileDataObject(dataObject);
    if (clipboard.responseData)
        GlobalFree(clipboard.responseData);
    clearRequestFileData(&clipboard);

    if (init == S_OK || init == S_FALSE)
        CoUninitialize();
    g_clipboardForStubs = nullptr;
    return 0;
}
