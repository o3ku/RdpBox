#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "common/AppPaths.h"
#include "common/PasswordProtection.h"
#include "common/Win32String.h"
#include "profiles/ProfileRepository.h"

namespace
{
std::filesystem::path executableDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
    assert(length > 0 && length < std::size(modulePath));
    return std::filesystem::path(std::wstring(modulePath, length)).parent_path();
}

class ScopedPathBackup
{
public:
    explicit ScopedPathBackup(const std::filesystem::path &path)
        : m_original(path)
    {
        if (!std::filesystem::exists(m_original))
            return;

        m_backup = m_original;
        m_backup += L".backup-" + wideFromUtf8(createGuidString());
        std::filesystem::rename(m_original, m_backup);
        m_hasBackup = true;
    }

    ~ScopedPathBackup()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_original, ignored);
        if (m_hasBackup) {
            std::filesystem::rename(m_backup, m_original, ignored);
        }
    }

private:
    std::filesystem::path m_original;
    std::filesystem::path m_backup;
    bool m_hasBackup = false;
};

std::string readTextFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
}

int main()
{
    const std::filesystem::path exeDir = executableDirectory();
    const std::filesystem::path profilesPath = exeDir / "profiles.json";
    const std::filesystem::path frameCapturesPath = exeDir / "frame-captures";

    ScopedPathBackup profilesBackup(profilesPath);
    ScopedPathBackup frameCapturesBackup(frameCapturesPath);

    assert(AppPaths::enablePortableMode());
    PasswordProtection::setMode(PasswordProtection::Mode::Portable);
    assert(PasswordProtection::isReady());

    ProfileRepository repository(AppPaths::profilesFilePath());

    Profile profile = Profile::create();
    profile.name = L"smoke";
    profile.host = L"127.0.0.1";
    profile.username = L"tester";
    profile.password = L"portable-secret";
    repository.addProfile(profile);

    WindowState state;
    state.leftRatio = 0.1;
    state.topRatio = 0.2;
    state.widthRatio = 0.6;
    state.heightRatio = 0.7;
    state.monitorDeviceName = L"\\\\.\\DISPLAY1";
    state.showCmd = SW_SHOWMAXIMIZED;
    state.valid = true;
    repository.saveWindowState(state);

    ProfileRepository reloaded(AppPaths::profilesFilePath());
    assert(reloaded.profiles().size() == 1);
    assert(reloaded.profiles()[0].name == L"smoke");
    assert(reloaded.profiles()[0].host == L"127.0.0.1");
    assert(reloaded.profiles()[0].username == L"tester");
    assert(reloaded.profiles()[0].password == L"portable-secret");

    const WindowState restoredState = reloaded.loadWindowState();
    assert(restoredState.valid);
    assert(restoredState.leftRatio == 0.1);
    assert(restoredState.topRatio == 0.2);
    assert(restoredState.widthRatio == 0.6);
    assert(restoredState.heightRatio == 0.7);
    assert(restoredState.monitorDeviceName == L"\\\\.\\DISPLAY1");
    assert(restoredState.showCmd == SW_SHOWMAXIMIZED);

    assert(std::filesystem::exists(profilesPath));
    const nlohmann::json root = nlohmann::json::parse(readTextFile(profilesPath), nullptr, false);
    assert(root.is_object());
    assert(root.value("portableMode", false));
    assert(root.contains("profiles"));
    assert(root["profiles"].is_array());
    assert(root["profiles"].size() == 1);
    assert(root.contains("windowState"));

    const std::filesystem::path captureRoot = AppPaths::frameCaptureRootPath();
    assert(captureRoot == frameCapturesPath);
    assert(std::filesystem::exists(captureRoot));
    assert(std::filesystem::is_directory(captureRoot));

    PasswordProtection::setMode(PasswordProtection::Mode::Dpapi);
    return 0;
}
