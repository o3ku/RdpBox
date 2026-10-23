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

    // True when the most recent save() failed to write the file (disk full,
    // lock, permission). Lets callers warn without conflating with the
    // mutators' logical false returns (name collision, not found).
    bool saveFailed() const;

    // True when a mutator already reported success but its save() could not be
    // persisted, or when password protection was not prepared so stored and
    // in-memory profiles disagree. UIs use this for a one-shot visible warning.
    bool persistWarningNeeded() const;

private:
    void load();
    bool save() const;
    bool containsName(const std::wstring &name, const std::wstring *excludedName = nullptr) const;

    std::wstring m_filePath;
    std::vector<Profile> m_profiles;
    mutable bool m_lastSaveFailed = false;
    mutable bool m_unpersistedChange = false;
};
