#include "rdp/RdpResolutionRecovery.h"

int main()
{
    {
        RdpResolutionRecovery recovery;
        if (recovery.begin(false))
            return 1;
        if (recovery.active())
            return 1;
        if (recovery.onTimeout())
            return 1;
    }

    {
        RdpResolutionRecovery recovery;
        if (!recovery.begin(true))
            return 1;
        if (!recovery.active())
            return 1;
        recovery.onFrameProgress(false);
        if (!recovery.active())
            return 1;
        if (!recovery.onTimeout())
            return 1;
        if (recovery.active())
            return 1;
    }

    {
        RdpResolutionRecovery recovery;
        if (!recovery.begin(true))
            return 1;
        recovery.onFrameProgress(true);
        if (recovery.active())
            return 1;
        if (recovery.onTimeout())
            return 1;
    }

    return 0;
}
