#include "ProfileRepository.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/AppPaths.h"
#include "common/PasswordProtection.h"
#include "common/Win32String.h"

void to_json(nlohmann::json &j, const WindowState &s)
{
    j = nlohmann::json{
        {"leftRatio", s.leftRatio}, {"topRatio", s.topRatio},
        {"widthRatio", s.widthRatio}, {"heightRatio", s.heightRatio},
        {"monitorDeviceName", utf8FromWide(s.monitorDeviceName)},
        {"showCmd", s.showCmd},
    };
}

void from_json(const nlohmann::json &j, WindowState &s)
{
    s.leftRatio = j.value("leftRatio", 0.0);
    s.topRatio = j.value("topRatio", 0.0);
    s.widthRatio = j.value("widthRatio", 0.0);
    s.heightRatio = j.value("heightRatio", 0.0);
    s.monitorDeviceName = wideFromUtf8(j.value("monitorDeviceName", ""));
    s.showCmd = j.value("showCmd", 1);
    s.valid = j.contains("leftRatio")
        && j.contains("topRatio")
        && j.contains("widthRatio")
        && j.contains("heightRatio");
}

void to_json(nlohmann::json &j, const Profile &p)
{
    j = nlohmann::json{
        {"name", utf8FromWide(p.name)},
        {"host", utf8FromWide(p.host)},
        {"port", p.port},
        {"username", utf8FromWide(p.username)},
        {"domain", utf8FromWide(p.domain)},
        {"clipboardEnabled", p.clipboardEnabled},
        {"ignoreCertificate", p.ignoreCertificate},
        {"fullScreenOnConnect", p.fullScreenOnConnect},
        {"lastConnectedAt", p.lastConnectedAt},
    };
    if (PasswordProtection::mode() == PasswordProtection::Mode::Portable)
        j["passwordPortable"] = PasswordProtection::protectPortable(p.password);
    else
        j["passwordProtected"] = PasswordProtection::protectDpapi(p.password);
}

void from_json(const nlohmann::json &j, Profile &p)
{
    p.name = wideFromUtf8(j.value("name", ""));
    p.host = wideFromUtf8(j.value("host", ""));
    p.port = j.value("port", 3389);
    p.username = wideFromUtf8(j.value("username", ""));
    p.domain = wideFromUtf8(j.value("domain", ""));
    // Clipboard redirection and cert-ignore are no longer user options
    // (removed from both UIs); always on, stored values overridden.
    p.clipboardEnabled = true;
    p.ignoreCertificate = true;
    p.fullScreenOnConnect = j.value("fullScreenOnConnect", false);
    p.lastConnectedAt = j.value("lastConnectedAt", "");

    if (j.contains("passwordPortable") && j["passwordPortable"].is_string()) {
        bool ok = false;
        p.password = PasswordProtection::unprotectPortable(j["passwordPortable"].get_ref<const std::string &>(), &ok);
        if (!ok)
            p.password.clear();
    } else if (j.contains("passwordProtected") && j["passwordProtected"].is_string()) {
        bool ok = false;
        p.password = PasswordProtection::unprotectDpapi(j["passwordProtected"].get_ref<const std::string &>(), &ok);
        if (!ok)
            p.password.clear();
    } else {
        p.password = wideFromUtf8(j.value("password", ""));
    }
}

namespace
{
nlohmann::json loadRepositoryRoot(const std::wstring &filePath)
{
    const std::string contents = AppPaths::readFileContent(filePath);
    if (contents.empty())
        return nlohmann::json::object();

    nlohmann::json root = nlohmann::json::parse(contents, nullptr, false);
    if (!root.is_object())
        return nlohmann::json::object();

    return root;
}

bool saveRepositoryRoot(const std::wstring &filePath, const nlohmann::json &root)
{
    // A corrupt/truncated file the user could recover by hand must not be
    // silently overwritten by the next save: park it as .bad first.
    const std::string existing = AppPaths::readFileContent(filePath);
    if (!existing.empty()) {
        const nlohmann::json parsed = nlohmann::json::parse(existing, nullptr, false);
        if (!parsed.is_object()) {
            const std::wstring badPath = filePath + L".bad";
            std::error_code ec;
            std::filesystem::remove(badPath, ec);
            std::filesystem::rename(filePath, badPath, ec);
        }
    }
    return AppPaths::writeFileContent(filePath, root.dump(4));
}

// A mode that was never prepared has no key, so a non-empty password seals to an
// empty string: the write "succeeds" and the secret is silently gone. Detect it
// from the in-memory profiles (the json never holds a plaintext key, so it
// cannot answer this) and report it instead of quietly prompting next time.
bool anyPasswordDropped(const std::vector<Profile> &profiles)
{
    if (PasswordProtection::isReady())
        return false;

    return std::any_of(profiles.begin(), profiles.end(), [](const Profile &profile) {
        return !profile.password.empty();
    });
}

bool containsInsensitive(const std::wstring &haystack, const std::wstring &needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > haystack.size())
        return false;

