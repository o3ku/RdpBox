#include "MainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/UpdateClient.h"
#include "session/SessionManager.h"

#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <thread>

namespace
{
constexpr int kUpdateButtonWidth = 30;

struct UpdateCheckResult
{
    std::uint64_t generation = 0;
    bool success = false;
    std::wstring errorMessage;
    updater::ReleaseAsset release;
    bool hasUpdate = false;
};

struct UpdateDownloadResult
{
    std::uint64_t generation = 0;
    bool success = false;
    std::wstring errorMessage;
};

std::wstring releaseFileName(const updater::ReleaseAsset &release)
{
    if (release.tagName.empty())
        return L"RdpBox.exe";
    return L"RdpBox-" + release.tagName + L".exe";
}
}

bool MainWindow::shouldShowUpdateButton() const
{
    return m_updateButtonState != UpdateButtonState::Hidden;
}

CRect MainWindow::updateButtonRect() const
{
    CRect clientRect;
    const_cast<MainWindow *>(this)->GetClientRect(&clientRect);
    const int right = clientRect.right - 46 * 3;
    return CRect(right - kUpdateButtonWidth, 0, right, 34);
}

int MainWindow::captionButtonReserveWidth() const
{
    return 46 * 3 + (shouldShowUpdateButton() ? updateButtonRect().Width() : 0);
}

void MainWindow::invalidateUpdateButton()
{
    if (GetSafeHwnd())
        InvalidateRect(updateButtonRect(), FALSE);
}

CString MainWindow::updateTooltipText() const
{
    CString text;
    switch (m_updateButtonState) {
    case UpdateButtonState::Available:
        if (!m_updateRelease.tagName.empty())
            text.Format(L"New version %s available. Click to download.", m_updateRelease.tagName.c_str());
        else
            text = L"New version available. Click to download.";
        break;
    case UpdateButtonState::Downloading:
        text = L"Downloading update...";
        break;
    case UpdateButtonState::Downloaded:
        if (!m_updateRelease.tagName.empty())
            text.Format(L"Update %s downloaded. Click to launch.", m_updateRelease.tagName.c_str());
        else
            text = L"Update downloaded. Click to launch.";
        break;
    case UpdateButtonState::Hidden:
    default:
        text.Empty();
        break;
    }
    return text;
}

void MainWindow::updateCaptionTooltip()
{
    if (!m_captionTooltip.GetSafeHwnd())
        return;

    m_captionTooltipText = updateTooltipText();
    m_captionTooltip.UpdateTipText(m_captionTooltipText, this);
}

std::wstring MainWindow::downloadedUpdatePath() const
{
    const std::wstring updateDir = AppPaths::updatesDirectoryPath();
    if (updateDir.empty())
        return {};
    return updateDir + L"\\" + releaseFileName(m_updateRelease);
}

bool MainWindow::launchDownloadedUpdate() const
{
    const std::wstring path = downloadedUpdatePath();
    if (path.empty())
        return false;

    std::vector<std::wstring> connectionNames = m_sessionManager
        ? m_sessionManager->openProfileNames()
        : std::vector<std::wstring>{};
    const std::wstring connectionsArg = launch::buildConnectionsArgumentValue(connectionNames);

    std::wstring params;
    if (AppPaths::isPortableMode())
        params = L"--portable";
    if (!connectionsArg.empty()) {
        if (!params.empty())
            params += L" ";
        params += L"--connections=\"";
        params += connectionsArg;
        params += L"\"";
    }

    const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", path.c_str(),
                                             params.empty() ? nullptr : params.c_str(),
                                             nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(result) > 32;
}

