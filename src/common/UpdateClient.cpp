#include "common/UpdateClient.h"

#include "common/AppPaths.h"
#include "common/Win32String.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace
{
struct WinHttpHandleCloser
{
    void operator()(void *handle) const
    {
        if (handle)
            ::WinHttpCloseHandle(static_cast<HINTERNET>(handle));
    }
};

using UniqueWinHttpHandle = std::unique_ptr<void, WinHttpHandleCloser>;

struct ParsedUrl
{
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

std::wstring stripVersionPrefix(const std::wstring &tag)
{
    if (!tag.empty() && (tag.front() == L'v' || tag.front() == L'V'))
        return tag.substr(1);
    return tag;
}

std::vector<int> parseVersionComponents(const std::wstring &tag)
{
    std::vector<int> components;
    std::wstring current;
    for (wchar_t ch : stripVersionPrefix(tag)) {
        if (ch >= L'0' && ch <= L'9') {
            current.push_back(ch);
            continue;
        }
        if (ch == L'.') {
            if (!current.empty()) {
                components.push_back(_wtoi(current.c_str()));
                current.clear();
            }
            continue;
        }
        break;
    }

    if (!current.empty())
        components.push_back(_wtoi(current.c_str()));
    return components;
}

bool parseUrl(const std::wstring &url, ParsedUrl &parsed)
{
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &components))
        return false;

    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0)
        parsed.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return !parsed.host.empty() && !parsed.path.empty();
}

bool readResponseBody(HINTERNET request,
                      std::vector<std::uint8_t> &bytes,
                      std::wstring &errorMessage,
                      const updater::DownloadProgressCallback &progressCallback)
{
    bytes.clear();
    std::uint64_t totalBytes = 0;
    DWORD totalBytesSize = sizeof(totalBytes);
    if (!::WinHttpQueryHeaders(request,
                               WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,
                               &totalBytes,
                               &totalBytesSize,
                               WINHTTP_NO_HEADER_INDEX)) {
        totalBytes = 0;
    }

    std::uint64_t receivedBytes = 0;
    if (progressCallback)
        progressCallback(receivedBytes, totalBytes);

    for (;;) {
        DWORD available = 0;
        if (!::WinHttpQueryDataAvailable(request, &available)) {
            errorMessage = L"Failed to query update response size.";
            return false;
        }
        if (available == 0)
            return true;

        const std::size_t start = bytes.size();
        bytes.resize(start + available);
        DWORD read = 0;
        if (!::WinHttpReadData(request, bytes.data() + start, available, &read)) {
            errorMessage = L"Failed to read update response.";
            return false;
        }
        bytes.resize(start + read);
        receivedBytes += read;
        if (progressCallback)
            progressCallback(receivedBytes, totalBytes);
        if (read == 0)
            return true;
    }
}

bool sendHttpRequest(const std::wstring &url,
                     const wchar_t *acceptTypes[],
                     std::vector<std::uint8_t> &responseBytes,
                     std::wstring &errorMessage,
                     const updater::DownloadProgressCallback &progressCallback = {})
{
    ParsedUrl parsed;
    if (!parseUrl(url, parsed)) {
        errorMessage = L"Invalid update URL.";
        return false;
    }

    UniqueWinHttpHandle session(::WinHttpOpen(L"RdpBox/1.0",
                                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                              WINHTTP_NO_PROXY_NAME,
                                              WINHTTP_NO_PROXY_BYPASS,
                                              0));
    if (!session) {
        errorMessage = L"Failed to open HTTP session.";
        return false;
    }

    UniqueWinHttpHandle connection(::WinHttpConnect(static_cast<HINTERNET>(session.get()),
                                                    parsed.host.c_str(),
                                                    parsed.port,
                                                    0));
    if (!connection) {
        errorMessage = L"Failed to connect to update server.";
        return false;
    }

    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    UniqueWinHttpHandle request(::WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()),
                                                     L"GET",
                                                     parsed.path.c_str(),
                                                     nullptr,
                                                     WINHTTP_NO_REFERER,
                                                     acceptTypes,
                                                     flags));
    if (!request) {
        errorMessage = L"Failed to create update request.";
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    ::WinHttpSetOption(static_cast<HINTERNET>(request.get()),
                       WINHTTP_OPTION_REDIRECT_POLICY,
                       &redirectPolicy,
                       sizeof(redirectPolicy));

    const std::wstring headers =
        L"User-Agent: RdpBox\r\n"
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!::WinHttpSendRequest(static_cast<HINTERNET>(request.get()),
                              headers.c_str(),
                              static_cast<DWORD>(headers.size()),
                              WINHTTP_NO_REQUEST_DATA,
                              0,
                              0,
                              0)) {
        errorMessage = L"Failed to send update request.";
        return false;
    }

    if (!::WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
        errorMessage = L"Failed to receive update response.";
        return false;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (!::WinHttpQueryHeaders(static_cast<HINTERNET>(request.get()),
                               WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,
                               &statusCode,
                               &size,
                               WINHTTP_NO_HEADER_INDEX)) {
        errorMessage = L"Failed to read update status code.";
        return false;
    }

    if (statusCode < 200 || statusCode >= 300) {
        wchar_t buffer[64] = {};
        std::swprintf(buffer, std::size(buffer), L"Update request failed (%lu).", statusCode);
        errorMessage = buffer;
        return false;
    }

    return readResponseBody(static_cast<HINTERNET>(request.get()), responseBytes, errorMessage, progressCallback);
}

