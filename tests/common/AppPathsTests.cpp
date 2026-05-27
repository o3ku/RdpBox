#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "common/AppPaths.h"
#include "common/Win32String.h"

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
    assert(AppPaths::isPortableMode());
    assert(std::filesystem::path(AppPaths::dataRootPath()) == exeDir);
    assert(std::filesystem::path(AppPaths::profilesFilePath()) == profilesPath);

    assert(std::filesystem::exists(profilesPath));
    const nlohmann::json profilesJson = nlohmann::json::parse(readTextFile(profilesPath), nullptr, false);
    assert(profilesJson.is_object());
    assert(profilesJson.value("portableMode", false));
    assert(profilesJson.contains("profiles"));
    assert(profilesJson["profiles"].is_array());

    const std::filesystem::path frameCaptureRoot = AppPaths::frameCaptureRootPath();
    assert(frameCaptureRoot == frameCapturesPath);
    assert(std::filesystem::exists(frameCaptureRoot));
    assert(std::filesystem::is_directory(frameCaptureRoot));

    const std::filesystem::path roundTripPath = exeDir / wideFromUtf8("AppPathsTests-" + createGuidString() + ".txt");
    assert(AppPaths::writeFileContent(roundTripPath.wstring(), "portable-roundtrip"));
    assert(AppPaths::readFileContent(roundTripPath.wstring()) == "portable-roundtrip");
    assert(AppPaths::writeFileContent(roundTripPath.wstring(), "portable-overwrite"));
    assert(AppPaths::readFileContent(roundTripPath.wstring()) == "portable-overwrite");
    std::filesystem::remove(roundTripPath);

    return 0;
}
