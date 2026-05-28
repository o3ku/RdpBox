#include "AppPaths.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>

#include <shlobj.h>
#include <windows.h>

namespace
{
std::wstring executableModulePath()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath))
        return {};

    return std::wstring(modulePath, length);
}

std::wstring executableDirectory()
{
    std::wstring path = executableModulePath();
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    return path.substr(0, slash);
}

bool fileExists(const std::wstring &path)
{
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string readFile(const std::wstring &filePath)
{
    std::FILE *file = nullptr;
    if (_wfopen_s(&file, filePath.c_str(), L"rb") != 0 || !file)
        return {};

    std::string contents;
    char buffer[4096];
    while (const size_t read = std::fread(buffer, 1, sizeof(buffer), file))
        contents.append(buffer, read);

    std::fclose(file);
    return contents;
}

bool writeFileAtomically(const std::wstring &filePath, const std::string &contents)
{
    const std::wstring tempPath = filePath
        + L"." + std::to_wstring(::GetCurrentProcessId())
        + L"." + std::to_wstring(::GetTickCount64())
        + L".tmp";

    std::FILE *file = nullptr;
    if (_wfopen_s(&file, tempPath.c_str(), L"wb") != 0 || !file)
        return false;

    const bool writeOk = contents.empty()
        || std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool closeOk = std::fclose(file) == 0;

    if (!writeOk || !closeOk) {
        ::DeleteFileW(tempPath.c_str());
        return false;
    }

    if (!::MoveFileExW(tempPath.c_str(), filePath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tempPath.c_str());
        return false;
    }

    return true;
}

bool writeFile(const std::wstring &filePath, const std::string &contents)
{
    return writeFileAtomically(filePath, contents);
}

std::wstring appDataDirectory()
{
    wchar_t pathBuffer[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, pathBuffer)))
        return {};

    return std::wstring(pathBuffer) + L"\\RdpBox";
}

void ensureDirectoryExists(const std::wstring &path)
{
    if (!path.empty())
        ::CreateDirectoryW(path.c_str(), nullptr);
}
}

namespace AppPaths
{
std::wstring executablePath()
{
    return executableModulePath();
}

std::string readFileContent(const std::wstring &filePath)
{
    std::FILE *file = nullptr;
    if (_wfopen_s(&file, filePath.c_str(), L"rb") != 0 || !file)
        return {};

    std::string contents;
    char buffer[4096];
    while (const size_t read = std::fread(buffer, 1, sizeof(buffer), file))
        contents.append(buffer, read);

    std::fclose(file);
    return contents;
}

bool writeFileContent(const std::wstring &filePath, const std::string &contents)
{
    return writeFileAtomically(filePath, contents);
}
}

namespace
{
using AppPaths::readFileContent;
using AppPaths::writeFileContent;

std::wstring portableProfilesPath()
{
    const std::wstring root = executableDirectory();
    return root.empty() ? std::wstring() : (root + L"\\profiles.json");
}

bool readPortableModeFromProfiles()
{
    const std::wstring path = portableProfilesPath();
    if (path.empty() || !fileExists(path))
        return false;

    const std::string contents = readFile(path);
    if (contents.empty())
        return false;

    const auto root = nlohmann::json::parse(contents, nullptr, false);
    if (!root.is_object())
        return false;

    return root.value("portableMode", false);
}

bool writePortableModeToProfiles(bool portableMode)
{
    const std::wstring path = portableProfilesPath();
    if (path.empty())
        return false;

    auto root = nlohmann::json::object();
    const std::string existing = readFile(path);
    if (!existing.empty()) {
        auto parsed = nlohmann::json::parse(existing, nullptr, false);
        if (parsed.is_object()) {
            root = std::move(parsed);
        }
    } else {
        root = nlohmann::json::object();
    }

    root["portableMode"] = portableMode;
    if (!root.contains("profiles") || !root["profiles"].is_array())
        root["profiles"] = nlohmann::json::array();

    return writeFile(path, root.dump(4));
}
}

namespace AppPaths
{
std::atomic<bool> g_forcePortable{false};

bool enablePortableMode()
{
    g_forcePortable = writePortableModeToProfiles(true);
    return g_forcePortable.load();
}

bool isPortableMode()
{
    return g_forcePortable.load() || readPortableModeFromProfiles();
}

std::wstring dataRootPath()
{
    const std::wstring root = isPortableMode()
        ? executableDirectory()
        : appDataDirectory();
    if (root.empty())
        return {};

    ensureDirectoryExists(root);
    return root;
}

std::wstring profilesFilePath()
{
    const std::wstring root = dataRootPath();
    return root.empty() ? std::wstring() : (root + L"\\profiles.json");
}

std::wstring frameCaptureRootPath()
{
    const std::wstring root = dataRootPath();
    if (root.empty())
        return {};

    const std::wstring path = root + L"\\frame-captures";
    ensureDirectoryExists(path);
    return path;
}

std::wstring updatesDirectoryPath()
{
    const std::wstring root = dataRootPath();
    if (root.empty())
        return {};

    const std::wstring path = root + L"\\updates";
    ensureDirectoryExists(path);
    return path;
}
}
