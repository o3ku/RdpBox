#include "ProfileRepository.h"

#include <algorithm>
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
        {"id", p.id},
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
    p.id = j.value("id", "");
    p.name = wideFromUtf8(j.value("name", ""));
    p.host = wideFromUtf8(j.value("host", ""));
    p.port = j.value("port", 3389);
    p.username = wideFromUtf8(j.value("username", ""));
    p.domain = wideFromUtf8(j.value("domain", ""));
    p.clipboardEnabled = j.value("clipboardEnabled", true);
    p.ignoreCertificate = j.value("ignoreCertificate", true);
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
bool containsInsensitive(const std::wstring &haystack, const std::wstring &needle)
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
    const std::string contents = AppPaths::readFileContent(m_filePath);
    if (contents.empty())
        return;

    auto root = nlohmann::json::parse(contents, nullptr, false);
    if (!root.is_object())
        return;

    const auto it = root.find("profiles");
    if (it == root.end() || !it->is_array())
        return;

    m_profiles.clear();
    for (const auto &item : *it) {
        if (item.is_object())
            m_profiles.push_back(item.get<Profile>());
    }
}

void ProfileRepository::save() const
{
    nlohmann::json root;
    root["portableMode"] = PasswordProtection::mode() == PasswordProtection::Mode::Portable;
    root["profiles"] = m_profiles;

    AppPaths::writeFileContent(m_filePath, root.dump(4));
}

WindowState ProfileRepository::loadWindowState() const
{
    const std::string contents = AppPaths::readFileContent(m_filePath);
    if (contents.empty())
        return {};

    auto root = nlohmann::json::parse(contents, nullptr, false);
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
    const std::string contents = AppPaths::readFileContent(m_filePath);

    nlohmann::json root;
    if (!contents.empty()) {
        root = nlohmann::json::parse(contents, nullptr, false);
        if (!root.is_object())
            root = nlohmann::json::object();
    }

    root["windowState"] = state;
    AppPaths::writeFileContent(m_filePath, root.dump(4));
}
