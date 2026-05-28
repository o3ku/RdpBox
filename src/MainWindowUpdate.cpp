#include "MainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/UpdateClient.h"
#include "common/Win32String.h"
#include "session/SessionManager.h"

#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <thread>

namespace
{
constexpr int kUpdateButtonWidth = 38;

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

std::wstring releaseFileName(const updater::ReleaseAsset &release)
{
    if (release.tagName.empty())
        return L"RdpBox.exe";
    return L"RdpBox-" + release.tagName + L".exe";
}

std::wstring quoteForBatchSet(const std::wstring &value)
{
    std::wstring escaped;
    escaped.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch == L'"')
            escaped += L"\"\"";
        else
            escaped.push_back(ch);
    }
    return escaped;
}

bool writeScriptFile(const std::wstring &path, const std::wstring &contents)
{
    return AppPaths::writeFileContent(path, utf8FromWide(contents));
}

LRESULT CALLBACK centerMessageBoxHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HCBT_ACTIVATE || !g_messageBoxCenterContext || !g_messageBoxCenterContext->owner)
        return ::CallNextHookEx(g_messageBoxCenterContext ? g_messageBoxCenterContext->hook : nullptr,
                                code, wParam, lParam);

    HWND dialog = reinterpret_cast<HWND>(wParam);
    RECT ownerRect = {};
    RECT dialogRect = {};
    if (::GetWindowRect(g_messageBoxCenterContext->owner, &ownerRect)
        && ::GetWindowRect(dialog, &dialogRect)) {
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
        if (m_updateDownloadProgress >= 0)
            text.Format(L"Downloading update... %d%%", m_updateDownloadProgress);
        else
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

CString MainWindow::updateButtonText() const
{
    if (m_updateButtonState != UpdateButtonState::Downloading)
        return {};

    CString text;
    if (m_updateDownloadProgress >= 0)
        text.Format(L"%d%%", m_updateDownloadProgress);
    else
        text = L"...";
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
    const std::wstring downloadedPath = downloadedUpdatePath();
    const std::wstring currentExePath = AppPaths::executablePath();
    if (downloadedPath.empty() || currentExePath.empty())
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

    const std::wstring updatesDir = AppPaths::updatesDirectoryPath();
    if (updatesDir.empty())
        return false;

    const std::wstring backupExePath = currentExePath + L".bak";
    const std::wstring scriptPath = updatesDir + L"\\apply-update-"
        + std::to_wstring(::GetCurrentProcessId()) + L".ps1";
    const std::wstring logPath = updatesDir + L"\\update-apply.log";

    std::wstring script;
    script += L"$ErrorActionPreference = 'Stop'\n";
    script += L"$pidToWait = " + std::to_wstring(::GetCurrentProcessId()) + L"\n";
    script += L"$src = '" + quoteForBatchSet(downloadedPath) + L"'\n";
    script += L"$dst = '" + quoteForBatchSet(currentExePath) + L"'\n";
    script += L"$bak = '" + quoteForBatchSet(backupExePath) + L"'\n";
    script += L"$argsLine = '" + quoteForBatchSet(params) + L"'\n";
    script += L"$log = '" + quoteForBatchSet(logPath) + L"'\n";
    script += L"function Log($message) { Add-Content -Path $log -Value $message }\n";
    script += L"Log \"==== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') ====\"\n";
    script += L"Log \"PID=$pidToWait\"\n";
    script += L"Log \"SRC=$src\"\n";
    script += L"Log \"DST=$dst\"\n";
    script += L"try {\n";
    script += L"  try {\n";
    script += L"    Wait-Process -Id $pidToWait -ErrorAction Stop\n";
    script += L"    Log 'old process exited'\n";
    script += L"  } catch {\n";
    script += L"    Log 'old process already exited'\n";
    script += L"  }\n";
    script += L"  for ($i = 0; $i -lt 20; $i++) {\n";
    script += L"    try {\n";
    script += L"      if (Test-Path $bak) { Remove-Item -Path $bak -Force -ErrorAction SilentlyContinue }\n";
    script += L"      if (Test-Path $dst) { Move-Item -Path $dst -Destination $bak -Force }\n";
    script += L"      Copy-Item -Path $src -Destination $dst -Force\n";
    script += L"      if (Test-Path $dst) {\n";
    script += L"        Log 'replacement complete'\n";
    script += L"        break\n";
    script += L"      }\n";
    script += L"    } catch {\n";
    script += L"      Log \"replace retry $i : $($_.Exception.Message)\"\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"      continue\n";
    script += L"    }\n";
    script += L"  }\n";
    script += L"  if (-not (Test-Path $dst)) { throw 'replacement failed' }\n";
    script += L"  Remove-Item -Path $src -Force -ErrorAction SilentlyContinue\n";
    script += L"  for ($i = 0; $i -lt 5; $i++) {\n";
    script += L"    try {\n";
    script += L"      if ([string]::IsNullOrWhiteSpace($argsLine)) {\n";
    script += L"        Log \"launching without args try $i\"\n";
    script += L"        Start-Process -FilePath $dst\n";
    script += L"      } else {\n";
    script += L"        Log \"launching with args try $i : $argsLine\"\n";
    script += L"        Start-Process -FilePath $dst -ArgumentList $argsLine\n";
    script += L"      }\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"      $p = Get-Process -Name 'RdpBox' -ErrorAction SilentlyContinue\n";
    script += L"      if ($p) {\n";
    script += L"        Log 'launch success'\n";
    script += L"        break\n";
    script += L"      }\n";
    script += L"      Log 'launch retry'\n";
    script += L"    } catch {\n";
    script += L"      Log \"launch error $i : $($_.Exception.Message)\"\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"    }\n";
    script += L"  }\n";
    script += L"} catch {\n";
    script += L"  Log \"script error: $($_.Exception.Message)\"\n";
    script += L"} finally {\n";
    script += L"  Log 'script end'\n";
    script += L"}\n";

    if (!writeScriptFile(scriptPath, script))
        return false;

    std::wstring commandLine =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath + L"\"";
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    BOOL created = ::CreateProcessW(nullptr,
                                    commandLine.data(),
                                    nullptr,
                                    nullptr,
                                    FALSE,
                                    CREATE_NO_WINDOW,
                                    nullptr,
                                    nullptr,
                                    &startupInfo,
                                    &processInfo);
    if (!created)
        return false;

    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);
    return true;
}

bool MainWindow::confirmLaunchDownloadedUpdate()
{
    CString prompt;
    if (m_updateRelease.tagName.empty()) {
        prompt = L"Update downloaded. Launch new version now?";
    } else {
        prompt = L"Update ";
        prompt += m_updateRelease.tagName.c_str();
        prompt += L" downloaded. Launch now?";
    }

    if (centeredMessageBox(GetSafeHwnd(), prompt, L"Update Downloaded", MB_YESNO | MB_ICONQUESTION) != IDYES)
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
            : (AppPaths::updatesDirectoryPath() + L"\\" + releaseFileName(release));
        auto progressCallback = [hwnd, generation](std::uint64_t bytesReceived, std::uint64_t totalBytes) {
            if (!::IsWindow(hwnd))
                return;

            int progress = -1;
            if (totalBytes > 0) {
                const auto percent = static_cast<int>((bytesReceived * 100) / totalBytes);
                progress = std::clamp(percent, 0, 100);
            }
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
