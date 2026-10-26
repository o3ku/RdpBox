#include "WindowsClipboardBackendInternal.h"

#include <winpr/string.h>

UINT32 clipboardGetLocalFormatIdByName(ClipboardContext *clipboard, const TCHAR *formatName)
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

UINT32 clipboardHtmlFormatId()
{
    return RegisterClipboardFormatA("HTML Format");
}

UINT32 clipboardLocalFormatIdForRemoteFormat(const CLIPRDR_FORMAT &format)
{
    if (!format.formatName)
        return format.formatId;

    WCHAR *name = ConvertUtf8ToWCharAlloc(format.formatName, nullptr);
    if (!name)
        return 0;

    const UINT32 localFormatId = RegisterClipboardFormatW(name);
    free(name);
    return localFormatId;
}

void clipboardEnsureMapCapacity(ClipboardContext *clipboard)
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

BOOL clipboardClearFormatMap(ClipboardContext *clipboard)
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
