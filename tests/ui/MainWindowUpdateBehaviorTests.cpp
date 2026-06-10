#include <cassert>

#include "ui/MainWindowUpdateBehavior.h"

int main()
{
    using ui::UpdateUiState;

    assert(!ui::shouldShowUpdateButton(UpdateUiState::Hidden));
    assert(ui::shouldShowUpdateButton(UpdateUiState::Available));
    assert(ui::shouldShowUpdateButton(UpdateUiState::Downloading));
    assert(ui::shouldShowUpdateButton(UpdateUiState::Downloaded));

    assert(ui::updateReleaseFileName({}) == L"RdpBox.exe");
    assert(ui::updateReleaseFileName(L"v1.12.0") == L"RdpBox-v1.12.0.exe");

    assert(ui::updateTooltipText(UpdateUiState::Hidden, L"v1.12.0", -1).empty());
    assert(ui::updateTooltipText(UpdateUiState::Available, L"", -1)
           == L"New version available. Click to download.");
    assert(ui::updateTooltipText(UpdateUiState::Available, L"v1.12.0", -1)
           == L"New version v1.12.0 available. Click to download.");
    assert(ui::updateTooltipText(UpdateUiState::Downloading, L"v1.12.0", -1)
           == L"Downloading update...");
    assert(ui::updateTooltipText(UpdateUiState::Downloading, L"v1.12.0", 42)
           == L"Downloading update... 42%");
    assert(ui::updateTooltipText(UpdateUiState::Downloaded, L"", 100)
           == L"Update downloaded. Click to launch.");
    assert(ui::updateTooltipText(UpdateUiState::Downloaded, L"v1.12.0", 100)
           == L"Update v1.12.0 downloaded. Click to launch.");

    assert(ui::updateButtonText(UpdateUiState::Hidden, 10).empty());
    assert(ui::updateButtonText(UpdateUiState::Available, 10).empty());
    assert(ui::updateButtonText(UpdateUiState::Downloaded, 100).empty());
    assert(ui::updateButtonText(UpdateUiState::Downloading, -1) == L"...");
    assert(ui::updateButtonText(UpdateUiState::Downloading, 42) == L"42%");

    assert(ui::downloadedUpdatePrompt({}) == L"Update downloaded. Launch new version now?");
    assert(ui::downloadedUpdatePrompt(L"v1.12.0") == L"Update v1.12.0 downloaded. Launch now?");

    assert(ui::updateDownloadProgressPercent(0, 0) == -1);
    assert(ui::updateDownloadProgressPercent(0, 100) == 0);
    assert(ui::updateDownloadProgressPercent(42, 100) == 42);
    assert(ui::updateDownloadProgressPercent(100, 100) == 100);
    assert(ui::updateDownloadProgressPercent(120, 100) == 100);

    return 0;
}
