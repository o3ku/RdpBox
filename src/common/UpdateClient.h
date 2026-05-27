#pragma once

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
bool downloadReleaseAsset(const ReleaseAsset &asset,
                          const std::wstring &targetPath,
                          std::wstring &errorMessage);
}