void MainWindow::startBackgroundUpdateCheck()
{
    if (m_updateCheckInFlight || m_updateDownloadInFlight)
        return;

    m_updateCheckInFlight = true;
    const std::uint64_t generation = ++m_updateCheckGeneration;
    const HWND hwnd = GetSafeHwnd();
    std::thread([hwnd, generation]() {
        auto result = std::make_unique<UpdateCheckResult>();
        result->generation = generation;
        std::wstring error;
        updater::ReleaseAsset release;
        if (updater::fetchLatestRelease(L"o3ku", L"RdpBox", L"RdpBox.exe", release, error)) {
            result->success = true;
            result->release = std::move(release);
            result->hasUpdate = updater::isNewerReleaseTag(RDPBOX_VERSION, result->release.tagName);
        } else {
            result->success = false;
            result->errorMessage = std::move(error);
        }

        if (::IsWindow(hwnd))
            ::PostMessageW(hwnd, WM_APP_UPDATE_CHECK_COMPLETED, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

void MainWindow::startBackgroundUpdateDownload()
{
    if (m_updateDownloadInFlight || m_updateRelease.downloadUrl.empty())
        return;

    m_updateDownloadInFlight = true;
    m_updateButtonState = UpdateButtonState::Downloading;
    updateCaptionTooltip();
    layoutChildren();
    invalidateCaptionButtons();
    const std::uint64_t generation = ++m_updateDownloadGeneration;
    const HWND hwnd = GetSafeHwnd();
    const updater::ReleaseAsset release = m_updateRelease;
    std::thread([hwnd, generation, release]() {
        auto result = std::make_unique<UpdateDownloadResult>();
        result->generation = generation;
        std::wstring error;
        const std::wstring targetPath = AppPaths::updatesDirectoryPath().empty()
            ? std::wstring()
            : (AppPaths::updatesDirectoryPath() + L"\\" + releaseFileName(release));
        if (!targetPath.empty() && updater::downloadReleaseAsset(release, targetPath, error)) {
            result->success = true;
        } else {
            result->success = false;
            result->errorMessage = std::move(error);
        }

        if (::IsWindow(hwnd))
            ::PostMessageW(hwnd, WM_APP_UPDATE_DOWNLOAD_COMPLETED, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

LRESULT MainWindow::OnUpdateCheckCompleted(WPARAM, LPARAM lParam)
{
    std::unique_ptr<UpdateCheckResult> result(reinterpret_cast<UpdateCheckResult *>(lParam));
    m_updateCheckInFlight = false;
    if (!result || result->generation != m_updateCheckGeneration)
        return 0;

    if (!result->success)
        return 0;

    if (!result->hasUpdate) {
        m_updateButtonState = UpdateButtonState::Hidden;
        m_updateRelease = {};
        updateCaptionTooltip();
        layoutChildren();
        invalidateCaptionButtons();
        return 0;
    }

    m_updateRelease = result->release;
    const std::wstring path = downloadedUpdatePath();
    m_updateButtonState = std::filesystem::exists(path)
        ? UpdateButtonState::Downloaded
        : UpdateButtonState::Available;
    updateCaptionTooltip();
    layoutChildren();
    invalidateCaptionButtons();
    return 0;
}

LRESULT MainWindow::OnUpdateDownloadCompleted(WPARAM, LPARAM lParam)
{
    std::unique_ptr<UpdateDownloadResult> result(reinterpret_cast<UpdateDownloadResult *>(lParam));
    m_updateDownloadInFlight = false;
    if (!result || result->generation != m_updateDownloadGeneration)
        return 0;

    if (!result->success) {
        MessageBox(result->errorMessage.c_str(), L"Update Download Failed", MB_OK | MB_ICONERROR);
        m_updateButtonState = UpdateButtonState::Available;
        updateCaptionTooltip();
        layoutChildren();
        invalidateCaptionButtons();
        return 0;
    }

    m_updateButtonState = UpdateButtonState::Downloaded;
    updateCaptionTooltip();
    layoutChildren();
    invalidateCaptionButtons();

    CString prompt;
    if (m_updateRelease.tagName.empty()) {
        prompt = L"Update downloaded. Launch new version now?";
    } else {
        prompt = L"Update ";
        prompt += m_updateRelease.tagName.c_str();
        prompt += L" downloaded. Launch now?";
    }
    if (MessageBox(prompt, L"Update Downloaded", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (launchDownloadedUpdate())
            PostMessage(WM_CLOSE);
    }
    return 0;
}
