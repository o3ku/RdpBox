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
        ProfileRepository repository(filePath.wstring());

        Profile generatedIdProfile;
        generatedIdProfile.name = L"generated-id";
        generatedIdProfile.host = L"10.0.0.12";
        repository.addProfile(generatedIdProfile);

        assert(repository.profiles().size() == 2);
        const Profile &stored = repository.profiles()[1];
        assert(!stored.id.empty());
        assert(stored.name == L"generated-id");
        assert(stored.host == L"10.0.0.12");

        Profile updated = stored;
        updated.name = L"generated-id-updated";
        updated.port = 3390;
        updated.clipboardEnabled = false;
        updated.ignoreCertificate = true;
        updated.fullScreenOnConnect = true;
        updated.lastConnectedAt = "2026-05-27T12:34:56Z";
        repository.updateProfile(updated);

        const Profile reloaded = repository.profileById(updated.id);
        assert(reloaded.name == L"generated-id-updated");
        assert(reloaded.port == 3390);
        assert(!reloaded.clipboardEnabled);
        assert(reloaded.ignoreCertificate);
        assert(reloaded.fullScreenOnConnect);
        assert(reloaded.lastConnectedAt == "2026-05-27T12:34:56Z");

        cJSON *root = parseJsonFile(filePath);
        assert(root && cJSON_IsObject(root));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        assert(cJSON_GetArraySize(profiles) == 2);

        bool foundUpdatedProfile = false;
        for (int i = 0; i < cJSON_GetArraySize(profiles); ++i) {
            cJSON *jsonProfile = cJSON_GetArrayItem(profiles, i);
            cJSON *id = cJSON_GetObjectItemCaseSensitive(jsonProfile, "id");
            if (!id || !cJSON_IsString(id) || updated.id != id->valuestring)
                continue;

            foundUpdatedProfile = true;
            cJSON *port = cJSON_GetObjectItemCaseSensitive(jsonProfile, "port");
            cJSON *clipboardEnabled = cJSON_GetObjectItemCaseSensitive(jsonProfile, "clipboardEnabled");
            cJSON *ignoreCertificate = cJSON_GetObjectItemCaseSensitive(jsonProfile, "ignoreCertificate");
            cJSON *fullScreenOnConnect = cJSON_GetObjectItemCaseSensitive(jsonProfile, "fullScreenOnConnect");
            cJSON *lastConnectedAt = cJSON_GetObjectItemCaseSensitive(jsonProfile, "lastConnectedAt");
            assert(port && cJSON_IsNumber(port) && port->valueint == 3390);
            assert(clipboardEnabled && cJSON_IsFalse(clipboardEnabled));
            assert(ignoreCertificate && cJSON_IsTrue(ignoreCertificate));
            assert(fullScreenOnConnect && cJSON_IsTrue(fullScreenOnConnect));
            assert(lastConnectedAt && cJSON_IsString(lastConnectedAt));
            assert(std::string(lastConnectedAt->valuestring) == "2026-05-27T12:34:56Z");
        }

        assert(foundUpdatedProfile);
        cJSON_Delete(root);

        repository.removeProfile(updated.id);
        assert(repository.profiles().size() == 1);
        assert(!repository.profileById(updated.id).isValid());

        Profile missing;
        missing.id = "does-not-exist";
        missing.name = L"missing";
        repository.updateProfile(missing);
        assert(repository.profiles().size() == 1);

        repository.removeProfile("does-not-exist");
        assert(repository.profiles().size() == 1);
    }

    {
        const std::filesystem::path searchPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositorySearchTests-" + createGuidString() + ".json");
        std::filesystem::remove(searchPath);

        ProfileRepository repository(searchPath.wstring());

        Profile alpha = Profile::create();
        alpha.name = L"Alpha Server";
        alpha.host = L"rdp-alpha.internal";
        alpha.domain = L"DomainA";
        repository.addProfile(alpha);

        Profile beta = Profile::create();
        beta.name = L"Beta Box";
        beta.host = L"10.20.30.40";
        beta.domain = L"DomainB";
        repository.addProfile(beta);

        const std::vector<Profile> allResults = repository.search(L"");
        assert(allResults.size() == 2);

        const std::vector<Profile> nameResults = repository.search(L"alpha");
        assert(nameResults.size() == 1);
        assert(nameResults[0].id == alpha.id);

        const std::vector<Profile> hostResults = repository.search(L"10.20");
        assert(hostResults.size() == 1);
        assert(hostResults[0].id == beta.id);

        const std::vector<Profile> missingResults = repository.search(L"does-not-match");
        assert(missingResults.empty());

        const std::vector<Profile> domainResults = repository.search(L"domaina");
        assert(domainResults.empty());

        std::filesystem::remove(searchPath);
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
        ProfileRepository repository(filePath.wstring());

        WindowState state;
        state.leftRatio = 0.125;
        state.topRatio = 0.25;
        state.widthRatio = 0.5;
        state.heightRatio = 0.75;
        state.monitorDeviceName = L"\\\\.\\DISPLAY7";
        state.showCmd = 3;
        state.valid = true;
        repository.saveWindowState(state);

        const WindowState stored = repository.loadWindowState();
        assert(stored.valid);
        assert(stored.leftRatio == 0.125);
        assert(stored.topRatio == 0.25);
        assert(stored.widthRatio == 0.5);
        assert(stored.heightRatio == 0.75);
        assert(stored.monitorDeviceName == L"\\\\.\\DISPLAY7");
        assert(stored.showCmd == 3);

        cJSON *root = parseJsonFile(filePath);
        assert(root && cJSON_IsObject(root));
        cJSON *windowState = cJSON_GetObjectItemCaseSensitive(root, "windowState");
        assert(windowState && cJSON_IsObject(windowState));
        assert(cJSON_GetObjectItemCaseSensitive(windowState, "leftRatio"));
        assert(cJSON_GetObjectItemCaseSensitive(windowState, "topRatio"));
        assert(cJSON_GetObjectItemCaseSensitive(windowState, "widthRatio"));
        assert(cJSON_GetObjectItemCaseSensitive(windowState, "heightRatio"));
        cJSON *monitorDeviceName = cJSON_GetObjectItemCaseSensitive(windowState, "monitorDeviceName");
        assert(monitorDeviceName && cJSON_IsString(monitorDeviceName));
        assert(std::string(monitorDeviceName->valuestring) == "\\\\.\\DISPLAY7");
        assert(!cJSON_GetObjectItemCaseSensitive(windowState, "left"));
        assert(!cJSON_GetObjectItemCaseSensitive(windowState, "top"));
        assert(!cJSON_GetObjectItemCaseSensitive(windowState, "right"));
        assert(!cJSON_GetObjectItemCaseSensitive(windowState, "bottom"));
        cJSON_Delete(root);
    }

    {
        ProfileRepository repository(filePath.wstring());

        WindowState state;
        state.leftRatio = 0.33;
        state.topRatio = 0.44;
        state.widthRatio = 0.55;
        state.heightRatio = 0.66;
        state.monitorDeviceName = L"\\\\.\\DISPLAY3";
        state.showCmd = 1;
        state.valid = true;
        repository.saveWindowState(state);

        const WindowState stored = repository.loadWindowState();
        assert(stored.valid);
        assert(stored.leftRatio == 0.33);
        assert(stored.topRatio == 0.44);
        assert(stored.widthRatio == 0.55);
        assert(stored.heightRatio == 0.66);
        assert(stored.monitorDeviceName == L"\\\\.\\DISPLAY3");
        assert(stored.showCmd == 1);

        cJSON *root = parseJsonFile(filePath);
        assert(root && cJSON_IsObject(root));
        cJSON *windowState = cJSON_GetObjectItemCaseSensitive(root, "windowState");
        assert(windowState && cJSON_IsObject(windowState));
        cJSON *monitorDeviceName = cJSON_GetObjectItemCaseSensitive(windowState, "monitorDeviceName");
        assert(monitorDeviceName && cJSON_IsString(monitorDeviceName));
        assert(std::string(monitorDeviceName->valuestring) == "\\\\.\\DISPLAY3");
        cJSON *leftRatio = cJSON_GetObjectItemCaseSensitive(windowState, "leftRatio");
        assert(leftRatio && cJSON_IsNumber(leftRatio));
        assert(leftRatio->valuedouble == 0.33);
        cJSON_Delete(root);
    }

    {
        const std::filesystem::path mixedStatePath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryWindowStatePersistenceTests-" + createGuidString() + ".json");
        std::filesystem::remove(mixedStatePath);

        ProfileRepository repository(mixedStatePath.wstring());

        WindowState state;
        state.leftRatio = 0.2;
        state.topRatio = 0.3;
        state.widthRatio = 0.4;
        state.heightRatio = 0.5;
        state.monitorDeviceName = L"\\\\.\\DISPLAY9";
        state.showCmd = 1;
        state.valid = true;
        repository.saveWindowState(state);

        Profile profile = Profile::create();
        profile.name = L"state-preserved";
        profile.host = L"10.0.0.11";
        repository.addProfile(profile);

        const WindowState stored = repository.loadWindowState();
        assert(stored.valid);
        assert(stored.leftRatio == 0.2);
        assert(stored.topRatio == 0.3);
        assert(stored.widthRatio == 0.4);
        assert(stored.heightRatio == 0.5);
        assert(stored.monitorDeviceName == L"\\\\.\\DISPLAY9");
        assert(stored.showCmd == 1);

        cJSON *root = parseJsonFile(mixedStatePath);
        assert(root && cJSON_IsObject(root));
        assert(cJSON_GetObjectItemCaseSensitive(root, "windowState"));
        cJSON_Delete(root);

        std::filesystem::remove(mixedStatePath);
    }

    {
        const std::filesystem::path mixedProfilesPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryProfilePersistenceTests-" + createGuidString() + ".json");
        std::filesystem::remove(mixedProfilesPath);

        ProfileRepository repository(mixedProfilesPath.wstring());

        Profile profile = Profile::create();
        profile.name = L"profile-preserved";
        profile.host = L"10.0.0.21";
        repository.addProfile(profile);

        WindowState state;
        state.leftRatio = 0.11;
        state.topRatio = 0.22;
        state.widthRatio = 0.33;
        state.heightRatio = 0.44;
        state.monitorDeviceName = L"\\\\.\\DISPLAY5";
        state.showCmd = 3;
        state.valid = true;
        repository.saveWindowState(state);

        Profile updated = repository.profileById(profile.id);
        updated.name = L"profile-updated";
        updated.host = L"10.0.0.22";
        repository.updateProfile(updated);

        const WindowState stored = repository.loadWindowState();
        assert(stored.valid);
        assert(stored.leftRatio == 0.11);
        assert(stored.topRatio == 0.22);
        assert(stored.widthRatio == 0.33);
        assert(stored.heightRatio == 0.44);
        assert(stored.monitorDeviceName == L"\\\\.\\DISPLAY5");
        assert(stored.showCmd == 3);

        cJSON *root = parseJsonFile(mixedProfilesPath);
        assert(root && cJSON_IsObject(root));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        assert(cJSON_GetArraySize(profiles) == 1);
        cJSON *windowState = cJSON_GetObjectItemCaseSensitive(root, "windowState");
        assert(windowState && cJSON_IsObject(windowState));
        cJSON_Delete(root);

        std::filesystem::remove(mixedProfilesPath);
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
        const std::filesystem::path defaultsPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryDefaultsTests-" + createGuidString() + ".json");
        std::filesystem::remove(defaultsPath);

        writeTextFile(defaultsPath,
                      "{\"portableMode\":false,\"profiles\":[{\"id\":\"defaults\","
                      "\"host\":\"10.0.0.15\"}]}");

        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
        ProfileRepository repository(defaultsPath.wstring());
        assert(repository.profiles().size() == 1);

        const Profile &profile = repository.profiles()[0];
        assert(profile.id == "defaults");
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
        const std::filesystem::path malformedProfilesPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryMalformedProfilesTests-" + createGuidString() + ".json");
        std::filesystem::remove(malformedProfilesPath);

        writeTextFile(malformedProfilesPath,
                      "{\"portableMode\":false,\"profiles\":[1,\"bad\",{\"id\":\"good\",\"host\":\"10.0.0.16\"}]}");

        ProfileRepository repository(malformedProfilesPath.wstring());
        assert(repository.profiles().size() == 1);
        assert(repository.profiles()[0].id == "good");
        assert(repository.profiles()[0].host == L"10.0.0.16");

        std::filesystem::remove(malformedProfilesPath);
    }

    {
        const std::filesystem::path corruptProtectedPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryCorruptProtectedTests-" + createGuidString() + ".json");
        std::filesystem::remove(corruptProtectedPath);

        writeTextFile(corruptProtectedPath,
                      "{\"portableMode\":false,\"profiles\":[{\"id\":\"corrupt-protected\","
                      "\"name\":\"corrupt\",\"host\":\"10.0.0.13\",\"passwordProtected\":\"not-base64\"}]}");

        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
        ProfileRepository repository(corruptProtectedPath.wstring());
        assert(repository.profiles().size() == 1);
        assert(repository.profiles()[0].id == "corrupt-protected");
        assert(repository.profiles()[0].password.empty());

        std::filesystem::remove(corruptProtectedPath);
    }

    {
        const std::filesystem::path corruptPortablePath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryCorruptPortableTests-" + createGuidString() + ".json");
        std::filesystem::remove(corruptPortablePath);

        writeTextFile(corruptPortablePath,
                      "{\"portableMode\":true,\"profiles\":[{\"id\":\"corrupt-portable\","
                      "\"name\":\"corrupt\",\"host\":\"10.0.0.14\",\"passwordPortable\":\"not-base64\"}]}");

        PasswordProtection::setMode(PasswordProtection::Mode::Portable);
        assert(PasswordProtection::isReady());

        ProfileRepository repository(corruptPortablePath.wstring());
        assert(repository.profiles().size() == 1);
        assert(repository.profiles()[0].id == "corrupt-portable");
        assert(repository.profiles()[0].password.empty());

        std::filesystem::remove(corruptPortablePath);
        PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    }

    {
        const std::filesystem::path invalidWindowStatePath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryInvalidWindowStateTests-" + createGuidString() + ".json");
        std::filesystem::remove(invalidWindowStatePath);

        writeTextFile(invalidWindowStatePath,
                      "{\"portableMode\":false,\"profiles\":[],\"windowState\":{\"leftRatio\":0.1}}");

        ProfileRepository repository(invalidWindowStatePath.wstring());
        const WindowState stored = repository.loadWindowState();
        assert(!stored.valid);

        std::filesystem::remove(invalidWindowStatePath);
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

    {
        const std::filesystem::path modeSwitchPath =
            std::filesystem::temp_directory_path()
            / wideFromUtf8("RdpBox-ProfileRepositoryModeSwitchTests-" + createGuidString() + ".json");
        std::filesystem::remove(modeSwitchPath);

        PasswordProtection::setMode(PasswordProtection::Mode::Portable);
        assert(PasswordProtection::isReady());

        Profile portableProfile = Profile::create();
        portableProfile.name = L"mode-switch";
        portableProfile.host = L"10.0.0.23";
        portableProfile.password = L"portable-first";

        {
            ProfileRepository repository(modeSwitchPath.wstring());
            repository.addProfile(portableProfile);

            WindowState state;
            state.leftRatio = 0.12;
            state.topRatio = 0.23;
            state.widthRatio = 0.34;
            state.heightRatio = 0.45;
            state.monitorDeviceName = L"\\\\.\\DISPLAY8";
            state.showCmd = 1;
            state.valid = true;
            repository.saveWindowState(state);

            PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
            Profile updated = repository.profileById(portableProfile.id);
            assert(updated.id == portableProfile.id);
            updated.password = L"dpapi-second";
            repository.updateProfile(updated);

            const WindowState stored = repository.loadWindowState();
            assert(stored.valid);
            assert(stored.leftRatio == 0.12);
            assert(stored.topRatio == 0.23);
            assert(stored.widthRatio == 0.34);
            assert(stored.heightRatio == 0.45);
            assert(stored.monitorDeviceName == L"\\\\.\\DISPLAY8");
        }

        cJSON *root = parseJsonFile(modeSwitchPath);
        assert(root && cJSON_IsObject(root));
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "portableMode")));
        cJSON *profiles = cJSON_GetObjectItemCaseSensitive(root, "profiles");
        assert(profiles && cJSON_IsArray(profiles));
        assert(cJSON_GetArraySize(profiles) == 1);
        cJSON *profile = cJSON_GetArrayItem(profiles, 0);
        assert(profile && cJSON_IsObject(profile));
        assert(cJSON_GetObjectItemCaseSensitive(profile, "passwordProtected"));
        assert(!cJSON_GetObjectItemCaseSensitive(profile, "passwordPortable"));
        cJSON *windowState = cJSON_GetObjectItemCaseSensitive(root, "windowState");
        assert(windowState && cJSON_IsObject(windowState));
        cJSON_Delete(root);

        {
            ProfileRepository repository(modeSwitchPath.wstring());
            assert(repository.profiles().size() == 1);
            assert(repository.profiles()[0].password == L"dpapi-second");
        }

        std::filesystem::remove(modeSwitchPath);
    }

    std::filesystem::remove(filePath);
    return 0;
}
