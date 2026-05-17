#include "rdp/RdpReservedShortcutTracker.h"

#include <algorithm>

void RdpReservedShortcutTracker::noteHandledKeyDown(unsigned int virtualKey)
{
    if (std::find(m_virtualKeys.begin(), m_virtualKeys.end(), virtualKey) == m_virtualKeys.end())
        m_virtualKeys.push_back(virtualKey);
}

bool RdpReservedShortcutTracker::consumeHandledKeyUp(unsigned int virtualKey)
{
    const auto it = std::find(m_virtualKeys.begin(), m_virtualKeys.end(), virtualKey);
    if (it == m_virtualKeys.end())
        return false;

    m_virtualKeys.erase(it);
    return true;
}

void RdpReservedShortcutTracker::reset()
{
    m_virtualKeys.clear();
}
