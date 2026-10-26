#include "WindowsClipboardBackendInternal.h"
#include "WindowsClipboardBehavior.h"

#include <shlobj.h>
#include <strsafe.h>

void clipboardClearFileArray(ClipboardContext *clipboard)
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

BOOL clipboardGetFileContents(WCHAR *fileName, BYTE *buffer, LONG positionLow, LONG positionHigh,
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

FILEDESCRIPTORW *createFileDescriptor(WCHAR *fileName, size_t pathLength)
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
    // pathLength points at the leaf name and should stay inside the full path; guard it and fall back to an empty name.
    const size_t fullLength = wcslen(fileName);
    const WCHAR *leafName = pathLength < fullLength ? fileName + pathLength : L"";
    if (FAILED(StringCchCopyW(descriptor->cFileName, ARRAYSIZE(descriptor->cFileName), leafName))) {
        free(descriptor);
        CloseHandle(file);
        return nullptr;
    }
    CloseHandle(file);
    return descriptor;
}

BOOL ensureFileArrayCapacity(ClipboardContext *clipboard)
{
    if (!clipboard)
        return FALSE;
    if (clipboard->fileCount != clipboard->fileArraySize)
        return TRUE;

    const size_t newSize = rdp::clipboard::nextClipboardFileArrayCapacity(clipboard->fileArraySize);
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

BOOL addToFileArrays(ClipboardContext *clipboard, WCHAR *fullFileName, size_t pathLength)
{
    if (!clipboard || !rdp::clipboard::canAddClipboardFile(clipboard->fileCount))
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

BOOL traverseDirectory(ClipboardContext *clipboard, WCHAR *directory, size_t pathLength, int depth)
{
    if (!clipboard || !directory)
        return FALSE;
    if (!rdp::clipboard::canTraverseClipboardDirectoryDepth(depth))
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
        const bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool isReparsePoint = (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (rdp::clipboard::shouldSkipClipboardDirectoryEntry(findData.cFileName,
                                                              isDirectory,
                                                              isReparsePoint))
            continue;

        WCHAR path[MAX_PATH] = {};
        StringCchCopyW(path, ARRAYSIZE(path), directory);
        StringCchCatW(path, ARRAYSIZE(path), L"\\");
        StringCchCatW(path, ARRAYSIZE(path), findData.cFileName);

        if (!addToFileArrays(clipboard, path, pathLength)) {
            success = FALSE;
            break;
        }

        if (isDirectory) {
            if (!traverseDirectory(clipboard, path, pathLength, depth + 1)) {
                success = FALSE;
                break;
            }
        }
    } while (FindNextFileW(find, &findData));

    FindClose(find);
    return success;
}

BOOL processClipboardFilename(ClipboardContext *clipboard, WCHAR *fileName, size_t length)
{
    if (!clipboard || !fileName)
        return FALSE;

    const size_t pathLength =
        rdp::clipboard::clipboardPathPrefixLength(std::wstring_view(fileName, length));
    if (!addToFileArrays(clipboard, fileName, pathLength))
        return FALSE;

    const DWORD attributes = clipboard->fileDescriptors[clipboard->fileCount - 1]->dwFileAttributes;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        return traverseDirectory(clipboard, fileName, pathLength, 0);
    }

    return TRUE;
}

SSIZE_T clipboardBuildFileDescriptorList(ClipboardContext *clipboard, BYTE **data)
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

    clipboardClearFileArray(clipboard);

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

    const size_t size =
        rdp::clipboard::fileGroupDescriptorByteSize(clipboard->fileCount, sizeof(FILEDESCRIPTORW));
    if (size == 0) {
        IDataObject_Release(dataObject);
        return -1;
    }
    auto *group = static_cast<FILEGROUPDESCRIPTORW*>(calloc(size, 1));
    if (!group) {
        IDataObject_Release(dataObject);
        return -1;
    }

    group->cItems = static_cast<UINT>(clipboard->fileCount);
    for (size_t i = 0; i < clipboard->fileCount; ++i) {
        // addToFileArrays increments fileCount only after a descriptor is built, so fileCount is the
        // number of usable descriptors and no entry can be null here. Keep this an assertion: a null
        // entry would mean the bookkeeping above is broken, not something to skip at runtime.
        WINPR_ASSERT(clipboard->fileDescriptors[i]);
        group->fgd[i] = *clipboard->fileDescriptors[i];
    }

    *data = reinterpret_cast<BYTE*>(group);
    IDataObject_Release(dataObject);
    return static_cast<SSIZE_T>(size);
}
