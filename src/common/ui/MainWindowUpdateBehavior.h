#pragma once

#include <cstdint>
#include <string>

namespace ui
{
enum class UpdateUiState
{
    Hidden,
    Available,
    Downloading,
    Downloaded,
};

bool shouldShowUpdateButton(UpdateUiState state);

std::wstring updateReleaseFileName(const std::wstring &tagName);

std::wstring updateTooltipText(UpdateUiState state,
                               const std::wstring &tagName,
                               int downloadProgress);

std::wstring updateButtonText(UpdateUiState state, int downloadProgress);

std::wstring downloadedUpdatePrompt(const std::wstring &tagName);

int updateDownloadProgressPercent(std::uint64_t bytesReceived, std::uint64_t totalBytes);
}
