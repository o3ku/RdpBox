#include "MainWindowSessionBehavior.h"

#include <algorithm>
#include <windows.h>

namespace
{
bool equalsInsensitive(const std::wstring &left, const std::wstring &right)
{
    if (left.size() != right.size())
        return false;

    return CompareStringOrdinal(left.data(),
                                static_cast<int>(left.size()),
                                right.data(),
                                static_cast<int>(right.size()),
                                TRUE) == CSTR_EQUAL;
}
}

std::vector<Profile> validProfilesForConnectionNames(const std::vector<std::wstring> &connectionNames,
                                                     const std::vector<Profile> &availableProfiles)
{
    std::vector<Profile> profiles;
    profiles.reserve(connectionNames.size());

    for (const std::wstring &name : connectionNames) {
        const auto it = std::find_if(availableProfiles.begin(), availableProfiles.end(),
            [&](const Profile &profile) { return equalsInsensitive(profile.name, name); });
        if (it != availableProfiles.end() && it->isValid())
            profiles.push_back(*it);
    }

    return profiles;
}

bool shouldOpenProfileSession(const Profile &profile)
{
    return profile.isValid();
}

namespace ui
{
MainWindowOpenPlan openPlanForConnectionNames(const std::vector<std::wstring> &connectionNames,
                                              const std::vector<Profile> &availableProfiles)
{
    MainWindowOpenPlan plan;
    plan.profilesToOpen = validProfilesForConnectionNames(connectionNames, availableProfiles);
    return plan;
}

MainWindowTabClosePlan tabClosePlanForSessionId(const std::string &sessionId)
{
    MainWindowTabClosePlan plan;
    if (sessionId.empty())
        return plan;

    plan.closeSession = true;
    plan.refreshTabStatuses = true;
    plan.sessionId = sessionId;
    return plan;
}

MainWindowTabReconnectPlan tabReconnectPlanForSessionId(const std::string &sessionId)
{
    MainWindowTabReconnectPlan plan;
    if (sessionId.empty())
        return plan;

    plan.reconnectSession = true;
    plan.sessionId = sessionId;
    return plan;
}

MainWindowConnectionCompletedPlan connectionCompletedPlan(const Profile &profile,
                                                          bool currentlyFullScreen)
{
    MainWindowConnectionCompletedPlan plan;
    plan.enterFullScreen = profile.fullScreenOnConnect && !currentlyFullScreen;
    plan.refreshTabStatuses = true;
    return plan;
}

MainWindowExitSizeMovePlan exitSizeMovePlan(bool inMoveOrSizeLoop, bool hasSessionManager)
{
    MainWindowExitSizeMovePlan plan;
    plan.clearResizeSuppression = hasSessionManager;
    plan.flushPendingResize = hasSessionManager;
    plan.persistWindowState = inMoveOrSizeLoop;
    return plan;
}

MainWindowPowerBroadcastPlan powerBroadcastPlan(unsigned int eventCode, bool hasSessionManager)
{
    MainWindowPowerBroadcastPlan plan;
    plan.handleHostResume = hasSessionManager
        && (eventCode == PBT_APMRESUMEAUTOMATIC || eventCode == PBT_APMRESUMESUSPEND);
    return plan;
}
}
