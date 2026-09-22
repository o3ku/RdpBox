#include "MainWindow.h"

#include "common/AppPaths.h"
#include "common/UpdateClient.h"
#include "mfcui/session/SessionManager.h"
#include "mfcui/MainWindowLayoutBehavior.h"
#include "common/ui/MainWindowUpdateBehavior.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <thread>

namespace
{
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

struct MessageBoxCenterContext
{
    HWND owner = nullptr;
    HHOOK hook = nullptr;
};

thread_local MessageBoxCenterContext *g_messageBoxCenterContext = nullptr;

LRESULT CALLBACK centerMessageBoxHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HCBT_ACTIVATE || !g_messageBoxCenterContext || !g_messageBoxCenterContext->owner)
        return ::CallNextHookEx(g_messageBoxCenterContext ? g_messageBoxCenterContext->hook : nullptr,
                                code, wParam, lParam);

    HWND dialog = reinterpret_cast<HWND>(wParam);
    RECT ownerRect = {};
    RECT dialogRect = {};
    bool haveOwnerRect = false;
    if (::IsIconic(g_messageBoxCenterContext->owner)) {
        // GetWindowRect on a minimized window returns the off-screen iconic
        // placeholder (-32000,-32000); center on the restore position instead.
        WINDOWPLACEMENT placement = {};
        placement.length = sizeof(placement);
        if (::GetWindowPlacement(g_messageBoxCenterContext->owner, &placement)) {
            ownerRect = placement.rcNormalPosition;
            haveOwnerRect = true;
        }
    }
    if (!haveOwnerRect)
        haveOwnerRect = ::GetWindowRect(g_messageBoxCenterContext->owner, &ownerRect) != FALSE;
    if (haveOwnerRect && ::GetWindowRect(dialog, &dialogRect)) {
        const int dialogWidth = dialogRect.right - dialogRect.left;
        const int dialogHeight = dialogRect.bottom - dialogRect.top;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - dialogWidth) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - dialogHeight) / 2;
        ::SetWindowPos(dialog, nullptr, x, y, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (g_messageBoxCenterContext->hook) {
        ::UnhookWindowsHookEx(g_messageBoxCenterContext->hook);
        g_messageBoxCenterContext->hook = nullptr;
    }
    return 0;
}

int centeredMessageBox(HWND owner, const CString &text, const wchar_t *caption, UINT type)
{
    MessageBoxCenterContext context;
    context.owner = owner;
    g_messageBoxCenterContext = &context;
    context.hook = ::SetWindowsHookExW(WH_CBT,
                                       centerMessageBoxHook,
                                       nullptr,
                                       ::GetCurrentThreadId());
    const int result = ::MessageBoxW(owner, text, caption, type);
    if (context.hook)
        ::UnhookWindowsHookEx(context.hook);
    g_messageBoxCenterContext = nullptr;
    return result;
}

}

ui::UpdateUiState MainWindow::updateUiState() const
{
    switch (m_updateButtonState) {
    case UpdateButtonState::Available:
        return ui::UpdateUiState::Available;
    case UpdateButtonState::Downloading:
        return ui::UpdateUiState::Downloading;
    case UpdateButtonState::Downloaded:
        return ui::UpdateUiState::Downloaded;
    case UpdateButtonState::Hidden:
    default:
        return ui::UpdateUiState::Hidden;
    }
}

bool MainWindow::shouldShowUpdateButton() const
{
    return ui::shouldShowUpdateButton(updateUiState());
}

CRect MainWindow::updateButtonRect() const
{
    CRect clientRect;
    const_cast<MainWindow *>(this)->GetClientRect(&clientRect);
    const ui::LayoutRect rect = ui::mainWindowUpdateButtonRect(clientRect.right);
    return CRect(rect.left, rect.top, rect.right, rect.bottom);
}

int MainWindow::captionButtonReserveWidth() const
{
    return ui::mainWindowCaptionButtonReserveWidth(shouldShowUpdateButton());
}

void MainWindow::invalidateUpdateButton()
{
    if (GetSafeHwnd())
        InvalidateRect(updateButtonRect(), FALSE);
}

CString MainWindow::updateTooltipText() const
{
    return ui::updateTooltipText(updateUiState(),
                                 m_updateRelease.tagName,
                                 m_updateDownloadProgress).c_str();
}

CString MainWindow::updateButtonText() const
{
    return ui::updateButtonText(updateUiState(),
                                m_updateDownloadProgress).c_str();
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
    return updateDir + L"\\" + ui::updateReleaseFileName(m_updateRelease.tagName);
}

bool MainWindow::launchDownloadedUpdate() const
{
    const std::wstring downloadedPath = downloadedUpdatePath();
    const std::wstring currentExePath = AppPaths::executablePath();
    if (downloadedPath.empty() || currentExePath.empty())
        return false;

    std::vector<std::wstring> connectionNames = m_sessionManager
        ? m_sessionManager->openProfileNames()
        : std::vector<std::wstring>{};

    return updater::applyDownloadedUpdate(downloadedPath, currentExePath, connectionNames);
}

bool MainWindow::confirmLaunchDownloadedUpdate()
{
    const CString prompt = ui::downloadedUpdatePrompt(m_updateRelease.tagName).c_str();

    if (centeredMessageBox(GetSafeHwnd(),
                           prompt,
                           L"Update Downloaded",
                           MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES)
        return false;

    if (launchDownloadedUpdate()) {
        PostMessage(WM_CLOSE);
        return true;
    }

    MessageBox(L"Failed to launch downloaded update.", L"Update Launch Failed", MB_OK | MB_ICONERROR);
    return false;
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
    m_updateDownloadProgress = 0;
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
            : (AppPaths::updatesDirectoryPath() + L"\\" + ui::updateReleaseFileName(release.tagName));
        auto progressCallback = [hwnd, generation](std::uint64_t bytesReceived, std::uint64_t totalBytes) {
            if (!::IsWindow(hwnd))
                return;

            const int progress = ui::updateDownloadProgressPercent(bytesReceived, totalBytes);
            ::PostMessageW(hwnd, WM_APP_UPDATE_DOWNLOAD_PROGRESS,
                           static_cast<WPARAM>(progress),
                           static_cast<LPARAM>(generation));
        };
        if (!targetPath.empty() && updater::downloadReleaseAsset(release, targetPath, error, progressCallback)) {
            result->success = true;
        } else {
            result->success = false;
            result->errorMessage = std::move(error);
        }

        if (::IsWindow(hwnd))
            ::PostMessageW(hwnd, WM_APP_UPDATE_DOWNLOAD_COMPLETED, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

LRESULT MainWindow::OnUpdateDownloadProgress(WPARAM wParam, LPARAM lParam)
{
    if (static_cast<std::uint64_t>(lParam) != m_updateDownloadGeneration)
        return 0;

    m_updateDownloadProgress = static_cast<int>(wParam);
    updateCaptionTooltip();
    invalidateUpdateButton();
    return 0;
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
        m_updateDownloadProgress = -1;
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
    m_updateDownloadProgress = (m_updateButtonState == UpdateButtonState::Downloaded) ? 100 : -1;
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
        m_updateDownloadProgress = -1;
        updateCaptionTooltip();
        layoutChildren();
        invalidateCaptionButtons();
        return 0;
    }

    m_updateButtonState = UpdateButtonState::Downloaded;
    m_updateDownloadProgress = 100;
    updateCaptionTooltip();
    layoutChildren();
    invalidateCaptionButtons();

    confirmLaunchDownloadedUpdate();
    return 0;
}
