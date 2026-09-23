#include "common/UpdateClient.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/Win32String.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <bcrypt.h>
#endif

#ifdef _WIN32
#include <winhttp.h>
#endif

#ifdef _WIN32
#pragma comment(lib, "winhttp.lib")
#endif

// Portable version-tag parsing (shared by every platform).
namespace
{
std::wstring stripVersionPrefix(const std::wstring &tag)
{
    if (!tag.empty() && (tag.front() == L'v' || tag.front() == L'V'))
        return tag.substr(1);
    return tag;
}

int wideToInt(const std::wstring &digits)
{
    int value = 0;
    for (wchar_t ch : digits)
        value = value * 10 + (ch - L'0');
    return value;
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
                components.push_back(wideToInt(current));
                current.clear();
            }
            continue;
        }
        break;
    }

    if (!current.empty())
        components.push_back(wideToInt(current));
    return components;
}
}  // anonymous namespace

#ifdef _WIN32
namespace winhttp_detail
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
                      const updater::DownloadProgressCallback &progressCallback);

bool sendHttpRequest(const std::wstring &url,
                     const wchar_t *acceptTypes[],
                     std::vector<std::uint8_t> &responseBytes,
                     std::wstring &errorMessage,
                     const updater::DownloadProgressCallback &progressCallback = {});

bool winhttp_detail::readResponseBody(HINTERNET request,
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

bool winhttp_detail::sendHttpRequest(const std::wstring &url,
                     const wchar_t *acceptTypes[],
                     std::vector<std::uint8_t> &responseBytes,
                     std::wstring &errorMessage,
                     const updater::DownloadProgressCallback &progressCallback)
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

    // Resolve/connect/send/receive timeouts (ms): bounds stalled update
    // sockets so a dead server cannot pin the download thread forever.
    // ponytail: no overall transfer deadline; a slow-drip under 30s/recv
    // still passes. Add a deadline + cancel button if that bites.
    ::WinHttpSetTimeouts(static_cast<HINTERNET>(session.get()), 10'000, 10'000, 30'000, 30'000);

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

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
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

    return winhttp_detail::readResponseBody(static_cast<HINTERNET>(request.get()), responseBytes, errorMessage, progressCallback);
}
}  // namespace winhttp_detail
#endif  // _WIN32

namespace updater
{
#ifdef _WIN32
bool applyDownloadedUpdate(const std::wstring &downloadedPath,
                           const std::wstring &currentExePath,
                           const std::vector<std::wstring> &connectionNames)
{
    const std::wstring updatesDir = AppPaths::updatesDirectoryPath();
    if (downloadedPath.empty() || currentExePath.empty() || updatesDir.empty())
        return false;

    std::wstring params;
    if (AppPaths::isPortableMode())
        params = L"--portable";
    const std::wstring connectionsArg = launch::buildConnectionsArgumentValue(connectionNames);
    if (!connectionsArg.empty()) {
        if (!params.empty())
            params += L" ";
        params += L"--connections=\"";
        params += connectionsArg;
        params += L"\"";
    }

    const std::wstring backupExePath = currentExePath + L".bak";
    const std::wstring scriptPath = updatesDir + L"\\apply-update-"
        + std::to_wstring(::GetCurrentProcessId()) + L".ps1";
    const std::wstring logPath = updatesDir + L"\\update-apply.log";
    const std::wstring script = launch::buildUpdateApplyScript(downloadedPath,
                                                               currentExePath,
                                                               backupExePath,
                                                               params,
                                                               scriptPath,
                                                               logPath);
    if (script.empty())
        return false;
    if (!AppPaths::writeFileContent(scriptPath, launch::updateScriptUtf8(script)))
        return false;

    std::wstring commandLine =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "
        + launch::powerShellSingleQuotedLiteral(scriptPath);
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
#else
bool applyDownloadedUpdate(const std::wstring &,
                           const std::wstring &,
                           const std::vector<std::wstring> &)
{
    return false;  // the update flow is a Windows-only harness for now
}
#endif
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

#ifdef _WIN32
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
    if (!winhttp_detail::sendHttpRequest(url, acceptTypes, responseBytes, errorMessage))
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
    if (!winhttp_detail::sendHttpRequest(asset.downloadUrl, acceptTypes, bytes, errorMessage, progressCallback))
        return false;

    if (!AppPaths::writeFileContent(targetPath,
                                    std::string(reinterpret_cast<const char *>(bytes.data()),
                                                reinterpret_cast<const char *>(bytes.data()) + bytes.size()))) {
        errorMessage = L"Failed to write downloaded update file.";
        return false;
    }
    return true;
}

namespace
{
std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool sha256FileHex(const std::wstring &path, std::string &hexOut, std::wstring &errorMessage)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        errorMessage = L"Failed to open SHA-256 provider.";
        return false;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        errorMessage = L"Failed to create SHA-256 hash.";
        return false;
    }

