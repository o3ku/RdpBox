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
