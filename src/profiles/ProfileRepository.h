#pragma once

#include <string>
#include <vector>

#include "Profile.h"

struct WindowState
{
    double leftRatio = 0.0;
    double topRatio = 0.0;
    double widthRatio = 0.0;
    double heightRatio = 0.0;
    std::wstring monitorDeviceName;
    int showCmd = 1; // SW_SHOWNORMAL
    bool valid = false;
};

class ProfileRepository
{
public:
    explicit ProfileRepository(std::wstring filePath);

    const std::vector<Profile> &profiles() const;
    Profile profileByName(const std::wstring &name) const;
    bool addProfile(const Profile &profile);
    bool updateProfile(const std::wstring &currentName, const Profile &profile);
    bool removeProfile(const std::wstring &name);
    bool moveProfile(const std::wstring &name, std::size_t targetIndex);
    std::vector<Profile> search(const std::wstring &query) const;

    WindowState loadWindowState() const;
    void saveWindowState(const WindowState &state) const;

private:
    void load();
    void save() const;
    bool containsName(const std::wstring &name, const std::wstring *excludedName = nullptr) const;

    std::wstring m_filePath;
    std::vector<Profile> m_profiles;
};