std::wstring downloadTargetPathForAsset(const updater::ReleaseAsset &asset)
{
    const std::wstring updateDir = AppPaths::updatesDirectoryPath();
    if (updateDir.empty())
        return {};

    std::wstring fileName = asset.assetName;
    if (!asset.tagName.empty())
        fileName = L"RdpBox-" + asset.tagName + L".exe";
    return updateDir + L"\\" + fileName;
}
}

namespace updater
{
bool isNewerReleaseTag(const std::wstring &currentTag, const std::wstring &candidateTag)
{
    const std::vector<int> current = parseVersionComponents(currentTag);
    const std::vector<int> candidate = parseVersionComponents(candidateTag);
    const std::size_t count = std::max(current.size(), candidate.size());
    for (std::size_t i = 0; i < count; ++i) {
        const int currentValue = i < current.size() ? current[i] : 0;
        const int candidateValue = i < candidate.size() ? candidate[i] : 0;
        if (candidateValue != currentValue)
            return candidateValue > currentValue;
    }
    return false;
}

bool fetchLatestRelease(const std::wstring &owner,
                        const std::wstring &repository,
                        const std::wstring &assetName,
                        ReleaseAsset &asset,
                        std::wstring &errorMessage)
{
    const std::wstring url =
        L"https://api.github.com/repos/" + owner + L"/" + repository + L"/releases/latest";
    const wchar_t *acceptTypes[] = { L"*/*", nullptr };
    std::vector<std::uint8_t> responseBytes;
    if (!sendHttpRequest(url, acceptTypes, responseBytes, errorMessage))
        return false;

    nlohmann::json root = nlohmann::json::parse(responseBytes.begin(), responseBytes.end(), nullptr, false);
    if (!root.is_object()) {
        errorMessage = L"Failed to parse release metadata.";
        return false;
    }

    const std::string tagName = root.value("tag_name", "");
    if (tagName.empty()) {
        errorMessage = L"Latest release tag is missing.";
        return false;
    }

    const auto assetsIt = root.find("assets");
    if (assetsIt == root.end() || !assetsIt->is_array()) {
        errorMessage = L"Latest release does not contain assets.";
        return false;
    }

    for (const auto &item : *assetsIt) {
        if (!item.is_object())
            continue;

        const std::string currentAssetName = item.value("name", "");
        if (wideFromUtf8(currentAssetName) != assetName)
            continue;

        const std::string browserDownloadUrl = item.value("browser_download_url", "");
        if (browserDownloadUrl.empty())
            continue;

        asset.tagName = wideFromUtf8(tagName);
        asset.assetName = wideFromUtf8(currentAssetName);
        asset.downloadUrl = wideFromUtf8(browserDownloadUrl);
        return true;
    }

    errorMessage = L"Expected release asset was not found.";
    return false;
}

bool downloadReleaseAsset(const ReleaseAsset &asset,
                          const std::wstring &targetPath,
                          std::wstring &errorMessage,
                          DownloadProgressCallback progressCallback)
{
    const wchar_t *acceptTypes[] = { L"*/*", nullptr };
    std::vector<std::uint8_t> bytes;
    if (!sendHttpRequest(asset.downloadUrl, acceptTypes, bytes, errorMessage, progressCallback))
        return false;

    if (!AppPaths::writeFileContent(targetPath,
                                    std::string(reinterpret_cast<const char *>(bytes.data()),
                                                reinterpret_cast<const char *>(bytes.data()) + bytes.size()))) {
        errorMessage = L"Failed to write downloaded update file.";
        return false;
    }
    return true;
}
}
