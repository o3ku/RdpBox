#include "rdp/RdpResolutionRecovery.h"

bool RdpResolutionRecovery::begin(bool connected)
{
    m_active = connected;
    return m_active;
}

void RdpResolutionRecovery::onFrameProgress(bool completed)
{
    if (completed)
        m_active = false;
}

bool RdpResolutionRecovery::onTimeout()
{
    const bool shouldReconnect = m_active;
    m_active = false;
    return shouldReconnect;
}

void RdpResolutionRecovery::reset()
{
    m_active = false;
}

bool RdpResolutionRecovery::active() const
{
    return m_active;
}