    FILE *file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        errorMessage = L"Failed to open downloaded update for hashing.";
        return false;
    }

    std::vector<std::uint8_t> buffer(1 << 16);
    bool ok = true;
    for (;;) {
        const size_t read = fread(buffer.data(), 1, buffer.size(), file);
        if (read > 0 && !BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0))) {
            errorMessage = L"Failed to hash downloaded update.";
            ok = false;
            break;
        }
        if (read < buffer.size()) {
            if (ferror(file)) {
                errorMessage = L"Failed to read downloaded update for hashing.";
                ok = false;
            }
            break;
        }
    }
    fclose(file);

    std::uint8_t digest[32] = {};
    if (ok && !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
        errorMessage = L"Failed to finish SHA-256 hash.";
        ok = false;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
        return false;

    static const char *const kHex = "0123456789abcdef";
    hexOut.clear();
    hexOut.reserve(sizeof(digest) * 2);
    for (const std::uint8_t byte : digest) {
        hexOut.push_back(kHex[byte >> 4]);
        hexOut.push_back(kHex[byte & 0x0f]);
    }
    return true;
}

bool findReleaseAssetDownloadUrl(const std::wstring &owner,
                                 const std::wstring &repository,
                                 const std::wstring &assetName,
                                 std::wstring &downloadUrl,
                                 std::wstring &errorMessage)
{
    const std::wstring url =
        L"https://api.github.com/repos/" + owner + L"/" + repository + L"/releases/latest";
    const wchar_t *acceptTypes[] = { L"*/*", nullptr };
    std::vector<std::uint8_t> responseBytes;
    if (!winhttp_detail::sendHttpRequest(url, acceptTypes, responseBytes, errorMessage))
        return false;

    nlohmann::json root = nlohmann::json::parse(responseBytes.begin(), responseBytes.end(), nullptr, false);
    if (!root.is_object()) {
        errorMessage = L"Failed to parse release metadata.";
        return false;
    }
    const auto assets = root.find("assets");
    if (assets == root.end() || !assets->is_array()) {
        errorMessage = L"Latest release does not contain assets.";
        return false;
    }
    for (const auto &item : *assets) {
        if (!item.is_object())
            continue;
        if (wideFromUtf8(item.value("name", "")) != assetName)
            continue;
        downloadUrl = wideFromUtf8(item.value("browser_download_url", ""));
        return !downloadUrl.empty();
    }
    errorMessage = L"Release does not contain the requested asset.";
    return false;
}
}

bool verifyDownloadedAssetSha256(const std::wstring &owner,
                                 const std::wstring &repository,
                                 const std::wstring &assetName,
                                 const std::wstring &downloadedPath,
                                 std::wstring &errorMessage)
{
    std::wstring sumsUrl;
    if (!findReleaseAssetDownloadUrl(owner, repository, L"SHA256SUMS.txt", sumsUrl, errorMessage))
        return false;

    const wchar_t *acceptTypes[] = { L"*/*", nullptr };
    std::vector<std::uint8_t> sumsBytes;
    if (!winhttp_detail::sendHttpRequest(sumsUrl, acceptTypes, sumsBytes, errorMessage))
        return false;
    const std::string sums(sumsBytes.begin(), sumsBytes.end());
    const std::string asset = utf8FromWide(assetName);

    // Line format: "<hex>  <name>" (sha256sum output, optional '*' marker).
    std::string expected;
    std::istringstream stream(sums);
    std::string line;
    while (std::getline(stream, line)) {
        const auto sep = line.find_first_of(" \t");
        if (sep == std::string::npos)
            continue;
        const auto nameStart = line.find_first_not_of(" \t*", sep);
        if (nameStart == std::string::npos)
            continue;
        if (line.substr(nameStart) == asset) {
            expected = line.substr(0, sep);
            break;
        }
    }
    if (expected.empty()) {
        errorMessage = L"SHA256SUMS.txt does not list the downloaded asset.";
        return false;
    }

    std::string actual;
    if (!sha256FileHex(downloadedPath, actual, errorMessage))
        return false;
    if (toLowerAscii(actual) != toLowerAscii(expected)) {
        errorMessage = L"Downloaded update failed the SHA-256 check.";
        return false;
    }
    return true;
}
#else
bool fetchLatestRelease(const std::wstring &,
                        const std::wstring &,
                        const std::wstring &,
                        ReleaseAsset &,
                        std::wstring &errorMessage)
{
    errorMessage = L"Updates are not supported on this platform.";
    return false;
}

bool downloadReleaseAsset(const ReleaseAsset &,
                          const std::wstring &,
                          std::wstring &errorMessage,
                          DownloadProgressCallback)
{
    errorMessage = L"Updates are not supported on this platform.";
    return false;
}

bool verifyDownloadedAssetSha256(const std::wstring &,
                                 const std::wstring &,
                                 const std::wstring &,
                                 const std::wstring &,
                                 std::wstring &errorMessage)
{
    errorMessage = L"Updates are not supported on this platform.";
    return false;
}
#endif
}
