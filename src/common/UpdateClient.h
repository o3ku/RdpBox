#pragma once

#include <cstdint>
#include <functional>
#include <string>

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
}
