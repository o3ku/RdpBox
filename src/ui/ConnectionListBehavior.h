#pragma once

#include "profiles/Profile.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct ConnectionListButtonState
{
    bool editEnabled = false;
    bool deleteEnabled = false;
    bool duplicateEnabled = false;
    bool connectEnabled = false;
};

bool isProfileConnected(const std::wstring &profileName,
                        const std::vector<std::wstring> &connectedProfileNames);

std::wstring connectionListStatusText(
    const std::wstring &profileName,
    const std::vector<std::wstring> &connectedProfileNames);

ConnectionListButtonState connectionListButtonState(
    const std::vector<Profile> &visibleProfiles,
    const std::vector<int> &selectedIndices,
    const std::vector<std::wstring> &connectedProfileNames);

std::vector<std::wstring> connectableProfileNamesForSelection(
    const std::vector<Profile> &visibleProfiles,
    const std::vector<int> &selectedIndices,
    const std::vector<std::wstring> &connectedProfileNames);

bool shouldActivateConnectionListSelection(bool hasKeyboardModifiers, bool isEnterKey);

std::vector<int> retainedSelectionRowsForProfiles(
    const std::vector<Profile> &visibleProfiles,
    const std::vector<std::wstring> &preferredProfileNames);

Profile duplicateProfileDraft(const Profile &profile);

std::size_t repositoryTargetIndexForVisibleInsertIndex(
    const std::vector<Profile> &repositoryProfiles,
    const std::vector<Profile> &visibleProfiles,
    int insertIndex);

std::optional<int> targetSelectionIndex(int currentIndex, int itemCount, int delta);
