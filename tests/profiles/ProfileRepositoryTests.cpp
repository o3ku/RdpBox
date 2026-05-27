#include <cassert>
#include <filesystem>
#include <fstream>
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
        assert(repository.addProfile(profile));
        assert(!repository.addProfile(profile));

        const Profile stored = repository.profileByName(L"server-a");
        assert(stored.name == L"server-a");
        assert(stored.host == L"10.0.0.8");
        assert(stored.clipboardEnabled);

        const std::vector<Profile> searchResults = repository.search(L"SERVER");
        assert(searchResults.size() == 1);
        assert(searchResults[0].name == L"server-a");
    }

    {
        ProfileRepository repository(filePath.wstring());
        assert(repository.profiles().size() == 1);
        assert(repository.profiles()[0].name == L"server-a");
        assert(repository.profiles()[0].password == L"secret");

        Profile renamed = repository.profileByName(L"server-a");
        renamed.name = L"server-b";
        renamed.port = 3390;
        renamed.clipboardEnabled = false;
        renamed.ignoreCertificate = true;
        renamed.fullScreenOnConnect = true;
        renamed.lastConnectedAt = "2026-05-27T12:34:56Z";
        assert(repository.updateProfile(L"server-a", renamed));

        const Profile reloaded = repository.profileByName(L"server-b");
        assert(reloaded.name == L"server-b");
        assert(reloaded.port == 3390);
        assert(!reloaded.clipboardEnabled);
        assert(reloaded.ignoreCertificate);
        assert(reloaded.fullScreenOnConnect);
        assert(reloaded.lastConnectedAt == "2026-05-27T12:34:56Z");
        assert(!repository.profileByName(L"server-a").isValid());

        Profile conflict;
        conflict.name = L"server-c";
        conflict.host = L"10.0.0.9";
        assert(repository.addProfile(conflict));
        Profile third;
        third.name = L"server-d";
        third.host = L"10.0.0.10";
        assert(repository.addProfile(third));

        assert(repository.moveProfile(L"server-b", 3));
        assert(repository.profiles().size() == 3);
        assert(repository.profiles()[0].name == L"server-c");
        assert(repository.profiles()[1].name == L"server-d");
        assert(repository.profiles()[2].name == L"server-b");
        assert(repository.moveProfile(L"server-b", 0));
        assert(repository.profiles()[0].name == L"server-b");
        assert(repository.profiles()[1].name == L"server-c");
        assert(repository.profiles()[2].name == L"server-d");

        Profile duplicateName = repository.profileByName(L"server-b");
        duplicateName.name = L"server-c";
        assert(!repository.updateProfile(L"server-b", duplicateName));

        cJSON *root = parseJsonFile(filePath);
        assert(root && cJSON_IsObject(root));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        assert(cJSON_GetArraySize(profiles) == 3);
        cJSON *jsonProfile = cJSON_GetArrayItem(profiles, 0);
        assert(jsonProfile && cJSON_IsObject(jsonProfile));
        assert(!cJSON_GetObjectItemCaseSensitive(jsonProfile, "id"));
        assert(cJSON_GetObjectItemCaseSensitive(jsonProfile, "passwordProtected"));
        cJSON_Delete(root);

        assert(repository.removeProfile(L"server-c"));
        assert(!repository.removeProfile(L"does-not-exist"));
        assert(repository.profiles().size() == 2);
    }

    {
        const std::filesystem::path legacyPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryLegacyTests-" + createGuidString() + ".json");
        std::filesystem::remove(legacyPath);

        writeTextFile(legacyPath,
                      "{\"portableMode\":false,\"profiles\":["
                      "{\"id\":\"legacy-a\",\"host\":\"10.0.0.10\"},"
                      "{\"name\":\"duplicate\",\"host\":\"10.0.0.11\"},"
                      "{\"name\":\"duplicate\",\"host\":\"10.0.0.12\"},"
                      "{\"name\":\"quoted\",\"host\":\"10.0.0.13\",\"passwordProtected\":\"not-base64\"}"
                      "]}");

        ProfileRepository repository(legacyPath.wstring());
        assert(repository.profiles().size() == 4);
        assert(repository.profiles()[0].name == L"legacy-a");
        assert(repository.profiles()[1].name == L"duplicate");
        assert(repository.profiles()[2].name == L"duplicate (2)");
        assert(repository.profiles()[3].password.empty());

        std::filesystem::remove(legacyPath);
    }

    {
        const std::filesystem::path defaultsPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryDefaultsTests-" + createGuidString() + ".json");
        std::filesystem::remove(defaultsPath);

        writeTextFile(defaultsPath,
                      "{\"portableMode\":false,\"profiles\":[{\"id\":\"defaults\",\"host\":\"10.0.0.15\"}]}");

        ProfileRepository repository(defaultsPath.wstring());
        assert(repository.profiles().size() == 1);
        const Profile &profile = repository.profiles()[0];
        assert(profile.name == L"defaults");
        assert(profile.host == L"10.0.0.15");
        assert(profile.port == 3389);
        assert(profile.username.empty());
        assert(profile.password.empty());
        assert(profile.domain.empty());
        assert(profile.clipboardEnabled);
        assert(profile.ignoreCertificate);
        assert(!profile.fullScreenOnConnect);
        assert(profile.lastConnectedAt.empty());

        std::filesystem::remove(defaultsPath);
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
            profile.host = L"10.0.0.20";
            profile.password = L"portable-secret";
            assert(repository.addProfile(profile));

            WindowState state;
            state.leftRatio = 0.1;
            state.topRatio = 0.2;
            state.widthRatio = 0.3;
            state.heightRatio = 0.4;
            state.monitorDeviceName = L"\\\\.\\DISPLAY1";
            state.showCmd = 3;
            state.valid = true;
            repository.saveWindowState(state);
        }

        cJSON *root = parseJsonFile(portablePath);
        assert(root && cJSON_IsObject(root));
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "portableMode")));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        cJSON *profile = cJSON_GetArrayItem(profiles, 0);
        assert(profile && cJSON_IsObject(profile));
        assert(cJSON_GetObjectItemCaseSensitive(profile, "passwordPortable"));
        assert(!cJSON_GetObjectItemCaseSensitive(profile, "passwordProtected"));
        cJSON *windowState = cJSON_GetObjectItemCaseSensitive(root, "windowState");
        assert(windowState && cJSON_IsObject(windowState));
        cJSON_Delete(root);

        {
            ProfileRepository repository(portablePath.wstring());
            assert(repository.profileByName(L"portable").password == L"portable-secret");
            const WindowState restored = repository.loadWindowState();
            assert(restored.valid);
            assert(restored.monitorDeviceName == L"\\\\.\\DISPLAY1");
        }

        std::filesystem::remove(portablePath);
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    }

    std::filesystem::remove(filePath);
    return 0;
}