    const std::wstring_view haystackView(haystack);
    const std::wstring_view needleView(needle);
    for (size_t offset = 0; offset + needleView.size() <= haystackView.size(); ++offset) {
        bool equal = true;
        for (size_t i = 0; i < needleView.size(); ++i) {
            if (std::towlower(haystackView[offset + i]) != std::towlower(needleView[i])) {
                equal = false;
                break;
            }
        }
        if (equal)
            return true;
    }
    return false;
}

bool equalsInsensitive(const std::wstring &left, const std::wstring &right)
{
    if (left.size() != right.size())
        return false;

    for (size_t i = 0; i < left.size(); ++i) {
        if (std::towlower(left[i]) != std::towlower(right[i]))
            return false;
    }
    return true;
}

std::wstring trimWhitespace(std::wstring value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    }).base();

    if (first >= last)
        return {};

    return std::wstring(first, last);
}

std::wstring uniqueProfileName(const std::wstring &preferredName, const std::vector<Profile> &profiles)
{
    const std::wstring baseName = trimWhitespace(preferredName.empty() ? L"(unnamed)" : preferredName);
    if (baseName.empty())
        return L"(unnamed)";

    auto nameExists = [&](const std::wstring &candidate) {
        return std::any_of(profiles.begin(), profiles.end(), [&](const Profile &profile) {
            return equalsInsensitive(profile.name, candidate);
        });
    };

    if (!nameExists(baseName))
        return baseName;

    for (int suffix = 2;; ++suffix) {
        std::wstring candidate = baseName + L" (" + std::to_wstring(suffix) + L")";
        if (!nameExists(candidate))
            return candidate;
    }
}
std::vector<Profile> parseProfiles(const nlohmann::json &root)
{
    std::vector<Profile> result;
    const auto it = root.find("profiles");
    if (it == root.end() || !it->is_array())
        return result;

    for (const auto &item : *it) {
        if (!item.is_object())
            continue;
        try {
            result.push_back(item.get<Profile>());
        } catch (const nlohmann::json::exception &) {
        }
    }
    return result;
}

// Structural equality with volatile fields ignored: lastConnectedAt moves on
// every connect and re-sealed password ciphertext differs on every write, so
// neither may decide whether profiles.json gets rewritten.
bool sameProfilesIgnoringVolatile(const std::vector<Profile> &left, const std::vector<Profile> &right)
{
    if (left.size() != right.size())
        return false;

    for (std::size_t i = 0; i < left.size(); ++i) {
        const Profile &a = left[i];
        const Profile &b = right[i];
        if (a.name != b.name || a.host != b.host || a.port != b.port
            || a.username != b.username || a.domain != b.domain || a.password != b.password
            || a.fullScreenOnConnect != b.fullScreenOnConnect)
            return false;
    }
    return true;
}
}

ProfileRepository::ProfileRepository(std::wstring filePath, std::wstring stateFilePath)
    : m_filePath(std::move(filePath))
    , m_stateFilePath(stateFilePath.empty() ? AppPaths::windowStateFilePath() : std::move(stateFilePath))
{
    load();
}
const std::vector<Profile> &ProfileRepository::profiles() const
{
    return m_profiles;
}

Profile ProfileRepository::profileByName(const std::wstring &name) const
{
    const auto it = std::find_if(m_profiles.begin(), m_profiles.end(),
        [&](const Profile &profile) { return equalsInsensitive(profile.name, name); });
    return (it != m_profiles.end()) ? *it : Profile{};
}

bool ProfileRepository::addProfile(const Profile &profile)
{
    Profile stored = profile;
    stored.name = trimWhitespace(stored.name);
    if (!stored.isValid() || containsName(stored.name))
        return false;

    m_profiles.push_back(stored);
    save();
    return true;
}

bool ProfileRepository::updateProfile(const std::wstring &currentName, const Profile &profile)
{
    const std::wstring trimmedCurrentName = trimWhitespace(currentName);
    Profile updated = profile;
    updated.name = trimWhitespace(updated.name);
    if (!updated.isValid() || containsName(updated.name, &trimmedCurrentName))
        return false;

    for (auto &item : m_profiles) {
        if (equalsInsensitive(item.name, trimmedCurrentName)) {
            item = updated;
            save();
            return true;
        }
    }
    return false;
}

bool ProfileRepository::removeProfile(const std::wstring &name)
{
    const auto before = m_profiles.size();
    m_profiles.erase(std::remove_if(m_profiles.begin(), m_profiles.end(),
        [&](const Profile &profile) { return equalsInsensitive(profile.name, name); }), m_profiles.end());
    if (m_profiles.size() == before)
        return false;
    save();
    return true;
}

