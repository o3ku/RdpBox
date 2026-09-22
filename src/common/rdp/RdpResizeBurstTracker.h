#pragma once

#include "common/NativeTypes.h"

class RdpResizeBurstTracker
{
public:
    void reset();

    bool onResize(const SizeI &size);
    bool onTimeout(const SizeI &size);

private:
    SizeI m_lastSentSize;
    bool m_active = false;
};
