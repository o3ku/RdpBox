#include "WindowsClipboardBehavior.h"

#include <algorithm>
#include <limits>

#include <windows.h>

namespace rdp::clipboard
{
bool shouldSyncClipboard(bool syncEnabled, bool clipboardOwnedByWindow, bool oleCurrentClipboard)
{
    return syncEnabled && !clipboardOwnedByWindow && !oleCurrentClipboard;
}

bool shouldAdvertiseClipboardFormat(std::uint32_t formatId,
                                    bool unicodeTextAvailable,
                                    std::uint32_t htmlFormatId)
{
    if (!unicodeTextAvailable)
        return true;

    if (formatId == CF_TEXT || formatId == CF_OEMTEXT)
        return false;

    return htmlFormatId == 0 || formatId != htmlFormatId;
}

bool shouldAcceptRemoteClipboardFormat(std::uint32_t localFormatId,
                                       bool unicodeTextAdvertised,
                                       std::uint32_t htmlFormatId)
{
    return shouldAdvertiseClipboardFormat(localFormatId, unicodeTextAdvertised, htmlFormatId);
}

std::uint32_t fileContentsRequestBufferSize(bool sizeRequest, std::uint32_t requestedSize)
{
    return sizeRequest ? sizeof(std::uint64_t) : requestedSize;
}

std::uint32_t boundedFileContentsResponseSize(bool responseOk,
                                              std::uint32_t requestedSize,
                                              std::uint32_t dataLength)
{
    if (!responseOk)
        return 0;

    return std::min(requestedSize, dataLength);
}

bool canAddClipboardFile(std::size_t fileCount)
{
    return fileCount < kMaxClipboardFileCount;
}

bool canTraverseClipboardDirectoryDepth(int depth)
{
    return depth < kMaxClipboardDirectoryDepth;
}

std::size_t nextClipboardFileArrayCapacity(std::size_t currentCapacity)
{
    return currentCapacity == 0 ? 16 : currentCapacity * 2;
}

bool shouldSkipClipboardDirectoryEntry(std::wstring_view name,
                                       bool directory,
                                       bool reparsePoint)
{
    if (directory && name == L".")
        return true;
    if (name == L"..")
        return true;
    return reparsePoint;
}

std::size_t clipboardPathPrefixLength(std::wstring_view fullPath)
{
    std::size_t offset = fullPath.size();
    while (offset > 0) {
        if (fullPath[offset - 1] == L'\\')
            break;
        --offset;
    }
    return offset == 0 ? 1 : offset;
}

std::size_t fileGroupDescriptorByteSize(std::size_t fileCount, std::size_t fileDescriptorSize)
{
    if (fileDescriptorSize == 0)
        return 0;

    if (fileCount > (std::numeric_limits<std::size_t>::max() - 4u) / fileDescriptorSize)
        return 0;

    return 4ull + fileCount * fileDescriptorSize;
}
}
