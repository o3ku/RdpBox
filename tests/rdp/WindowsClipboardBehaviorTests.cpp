#include <cassert>
#include <cstdint>

#include <windows.h>

#include "rdp/WindowsClipboardBehavior.h"

int main()
{
    assert(rdp::clipboard::shouldSyncClipboard(true, false, false));
    assert(!rdp::clipboard::shouldSyncClipboard(false, false, false));
    assert(!rdp::clipboard::shouldSyncClipboard(true, true, false));
    assert(!rdp::clipboard::shouldSyncClipboard(true, false, true));

    constexpr std::uint32_t htmlFormat = 49152;
    assert(rdp::clipboard::shouldAdvertiseClipboardFormat(CF_UNICODETEXT, true, htmlFormat));
    assert(!rdp::clipboard::shouldAdvertiseClipboardFormat(CF_TEXT, true, htmlFormat));
    assert(!rdp::clipboard::shouldAdvertiseClipboardFormat(CF_OEMTEXT, true, htmlFormat));
    assert(!rdp::clipboard::shouldAdvertiseClipboardFormat(htmlFormat, true, htmlFormat));
    assert(rdp::clipboard::shouldAdvertiseClipboardFormat(CF_TEXT, false, htmlFormat));
    assert(rdp::clipboard::shouldAdvertiseClipboardFormat(htmlFormat, false, htmlFormat));

    assert(rdp::clipboard::shouldAcceptRemoteClipboardFormat(CF_UNICODETEXT, true, htmlFormat));
    assert(!rdp::clipboard::shouldAcceptRemoteClipboardFormat(CF_TEXT, true, htmlFormat));
    assert(!rdp::clipboard::shouldAcceptRemoteClipboardFormat(CF_OEMTEXT, true, htmlFormat));
    assert(!rdp::clipboard::shouldAcceptRemoteClipboardFormat(htmlFormat, true, htmlFormat));
    assert(rdp::clipboard::shouldAcceptRemoteClipboardFormat(CF_TEXT, false, htmlFormat));

    assert(rdp::clipboard::fileContentsRequestBufferSize(true, 1234) == sizeof(std::uint64_t));
    assert(rdp::clipboard::fileContentsRequestBufferSize(false, 1234) == 1234);
    assert(rdp::clipboard::fileContentsRequestBufferSize(false, 0) == 0);

    assert(rdp::clipboard::boundedFileContentsResponseSize(false, 10, 20) == 0);
    assert(rdp::clipboard::boundedFileContentsResponseSize(true, 10, 20) == 10);
    assert(rdp::clipboard::boundedFileContentsResponseSize(true, 20, 10) == 10);
    assert(rdp::clipboard::boundedFileContentsResponseSize(true, 0, 10) == 0);

    assert(rdp::clipboard::canAddClipboardFile(0));
    assert(rdp::clipboard::canAddClipboardFile(rdp::clipboard::kMaxClipboardFileCount - 1));
    assert(!rdp::clipboard::canAddClipboardFile(rdp::clipboard::kMaxClipboardFileCount));

    assert(rdp::clipboard::canTraverseClipboardDirectoryDepth(0));
    assert(rdp::clipboard::canTraverseClipboardDirectoryDepth(rdp::clipboard::kMaxClipboardDirectoryDepth - 1));
    assert(!rdp::clipboard::canTraverseClipboardDirectoryDepth(rdp::clipboard::kMaxClipboardDirectoryDepth));

    assert(rdp::clipboard::nextClipboardFileArrayCapacity(0) == 16);
    assert(rdp::clipboard::nextClipboardFileArrayCapacity(16) == 32);

    assert(rdp::clipboard::shouldSkipClipboardDirectoryEntry(L".", true, false));
    assert(!rdp::clipboard::shouldSkipClipboardDirectoryEntry(L".", false, false));
    assert(rdp::clipboard::shouldSkipClipboardDirectoryEntry(L"..", true, false));
    assert(rdp::clipboard::shouldSkipClipboardDirectoryEntry(L"..", false, false));
    assert(rdp::clipboard::shouldSkipClipboardDirectoryEntry(L"link", false, true));
    assert(!rdp::clipboard::shouldSkipClipboardDirectoryEntry(L"file.txt", false, false));

    assert(rdp::clipboard::clipboardPathPrefixLength(L"C:\\Temp\\file.txt") == 8);
    assert(rdp::clipboard::clipboardPathPrefixLength(L"file.txt") == 1);
    assert(rdp::clipboard::clipboardPathPrefixLength(L"") == 1);

    assert(rdp::clipboard::fileGroupDescriptorByteSize(0, 16) == 4);
    assert(rdp::clipboard::fileGroupDescriptorByteSize(3, 16) == 52);

    return 0;
}
