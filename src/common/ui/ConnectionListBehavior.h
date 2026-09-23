#pragma once

#include "common/profiles/Profile.h"

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

enum class ConnectionSessionPhase
{
    Connecting,
    Connected,
    Disconnected
};

struct ConnectionSessionState
{
    std::wstring profileName;
    ConnectionSessionPhase phase = ConnectionSessionPhase::Disconnected;
};

inline bool operator==(const ConnectionSessionState &left, const ConnectionSessionState &right)
{
    return left.profileName == right.profileName && left.phase == right.phase;
}

std::vector<std::wstring> connectedProfileNamesForStates(
    const std::vector<ConnectionSessionState> &sessionStates);

std::wstring connectionListStatusText(
    const std::wstring &profileName,
    const std::vector<ConnectionSessionState> &sessionStates);

// Names-only legacy view (connected or nothing) used by the Qt subtitle path.
std::wstring connectionListStatusText(
    const std::wstring &profileName,
    const std::vector<std::wstring> &connectedProfileNames);

// Kept for button-state logic that only needs the connected subset.
bool isProfileConnected(const std::wstring &profileName,
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

std::optional<int> keyboardMoveDeltaForConnectionList(bool controlDown,
                                                      bool altDown,
                                                      bool shiftDown,
                                                      bool upKey,
                                                      bool downKey);

std::vector<int> retainedSelectionRowsForProfiles(
    const std::vector<Profile> &visibleProfiles,
    const std::vector<std::wstring> &preferredProfileNames,
    bool allowFallbackSelection = true);

std::wstring duplicateProfileName(const std::wstring &profileName,
                                 const std::vector<std::wstring> &existingNames = {});

Profile duplicateProfileDraft(const Profile &profile,
                              const std::vector<std::wstring> &existingNames = {});

std::size_t repositoryTargetIndexForVisibleInsertIndex(
    const std::vector<Profile> &repositoryProfiles,
    const std::vector<Profile> &visibleProfiles,
    int insertIndex);

std::optional<int> targetSelectionIndex(int currentIndex, int itemCount, int delta);
