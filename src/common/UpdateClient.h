#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace updater
{
struct ReleaseAsset
{
    std::wstring tagName;
    std::wstring assetName;
    std::wstring downloadUrl;
};

bool isNewerReleaseTag(const std::wstring &currentTag, const std::wstring &candidateTag);
bool fetchLatestRelease(const std::wstring &owner,
                        const std::wstring &repository,
                        const std::wstring &assetName,
                        ReleaseAsset &asset,
                        std::wstring &errorMessage);
using DownloadProgressCallback = std::function<void(std::uint64_t bytesReceived, std::uint64_t totalBytes)>;
bool downloadReleaseAsset(const ReleaseAsset &asset,
                          const std::wstring &targetPath,
                          std::wstring &errorMessage,
                          DownloadProgressCallback progressCallback = {});

// Verifies the downloaded copy of the release asset named `assetName` against
// the same release's SHA256SUMS.txt asset. Returns false when the sums asset
// is missing, does not list the asset, or the digest does not match. Callers
// must refuse to execute a download that fails this check.
bool verifyDownloadedAssetSha256(const std::wstring &owner,
                                 const std::wstring &repository,
                                 const std::wstring &assetName,
                                 const std::wstring &downloadedPath,
                                 std::wstring &errorMessage);

// Writes the apply-update PowerShell script and spawns it. Returns true once the
// helper process is running; it waits for this process to exit, replaces the exe
// and relaunches it with the given connection names restored.
bool applyDownloadedUpdate(const std::wstring &downloadedPath,
                           const std::wstring &currentExePath,
                           const std::vector<std::wstring> &connectionNames);
}
