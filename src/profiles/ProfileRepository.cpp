#include "ProfileRepository.h"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <vector>

#include <cjson/cJSON.h>
#include <windows.h>

#include "common/PasswordProtection.h"
#include "common/Win32String.h"

namespace
{
static cJSON *profileToJson(const Profile &profile)
{
    cJSON *object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "id", profile.id.c_str());
    cJSON_AddStringToObject(object, "name", utf8FromWide(profile.name).c_str());
    cJSON_AddStringToObject(object, "host", utf8FromWide(profile.host).c_str());
    cJSON_AddNumberToObject(object, "port", profile.port);
    cJSON_AddStringToObject(object, "username", utf8FromWide(profile.username).c_str());
    if (PasswordProtection::mode() == PasswordProtection::Mode::Portable)
        cJSON_AddStringToObject(object, "passwordPortable", PasswordProtection::protectPortable(profile.password).c_str());
    else
        cJSON_AddStringToObject(object, "passwordProtected", PasswordProtection::protectDpapi(profile.password).c_str());
    cJSON_AddStringToObject(object, "domain", utf8FromWide(profile.domain).c_str());
    cJSON_AddBoolToObject(object, "clipboardEnabled", profile.clipboardEnabled);
    cJSON_AddBoolToObject(object, "ignoreCertificate", profile.ignoreCertificate);
    cJSON_AddBoolToObject(object, "fullScreenOnConnect", profile.fullScreenOnConnect);
    cJSON_AddStringToObject(object, "lastConnectedAt", profile.lastConnectedAt.c_str());
    return object;
}

static cJSON *profileArrayToJson(const std::vector<Profile> &profiles)
{
    cJSON *array = cJSON_CreateArray();
    for (const Profile &profile : profiles)
        cJSON_AddItemToArray(array, profileToJson(profile));
    return array;
}

static Profile profileFromJson(const cJSON *object, bool &needsMigration)
{
    Profile profile;
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(object, "id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(object, "name");
    const cJSON *host = cJSON_GetObjectItemCaseSensitive(object, "host");
    const cJSON *port = cJSON_GetObjectItemCaseSensitive(object, "port");
    const cJSON *username = cJSON_GetObjectItemCaseSensitive(object, "username");
    const cJSON *passwordPortable = cJSON_GetObjectItemCaseSensitive(object, "passwordPortable");
    const cJSON *passwordProtected = cJSON_GetObjectItemCaseSensitive(object, "passwordProtected");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(object, "password");
    const cJSON *domain = cJSON_GetObjectItemCaseSensitive(object, "domain");
    const cJSON *clipboardEnabled = cJSON_GetObjectItemCaseSensitive(object, "clipboardEnabled");
    const cJSON *ignoreCertificate = cJSON_GetObjectItemCaseSensitive(object, "ignoreCertificate");
    const cJSON *fullScreenOnConnect = cJSON_GetObjectItemCaseSensitive(object, "fullScreenOnConnect");
    const cJSON *lastConnectedAt = cJSON_GetObjectItemCaseSensitive(object, "lastConnectedAt");

    profile.id = cJSON_GetStringValue(id) ? cJSON_GetStringValue(id) : "";
    profile.name = wideFromUtf8(cJSON_GetStringValue(name) ? cJSON_GetStringValue(name) : "");
    profile.host = wideFromUtf8(cJSON_GetStringValue(host) ? cJSON_GetStringValue(host) : "");
    profile.port = cJSON_IsNumber(port) ? port->valueint : 3389;
    profile.username = wideFromUtf8(cJSON_GetStringValue(username) ? cJSON_GetStringValue(username) : "");
    if (cJSON_GetStringValue(passwordPortable)) {
        bool ok = false;
        profile.password = PasswordProtection::unprotectPortable(cJSON_GetStringValue(passwordPortable), &ok);
        if (!ok)
            profile.password.clear();
        if (PasswordProtection::mode() != PasswordProtection::Mode::Portable)
            needsMigration = true;
    } else if (cJSON_GetStringValue(passwordProtected)) {
        bool ok = false;
        profile.password = PasswordProtection::unprotectDpapi(cJSON_GetStringValue(passwordProtected), &ok);
        if (!ok)
            profile.password.clear();
        if (PasswordProtection::mode() == PasswordProtection::Mode::Portable && ok)
            needsMigration = true;
    } else {
        profile.password = wideFromUtf8(cJSON_GetStringValue(password) ? cJSON_GetStringValue(password) : "");
        if (!profile.password.empty())
            needsMigration = true;
    }
    profile.domain = wideFromUtf8(cJSON_GetStringValue(domain) ? cJSON_GetStringValue(domain) : "");
    profile.clipboardEnabled = !cJSON_IsFalse(clipboardEnabled);
    profile.ignoreCertificate = !cJSON_IsFalse(ignoreCertificate);
    profile.fullScreenOnConnect = cJSON_IsTrue(fullScreenOnConnect);
    profile.lastConnectedAt = cJSON_GetStringValue(lastConnectedAt) ? cJSON_GetStringValue(lastConnectedAt) : "";
    return profile;
}

static std::string readFile(const std::wstring &filePath)
{
    std::FILE *file = nullptr;
    if (_wfopen_s(&file, filePath.c_str(), L"rb") != 0 || !file)
        return {};

    std::string contents;
    char buffer[4096];
    while (const size_t read = std::fread(buffer, 1, sizeof(buffer), file)) {
        contents.append(buffer, read);
    }

    std::fclose(file);
    return contents;
}

static void writeFile(const std::wstring &filePath, const std::string &contents)
{
    std::FILE *file = nullptr;
    if (_wfopen_s(&file, filePath.c_str(), L"wb") != 0 || !file)
        return;

    if (!contents.empty())
        std::fwrite(contents.data(), 1, contents.size(), file);

    std::fclose(file);
}

static bool containsInsensitive(const std::wstring &haystack, const std::wstring &needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > haystack.size())
        return false;

    const std::wstring_view haystackView(haystack);
    const std::wstring_view needleView(needle);
    for (size_t offset = 0; offset + needleView.size() <= haystackView.size(); ++offset) {
        const int result = CompareStringOrdinal(haystackView.data() + offset,
                                                static_cast<int>(needleView.size()),
                                                needleView.data(),
                                                static_cast<int>(needleView.size()),
                                                TRUE);
        if (result == CSTR_EQUAL)
            return true;
    }
    return false;
}
}

