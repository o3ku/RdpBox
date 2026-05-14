#include "rdp/RdpResumeRecovery.h"

void RdpResumeRecovery::reset()
{
    m_awaitingFrame = false;
    m_refreshPendingWhenVisible = false;
}

RdpResumeRecovery::Action RdpResumeRecovery::onResume(bool connected, bool visible)
{
    if (!connected) {
        m_awaitingFrame = false;
        m_refreshPendingWhenVisible = false;
        return Action::None;
    }

    if (!visible) {
        m_awaitingFrame = false;
        m_refreshPendingWhenVisible = true;
        return Action::None;
    }

    m_refreshPendingWhenVisible = false;
    m_awaitingFrame = true;
    return Action::RequestRefresh;
}

RdpResumeRecovery::Action RdpResumeRecovery::onBecameVisible(bool connected)
{
    if (!m_refreshPendingWhenVisible || !connected)
        return Action::None;

    m_refreshPendingWhenVisible = false;
    m_awaitingFrame = true;
    return Action::RequestRefresh;
}

void RdpResumeRecovery::onFrameArrived()
{
    m_awaitingFrame = false;
    m_refreshPendingWhenVisible = false;
}

RdpResumeRecovery::Action RdpResumeRecovery::onTimeout()
{
    if (!m_awaitingFrame)
        return Action::None;

    m_awaitingFrame = false;
    return Action::Reconnect;
}

bool RdpResumeRecovery::awaitingFrame() const
{
    return m_awaitingFrame;
}
