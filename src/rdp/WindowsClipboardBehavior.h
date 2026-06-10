#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace rdp::clipboard
{
constexpr std::size_t kMaxClipboardFileCount = 10000;
constexpr int kMaxClipboardDirectoryDepth = 32;

bool shouldSyncClipboard(bool syncEnabled, bool clipboardOwnedByWindow, bool oleCurrentClipboard);

std::uint32_t fileContentsRequestBufferSize(bool sizeRequest, std::uint32_t requestedSize);

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
