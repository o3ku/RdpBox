#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <cjson/cJSON.h>

#include "common/PasswordProtection.h"
#include "common/Win32String.h"
#include "common/profiles/ProfileRepository.h"

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

        const std::filesystem::path statePath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryStateTests-" + createGuidString() + ".json");
        std::filesystem::remove(statePath);

        {
            ProfileRepository repository(portablePath.wstring(), statePath.wstring());
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
        // Window geometry moved to the machine-local state file: profiles.json
        // (which may sit in a synced folder) must not carry it anymore.
        assert(!cJSON_GetObjectItemCaseSensitive(root, "windowState"));
        cJSON_Delete(root);

        cJSON *stateRoot = parseJsonFile(statePath);
        assert(stateRoot && cJSON_IsObject(stateRoot));
        const cJSON *leftRatio = cJSON_GetObjectItemCaseSensitive(stateRoot, "leftRatio");
        assert(leftRatio && cJSON_IsNumber(leftRatio) && leftRatio->valuedouble == 0.1);
        cJSON_Delete(stateRoot);

        {
            ProfileRepository repository(portablePath.wstring(), statePath.wstring());
            assert(repository.profileByName(L"portable").password == L"portable-secret");
            const WindowState restored = repository.loadWindowState();
            assert(restored.valid);
            assert(restored.monitorDeviceName == L"\\\\.\\DISPLAY1");
        }

        std::filesystem::remove(portablePath);
        std::filesystem::remove(statePath);
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    }

    // A prepared mode stores the secret and stays quiet.
    {
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
        assert(PasswordProtection::isReady());
        std::filesystem::remove(filePath);

        ProfileRepository repository(filePath.wstring());
        Profile profile = Profile::create();
        profile.name = L"with-password-mode";
        profile.host = L"10.0.0.10";
        profile.password = L"stored-secret";
        assert(repository.addProfile(profile));
        assert(!repository.saveFailed());
        assert(!repository.persistWarningNeeded());

        cJSON *root = parseJsonFile(filePath);
        assert(root && cJSON_IsObject(root));
        const cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        const cJSON *stored = profiles ? cJSON_GetArrayItem(profiles, 0) : nullptr;
        assert(stored && cJSON_IsObject(stored));
        assert(cJSON_GetObjectItemCaseSensitive(stored, "passwordProtected"));
        cJSON_Delete(root);

        assert(ProfileRepository(filePath.wstring()).profileByName(L"with-password-mode").password
               == L"stored-secret");
    }


    {
        // Legacy layout: windowState inside profiles.json is the fallback
        // until a machine-local state file exists (first run after upgrade).
        const std::filesystem::path legacyProfiles =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryLegacyWindowStateTests-" + createGuidString() + ".json");
        const std::filesystem::path missingState =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryMissingStateTests-" + createGuidString() + ".json");
        writeTextFile(legacyProfiles,
                      "{\"portableMode\":true,\"profiles\":[],\"windowState\":{"
                      "\"leftRatio\":0.5,\"topRatio\":0.25,\"widthRatio\":0.3,\"heightRatio\":0.4,"
                      "\"monitorDeviceName\":\"legacy\",\"showCmd\":3}}");

        ProfileRepository repository(legacyProfiles.wstring(), missingState.wstring());
        const WindowState legacy = repository.loadWindowState();
        assert(legacy.valid);
        assert(legacy.leftRatio == 0.5);
        assert(legacy.showCmd == 3);

        std::filesystem::remove(legacyProfiles);
    }

    {
        // Connect-only churn (lastConnectedAt) must not rewrite profiles.json:
        // a synced portable folder conflicts on every connect otherwise.
        PasswordProtection::setMode(PasswordProtection::Mode::Portable);
        assert(PasswordProtection::isReady());

        const std::filesystem::path churnPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryChurnTests-" + createGuidString() + ".json");
        std::filesystem::remove(churnPath);

        ProfileRepository repository(churnPath.wstring());
        Profile profile = Profile::create();
        profile.name = L"churn";
        profile.host = L"10.0.0.30";
        profile.password = L"churn-secret";
        assert(repository.addProfile(profile));

        const std::string afterAdd = readTextFile(churnPath);

        Profile connected = repository.profileByName(L"churn");
        connected.lastConnectedAt = "2026-10-27T03:00:00Z";
        assert(repository.updateProfile(L"churn", connected));
        assert(readTextFile(churnPath) == afterAdd); // volatile-only change: no rewrite

        Profile edited = repository.profileByName(L"churn");
        edited.port = 3390;
        assert(repository.updateProfile(L"churn", edited));
        const std::string afterEdit = readTextFile(churnPath);
        assert(afterEdit != afterAdd); // a real edit still lands

        cJSON *root = parseJsonFile(churnPath);
        cJSON *stored = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "profiles"), 0);
        const cJSON *port = cJSON_GetObjectItemCaseSensitive(stored, "port");
        assert(port && cJSON_IsNumber(port) && port->valueint == 3390);
        cJSON_Delete(root);

        std::filesystem::remove(churnPath);
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    }
    std::filesystem::remove(filePath);
    return 0;
}