ProfileRepository::ProfileRepository(std::wstring filePath)
    : m_filePath(filePath)
{
    load();
}

const std::vector<Profile> &ProfileRepository::profiles() const
{
    return m_profiles;
}

Profile ProfileRepository::profileById(const std::string &id) const
{
    const auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
        [&](const Profile &profile) { return profile.id == id; });
    return (it != m_profiles.end()) ? *it : Profile{};
}

void ProfileRepository::addProfile(const Profile &profile)
{
    Profile stored = profile;
    if (stored.id.empty())
        stored.id = createGuidString();

    m_profiles.push_back(stored);
    save();
}

void ProfileRepository::updateProfile(const Profile &profile)
{
    for (auto &item : m_profiles) {
        if (item.id == profile.id) {
            item = profile;
            save();
            return;
        }
    }
}

void ProfileRepository::removeProfile(const std::string &id)
{
    m_profiles.erase(std::remove_if(m_profiles.begin(), m_profiles.end(),
        [&](const Profile &profile) { return profile.id == id; }), m_profiles.end());
    save();
}

std::vector<Profile> ProfileRepository::search(const std::wstring &query) const
{
    if (query.empty())
        return m_profiles;

    std::vector<Profile> result;
    for (const Profile &profile : m_profiles) {
        if (containsInsensitive(profile.name, query)
            || containsInsensitive(profile.host, query)) {
            result.push_back(profile);
        }
    }
    return result;
}

void ProfileRepository::load()
{
    const std::string contents = readFile(m_filePath);
    if (contents.empty())
        return;

    cJSON *root = cJSON_Parse(contents.c_str());
    if (!root)
        return;

    m_profiles.clear();
    bool needsMigration = false;
    cJSON *profilesArray = nullptr;
    if (cJSON_IsArray(root)) {
        profilesArray = root;
        needsMigration = true;
    } else if (cJSON_IsObject(root)) {
        profilesArray = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    }

    if (!profilesArray || !cJSON_IsArray(profilesArray)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, profilesArray) {
        if (cJSON_IsObject(item))
            m_profiles.push_back(profileFromJson(item, needsMigration));
    }

    cJSON_Delete(root);
    if (needsMigration)
        save();
}

void ProfileRepository::save() const
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "portableMode", PasswordProtection::mode() == PasswordProtection::Mode::Portable);
    cJSON_AddItemToObject(root, "profiles", profileArrayToJson(m_profiles));

    char *json = cJSON_Print(root);
    if (!json) {
        cJSON_Delete(root);
        return;
    }

    writeFile(m_filePath, json);
    cJSON_free(json);
    cJSON_Delete(root);
}
