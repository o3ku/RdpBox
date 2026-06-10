#include "MainWindowUpdateBehavior.h"

#include <algorithm>

namespace ui
{
bool shouldShowUpdateButton(UpdateUiState state)
{
    return state != UpdateUiState::Hidden;
}

std::wstring updateReleaseFileName(const std::wstring &tagName)
{
    if (tagName.empty())
        return L"RdpBox.exe";
    return L"RdpBox-" + tagName + L".exe";
}

std::wstring updateTooltipText(UpdateUiState state,
                               const std::wstring &tagName,
                               int downloadProgress)
{
    switch (state) {
    case UpdateUiState::Available:
        if (!tagName.empty())
            return L"New version " + tagName + L" available. Click to download.";
        return L"New version available. Click to download.";
    case UpdateUiState::Downloading:
        if (downloadProgress >= 0)
            return L"Downloading update... " + std::to_wstring(downloadProgress) + L"%";
        return L"Downloading update...";
    case UpdateUiState::Downloaded:
        if (!tagName.empty())
            return L"Update " + tagName + L" downloaded. Click to launch.";
        return L"Update downloaded. Click to launch.";
    case UpdateUiState::Hidden:
    default:
        return {};
    }
}

std::wstring updateButtonText(UpdateUiState state, int downloadProgress)
{
    if (state != UpdateUiState::Downloading)
        return {};

    if (downloadProgress >= 0)
        return std::to_wstring(downloadProgress) + L"%";
    return L"...";
}

std::wstring downloadedUpdatePrompt(const std::wstring &tagName)
{
    if (tagName.empty())
        return L"Update downloaded. Launch new version now?";

    return L"Update " + tagName + L" downloaded. Launch now?";
}

int updateDownloadProgressPercent(std::uint64_t bytesReceived, std::uint64_t totalBytes)
{
    if (totalBytes == 0)
        return -1;

    const auto percent = static_cast<int>((bytesReceived * 100) / totalBytes);
    return std::clamp(percent, 0, 100);
}
}