bool ProfileRepository::moveProfile(const std::wstring &name, std::size_t targetIndex)
{
    const auto it = std::find_if(m_profiles.begin(), m_profiles.end(), [&](const Profile &profile) {
        return equalsInsensitive(profile.name, name);
    });
    if (it == m_profiles.end())
        return false;

    const std::size_t sourceIndex = static_cast<std::size_t>(std::distance(m_profiles.begin(), it));
    const std::size_t clampedTargetIndex = std::min(targetIndex, m_profiles.size());
    if (sourceIndex == clampedTargetIndex || sourceIndex + 1 == clampedTargetIndex)
        return false;

    if (sourceIndex < clampedTargetIndex) {
        std::rotate(m_profiles.begin() + static_cast<std::ptrdiff_t>(sourceIndex),
                    m_profiles.begin() + static_cast<std::ptrdiff_t>(sourceIndex + 1),
                    m_profiles.begin() + static_cast<std::ptrdiff_t>(clampedTargetIndex));
    } else {
        std::rotate(m_profiles.begin() + static_cast<std::ptrdiff_t>(clampedTargetIndex),
                    m_profiles.begin() + static_cast<std::ptrdiff_t>(sourceIndex),
                    m_profiles.begin() + static_cast<std::ptrdiff_t>(sourceIndex + 1));
    }

    save();
    return true;
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
    const nlohmann::json root = loadRepositoryRoot(m_filePath);
    const auto it = root.find("profiles");
    if (it == root.end() || !it->is_array())
        return;

    m_profiles.clear();
    for (const auto &item : *it) {
        if (!item.is_object())
            continue;

        Profile profile;
        try {
            profile = item.get<Profile>();
        } catch (const nlohmann::json::exception &) {
            continue; // malformed entry must not crash startup; keep the rest
        }
        profile.name = trimWhitespace(profile.name);
        if (profile.name.empty()) {
            const std::wstring legacyId = trimWhitespace(wideFromUtf8(item.value("id", "")));
            profile.name = !legacyId.empty()
                ? legacyId
                : trimWhitespace(profile.host);
        }

        if (profile.host.empty())
            continue;

        profile.name = uniqueProfileName(profile.name, m_profiles);
        m_profiles.push_back(std::move(profile));
    }
}

bool ProfileRepository::save() const
{
    const nlohmann::json previous = loadRepositoryRoot(m_filePath);
    nlohmann::json root = previous;
    root["portableMode"] = PasswordProtection::mode() == PasswordProtection::Mode::Portable;
    root["profiles"] = m_profiles;
    root.erase("windowState"); // lives in the machine-local state file; strip legacy copies once

    // profiles.json may sit in a synced folder (portable mode on OneDrive):
    // rewrite only when the profiles really changed, never for volatile churn,
    // which is what bred a sync-conflict copy on nearly every exit.
    if (!previous.contains("windowState")
        && previous.value("portableMode", false) == root["portableMode"].get<bool>()
        && sameProfilesIgnoringVolatile(parseProfiles(previous), m_profiles)) {
        m_lastSaveFailed = false;
        m_unpersistedChange = anyPasswordDropped(m_profiles);
        return true;
    }

    m_lastSaveFailed = !saveRepositoryRoot(m_filePath, root);
    m_unpersistedChange = m_lastSaveFailed || anyPasswordDropped(m_profiles);
    return !m_lastSaveFailed;
}

bool ProfileRepository::saveFailed() const
{
    return m_lastSaveFailed;
}

bool ProfileRepository::persistWarningNeeded() const
{
    return m_unpersistedChange;
}

WindowState ProfileRepository::loadWindowState() const
{
    const std::string contents = AppPaths::readFileContent(m_stateFilePath);
    if (!contents.empty()) {
        try {
            const nlohmann::json state = nlohmann::json::parse(contents, nullptr, false);
            if (state.is_object())
                return state.get<WindowState>();
        } catch (...) {
        }
    }

    // Legacy layout kept windowState inside profiles.json; fall back to it so
    // an upgrade keeps its geometry until the first local save.
    const nlohmann::json root = loadRepositoryRoot(m_filePath);
    if (!root.is_object() || !root.contains("windowState"))
        return {};

    try {
        return root["windowState"].get<WindowState>();
    } catch (...) {
        return {};
    }
}

void ProfileRepository::saveWindowState(const WindowState &state) const
{
    // Geometry is per-machine (monitor layout): write the local state file
    // only, never profiles.json, which may live in a synced folder.
    if (m_stateFilePath.empty())
        return;

    AppPaths::writeFileContent(m_stateFilePath, nlohmann::json(state).dump(4));
}

bool ProfileRepository::containsName(const std::wstring &name, const std::wstring *excludedName) const
{
    return std::any_of(m_profiles.begin(), m_profiles.end(), [&](const Profile &profile) {
        if (excludedName && equalsInsensitive(profile.name, *excludedName))
            return false;
        return equalsInsensitive(profile.name, name);
    });
}
