#pragma once

enum class SessionResumeAction
{
    MarkDisconnected,
    AutoReconnect,
};

inline SessionResumeAction sessionResumeActionForTab(int tabIndex, int activeTabIndex)
{
    return tabIndex == activeTabIndex
        ? SessionResumeAction::AutoReconnect
        : SessionResumeAction::MarkDisconnected;
}
