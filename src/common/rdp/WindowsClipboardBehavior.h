#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace rdp::clipboard
{
constexpr std::size_t kMaxClipboardFileCount = 10000;
constexpr int kMaxClipboardDirectoryDepth = 32;

bool shouldSyncClipboard(bool syncEnabled, bool clipboardOwnedByWindow, bool oleCurrentClipboard);

bool shouldAdvertiseClipboardFormat(std::uint32_t formatId,
                                    bool unicodeTextAvailable,
                                    std::uint32_t htmlFormatId);

bool shouldAcceptRemoteClipboardFormat(std::uint32_t localFormatId,
                                       bool unicodeTextAdvertised,
                                       std::uint32_t htmlFormatId);

std::uint32_t fileContentsRequestBufferSize(bool sizeRequest, std::uint32_t requestedSize);

// A FILEDESCRIPTORW attributes value identifies a directory entry (and not a
// failed GetFileAttributes query, which returns INVALID_FILE_ATTRIBUTES with
// every bit set). Directory entries have no byte stream: file-contents
// requests for them must fail instead of answering empty data.
bool isDirectoryDescriptorEntry(std::uint32_t fileAttributes);

std::uint32_t boundedFileContentsResponseSize(bool responseOk,
                                              std::uint32_t requestedSize,
                                              std::uint32_t dataLength);

bool canAddClipboardFile(std::size_t fileCount);

bool canTraverseClipboardDirectoryDepth(int depth);

std::size_t nextClipboardFileArrayCapacity(std::size_t currentCapacity);

bool shouldSkipClipboardDirectoryEntry(std::wstring_view name,
                                       bool directory,
                                       bool reparsePoint);

std::size_t clipboardPathPrefixLength(std::wstring_view fullPath);

std::size_t fileGroupDescriptorByteSize(std::size_t fileCount, std::size_t fileDescriptorSize);
}
