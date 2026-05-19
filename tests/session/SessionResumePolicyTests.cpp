#include "session/SessionResumePolicy.h"

int main()
{
    if (sessionResumeActionForTab(0, 0) != SessionResumeAction::AutoReconnect)
        return 1;
    if (sessionResumeActionForTab(1, 0) != SessionResumeAction::MarkDisconnected)
        return 1;
    if (sessionResumeActionForTab(0, -1) != SessionResumeAction::MarkDisconnected)
        return 1;

    return 0;
}
