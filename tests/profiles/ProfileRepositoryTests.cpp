#include <cassert>
#include <fstream>
#include <filesystem>
#include <string>

#include <cjson/cJSON.h>

#include "common/PasswordProtection.h"
#include "common/Win32String.h"
#include "profiles/ProfileRepository.h"

namespace
{
std::string readTextFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeTextFile(const std::filesystem::path &path, const std::string &contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

cJSON *parseJsonFile(const std::filesystem::path &path)
{
    const std::string contents = readTextFile(path);
    return cJSON_Parse(contents.c_str());
}
}

int main()
{
    PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);

    const std::filesystem::path filePath =
        std::filesystem::temp_directory_path()
        / wideFromUtf8("RdpBox-ProfileRepositoryTests-" + createGuidString() + ".json");
    std::filesystem::remove(filePath);

    {
        ProfileRepository repository(filePath.wstring());

        Profile profile = Profile::create();
        profile.name = L"server-a";
        profile.host = L"10.0.0.8";
        profile.username = L"alice";
        profile.password = L"secret";
        repository.addProfile(profile);

        const Profile stored = repository.profileById(profile.id);
        assert(stored.id == profile.id);
        assert(stored.host == L"10.0.0.8");
        assert(stored.clipboardEnabled);

        const std::vector<Profile> searchResults = repository.search(L"SERVER");
        assert(searchResults.size() == 1);
        assert(searchResults[0].id == profile.id);
    }

    {
        ProfileRepository repository(filePath.wstring());
        assert(repository.profiles().size() == 1);
        assert(repository.profiles()[0].name == L"server-a");
        assert(repository.profiles()[0].password == L"secret");
    }

    {
        cJSON *root = parseJsonFile(filePath);
        assert(root && cJSON_IsObject(root));
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "portableMode")));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        assert(cJSON_GetArraySize(profiles) == 1);
        cJSON *profile = cJSON_GetArrayItem(profiles, 0);
        assert(profile && cJSON_IsObject(profile));
        assert(cJSON_GetObjectItemCaseSensitive(profile, "passwordProtected"));
        assert(!cJSON_GetObjectItemCaseSensitive(profile, "passwordPortable"));
        assert(!cJSON_GetObjectItemCaseSensitive(profile, "password"));
        cJSON_Delete(root);
    }

    {
        const std::filesystem::path legacyPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryLegacyTests-" + createGuidString() + ".json");
        std::filesystem::remove(legacyPath);

        writeTextFile(legacyPath,
                      "[{\"id\":\"legacy\",\"name\":\"legacy\",\"host\":\"10.0.0.9\",\"port\":3389,"
                      "\"username\":\"bob\",\"password\":\"plain-secret\",\"domain\":\"\","
                      "\"clipboardEnabled\":true,\"ignoreCertificate\":true,\"fullScreenOnConnect\":false}]");

        ProfileRepository repository(legacyPath.wstring());
        assert(repository.profiles().empty());

        std::filesystem::remove(legacyPath);
    }

    {
        PasswordProtection::setMode(PasswordProtection::Mode::Portable);
        assert(PasswordProtection::isReady());

        const std::filesystem::path portablePath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryPortableTests-" + createGuidString() + ".json");
        std::filesystem::remove(portablePath);

        {
            ProfileRepository repository(portablePath.wstring());
            Profile profile = Profile::create();
            profile.name = L"portable";
            profile.host = L"10.0.0.10";
            profile.password = L"portable-secret";
            repository.addProfile(profile);
        }

        cJSON *root = parseJsonFile(portablePath);
        assert(root && cJSON_IsObject(root));
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "portableMode")));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        assert(cJSON_GetArraySize(profiles) == 1);
        cJSON *profile = cJSON_GetArrayItem(profiles, 0);
        assert(profile && cJSON_IsObject(profile));
        assert(cJSON_GetObjectItemCaseSensitive(profile, "passwordPortable"));
        assert(!cJSON_GetObjectItemCaseSensitive(profile, "passwordProtected"));
        cJSON_Delete(root);

        {
            ProfileRepository repository(portablePath.wstring());
            assert(repository.profiles().size() == 1);
            assert(repository.profiles()[0].password == L"portable-secret");
        }

        std::filesystem::remove(portablePath);
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    }

    std::filesystem::remove(filePath);
    return 0;
}
