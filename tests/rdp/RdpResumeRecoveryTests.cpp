#include <cassert>

#include "rdp/RdpResumeRecovery.h"

int main()
{
    {
        RdpResumeRecovery recovery;
        assert(recovery.onResume(false, true) == RdpResumeRecovery::Action::None);
        assert(!recovery.awaitingFrame());
        assert(recovery.onTimeout() == RdpResumeRecovery::Action::None);
    }

    {
        RdpResumeRecovery recovery;
        assert(recovery.onResume(true, true) == RdpResumeRecovery::Action::RequestRefresh);
        assert(recovery.awaitingFrame());
        recovery.onFrameArrived();
        assert(!recovery.awaitingFrame());
        assert(recovery.onTimeout() == RdpResumeRecovery::Action::None);
    }

    {
        RdpResumeRecovery recovery;
        assert(recovery.onResume(true, true) == RdpResumeRecovery::Action::RequestRefresh);
        assert(recovery.awaitingFrame());
        assert(recovery.onTimeout() == RdpResumeRecovery::Action::Reconnect);
        assert(!recovery.awaitingFrame());
        assert(recovery.onTimeout() == RdpResumeRecovery::Action::None);
    }

    {
        RdpResumeRecovery recovery;
        assert(recovery.onResume(true, false) == RdpResumeRecovery::Action::None);
        assert(!recovery.awaitingFrame());
        assert(recovery.onTimeout() == RdpResumeRecovery::Action::None);
        assert(recovery.onBecameVisible(true) == RdpResumeRecovery::Action::RequestRefresh);
        assert(recovery.awaitingFrame());
    }

    {
        RdpResumeRecovery recovery;
        assert(recovery.onResume(true, false) == RdpResumeRecovery::Action::None);
        assert(recovery.onBecameVisible(false) == RdpResumeRecovery::Action::None);
        assert(!recovery.awaitingFrame());
    }

    return 0;
}
