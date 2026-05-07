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
    Profile profileById(const std::string &id) const;
    void addProfile(const Profile &profile);
    void updateProfile(const Profile &profile);
    void removeProfile(const std::string &id);
    std::vector<Profile> search(const std::wstring &query) const;

    WindowState loadWindowState() const;
    void saveWindowState(const WindowState &state) const;

private:
    void load();
    void save() const;

    std::wstring m_filePath;
    std::vector<Profile> m_profiles;
};
