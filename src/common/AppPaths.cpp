#include "AppPaths.h"

#include <cjson/cJSON.h>
#include <shlobj.h>
#include <windows.h>

namespace
{
std::wstring executableDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath))
        return {};

    std::wstring path(modulePath, length);
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

bool writeFile(const std::wstring &filePath, const std::string &contents)
{
    std::FILE *file = nullptr;
    if (_wfopen_s(&file, filePath.c_str(), L"wb") != 0 || !file)
        return false;

    if (!contents.empty())
        std::fwrite(contents.data(), 1, contents.size(), file);

    std::fclose(file);
    return true;
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

    cJSON *root = cJSON_Parse(contents.c_str());
    if (!root)
        return false;

    bool portableMode = false;
    if (cJSON_IsObject(root)) {
        const cJSON *node = cJSON_GetObjectItemCaseSensitive(root, "portableMode");
        portableMode = cJSON_IsTrue(node);
    }

    cJSON_Delete(root);
    return portableMode;
}

bool writePortableModeToProfiles(bool portableMode)
{
    const std::wstring path = portableProfilesPath();
    if (path.empty())
        return false;

    cJSON *root = nullptr;
    const std::string existing = readFile(path);
    if (!existing.empty())
        root = cJSON_Parse(existing.c_str());

    if (cJSON_IsArray(root)) {
        cJSON *profiles = root;
        root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "portableMode", portableMode);
        cJSON_AddItemToObject(root, "profiles", profiles);
    } else if (cJSON_IsObject(root)) {
        cJSON_ReplaceItemInObject(root, "portableMode", portableMode ? cJSON_CreateTrue() : cJSON_CreateFalse());
        if (!cJSON_GetObjectItemCaseSensitive(root, "profiles"))
            cJSON_AddItemToObject(root, "profiles", cJSON_CreateArray());
    } else {
        if (root)
            cJSON_Delete(root);
        root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "portableMode", portableMode);
        cJSON_AddItemToObject(root, "profiles", cJSON_CreateArray());
    }

    char *json = cJSON_Print(root);
    const bool ok = json && writeFile(path, json);
    if (json)
        cJSON_free(json);
    cJSON_Delete(root);
    return ok;
}
}

namespace AppPaths
{
bool g_forcePortable = false;

bool enablePortableMode()
{
    g_forcePortable = writePortableModeToProfiles(true);
    return g_forcePortable;
}

bool isPortableMode()
{
    return g_forcePortable || readPortableModeFromProfiles();
}

std::wstring appRootPath()
{
    return executableDirectory();
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

std::wstring windowStateFilePath()
{
    const std::wstring root = dataRootPath();
    return root.empty() ? std::wstring() : (root + L"\\window-state.json");
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
}
