#pragma once

class RdpResolutionRecovery
{
public:
    bool begin(bool connected);
    void onFrameProgress(bool completed);
    bool onTimeout();
    void reset();
    bool active() const;

private:
    bool m_active = false;
};
