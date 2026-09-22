#pragma once

#include <cstdint>

class RdpResumeRecovery
{
public:
    enum class Action
    {
        None,
        RequestRefresh,
        Reconnect,
    };

    void reset();
    Action onResume(bool connected, bool visible);
    Action onBecameVisible(bool connected);
    void onFrameArrived();
    Action onTimeout();
    bool awaitingFrame() const;

private:
    bool m_awaitingFrame = false;
    bool m_refreshPendingWhenVisible = false;
};
