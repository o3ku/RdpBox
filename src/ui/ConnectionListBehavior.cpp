#include "ConnectionListBehavior.h"

#include <algorithm>

bool isProfileConnected(const std::wstring &profileName,
                        const std::vector<std::wstring> &connectedProfileNames)
{
    return std::find(connectedProfileNames.begin(), connectedProfileNames.end(), profileName)
        != connectedProfileNames.end();
}

ConnectionListButtonState connectionListButtonState(
    const std::vector<Profile> &visibleProfiles,
    const std::vector<int> &selectedIndices,
    const std::vector<std::wstring> &connectedProfileNames)
{
    ConnectionListButtonState state;
    const int selectedCount = static_cast<int>(selectedIndices.size());
    state.editEnabled = selectedCount == 1;
    state.deleteEnabled = selectedCount > 0;
    state.duplicateEnabled = selectedCount > 0;

    if (selectedCount <= 0)
        return state;

    bool allConnected = true;
    for (int index : selectedIndices) {
        if (index < 0 || index >= static_cast<int>(visibleProfiles.size()))
            continue;

        if (!isProfileConnected(visibleProfiles[static_cast<std::size_t>(index)].name,
                                connectedProfileNames)) {
            allConnected = false;
            break;
        }
    }

    state.connectEnabled = !allConnected;
    return state;
}

std::vector<std::wstring> connectableProfileNamesForSelection(
    const std::vector<Profile> &visibleProfiles,
    const std::vector<int> &selectedIndices,
    const std::vector<std::wstring> &connectedProfileNames)
{
    std::vector<std::wstring> names;
    for (int index : selectedIndices) {
        if (index < 0 || index >= static_cast<int>(visibleProfiles.size()))
            continue;

        const std::wstring &profileName = visibleProfiles[static_cast<std::size_t>(index)].name;
        if (!isProfileConnected(profileName, connectedProfileNames))
            names.push_back(profileName);
    }
    return names;
}

Profile duplicateProfileDraft(const Profile &profile)
{
    Profile duplicate = profile;
    if (!duplicate.name.empty())
        duplicate.name += L"(n)";
    else
        duplicate.name = L"(unnamed)";
    return duplicate;
}

std::size_t repositoryTargetIndexForVisibleInsertIndex(
    const std::vector<Profile> &repositoryProfiles,
    const std::vector<Profile> &visibleProfiles,
    int insertIndex)
{
    if (visibleProfiles.empty())
        return 0;

    auto findFullIndex = [&](const std::wstring &name) {
        for (std::size_t index = 0; index < repositoryProfiles.size(); ++index) {
            if (repositoryProfiles[index].name == name)
                return index;
        }
        return repositoryProfiles.size();
    };

    if (insertIndex <= 0)
        return findFullIndex(visibleProfiles.front().name);

    if (insertIndex >= static_cast<int>(visibleProfiles.size())) {
        const std::size_t lastIndex = findFullIndex(visibleProfiles.back().name);
        return lastIndex >= repositoryProfiles.size() ? repositoryProfiles.size() : lastIndex + 1;
    }

    return findFullIndex(visibleProfiles[static_cast<std::size_t>(insertIndex)].name);
}

std::optional<int> targetSelectionIndex(int currentIndex, int itemCount, int delta)
{
    if (delta == 0 || itemCount <= 0)
        return std::nullopt;
    if (currentIndex < 0 || currentIndex >= itemCount)
        return std::nullopt;

    const int targetIndex = currentIndex + delta;
    if (targetIndex < 0 || targetIndex >= itemCount)
        return std::nullopt;

    return targetIndex;
}
