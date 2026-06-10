#include <cassert>

#include <windows.h>

#include "ui/MainWindowSessionBehavior.h"

namespace
{
Profile profile(std::wstring name, std::wstring host)
{
    Profile result;
    result.name = std::move(name);
    result.host = std::move(host);
    result.port = 3389;
    return result;
}
}

int main()
{
    const std::vector<Profile> availableProfiles = {
        profile(L"alpha", L"10.0.0.1"),
        profile(L"beta", L"10.0.0.2"),
        profile(L"invalid", L""),
    };

    const auto profiles = validProfilesForConnectionNames(
        {L"missing", L"BETA", L"invalid", L"alpha", L"beta"},
        availableProfiles);
    assert(profiles.size() == 3);
    assert(profiles[0].name == L"beta");
    assert(profiles[1].name == L"alpha");
    assert(profiles[2].name == L"beta");

    assert(shouldOpenProfileSession(profile(L"valid", L"host")));
    assert(!shouldOpenProfileSession(profile(L"", L"host")));
    assert(!shouldOpenProfileSession(profile(L"name", L"")));

    {
        const ui::MainWindowOpenPlan plan =
            ui::openPlanForConnectionNames({L"ALPHA", L"missing", L"beta"}, availableProfiles);
        assert(plan.profilesToOpen.size() == 2);
        assert(plan.profilesToOpen[0].name == L"alpha");
        assert(plan.profilesToOpen[1].name == L"beta");
    }

    {
        const ui::MainWindowTabClosePlan plan = ui::tabClosePlanForSessionId("session-1");
        assert(plan.closeSession);
        assert(plan.refreshTabStatuses);
        assert(plan.sessionId == "session-1");

        const ui::MainWindowTabClosePlan emptyPlan = ui::tabClosePlanForSessionId({});
        assert(!emptyPlan.closeSession);
        assert(!emptyPlan.refreshTabStatuses);
        assert(emptyPlan.sessionId.empty());
    }

    {
        const ui::MainWindowTabReconnectPlan plan = ui::tabReconnectPlanForSessionId("session-2");
        assert(plan.reconnectSession);
        assert(plan.sessionId == "session-2");

        const ui::MainWindowTabReconnectPlan emptyPlan = ui::tabReconnectPlanForSessionId({});
        assert(!emptyPlan.reconnectSession);
        assert(emptyPlan.sessionId.empty());
    }

    {
        Profile fullscreenProfile = profile(L"fullscreen", L"host");
        fullscreenProfile.fullScreenOnConnect = true;
        const auto enterPlan = ui::connectionCompletedPlan(fullscreenProfile, false);
        assert(enterPlan.enterFullScreen);
        assert(enterPlan.refreshTabStatuses);

        const auto alreadyFullScreenPlan = ui::connectionCompletedPlan(fullscreenProfile, true);
        assert(!alreadyFullScreenPlan.enterFullScreen);
        assert(alreadyFullScreenPlan.refreshTabStatuses);

        const auto normalPlan = ui::connectionCompletedPlan(profile(L"normal", L"host"), false);
        assert(!normalPlan.enterFullScreen);
        assert(normalPlan.refreshTabStatuses);
    }

    {
        const auto plan = ui::exitSizeMovePlan(true, true);
        assert(plan.clearResizeSuppression);
        assert(plan.flushPendingResize);
        assert(plan.persistWindowState);

        const auto noSessionPlan = ui::exitSizeMovePlan(true, false);
        assert(!noSessionPlan.clearResizeSuppression);
        assert(!noSessionPlan.flushPendingResize);
        assert(noSessionPlan.persistWindowState);

        const auto noMovePlan = ui::exitSizeMovePlan(false, true);
        assert(noMovePlan.clearResizeSuppression);
        assert(noMovePlan.flushPendingResize);
        assert(!noMovePlan.persistWindowState);
    }

    {
        assert(ui::powerBroadcastPlan(PBT_APMRESUMEAUTOMATIC, true).handleHostResume);
        assert(ui::powerBroadcastPlan(PBT_APMRESUMESUSPEND, true).handleHostResume);
        assert(!ui::powerBroadcastPlan(PBT_APMSUSPEND, true).handleHostResume);
        assert(!ui::powerBroadcastPlan(PBT_APMRESUMEAUTOMATIC, false).handleHostResume);
    }

    return 0;
}
