#pragma once

#include "profiles/Profile.h"

#include <string>
#include <vector>

namespace ui
{
struct MainWindowOpenPlan
{
    std::vector<Profile> profilesToOpen;
};

struct MainWindowTabClosePlan
{
    bool closeSession = false;
    bool refreshTabStatuses = false;
    std::string sessionId;
};

struct MainWindowTabReconnectPlan
{
    bool reconnectSession = false;
    std::string sessionId;
};

struct MainWindowConnectionCompletedPlan
{
    bool enterFullScreen = false;
    bool refreshTabStatuses = false;
};

struct MainWindowExitSizeMovePlan
{
    bool clearResizeSuppression = false;
    bool flushPendingResize = false;
    bool persistWindowState = false;
};

struct MainWindowPowerBroadcastPlan
{
    bool handleHostResume = false;
};
}

std::vector<Profile> validProfilesForConnectionNames(const std::vector<std::wstring> &connectionNames,
                                                     const std::vector<Profile> &availableProfiles);

bool shouldOpenProfileSession(const Profile &profile);

namespace ui
{
MainWindowOpenPlan openPlanForConnectionNames(const std::vector<std::wstring> &connectionNames,
                                              const std::vector<Profile> &availableProfiles);

MainWindowTabClosePlan tabClosePlanForSessionId(const std::string &sessionId);

MainWindowTabReconnectPlan tabReconnectPlanForSessionId(const std::string &sessionId);

MainWindowConnectionCompletedPlan connectionCompletedPlan(const Profile &profile,
                                                          bool currentlyFullScreen);

MainWindowExitSizeMovePlan exitSizeMovePlan(bool inMoveOrSizeLoop, bool hasSessionManager);

MainWindowPowerBroadcastPlan powerBroadcastPlan(unsigned int eventCode, bool hasSessionManager);
}
