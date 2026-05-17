#pragma once

#include <vector>

class RdpReservedShortcutTracker
{
public:
    void noteHandledKeyDown(unsigned int virtualKey);
    bool consumeHandledKeyUp(unsigned int virtualKey);
    void reset();

private:
    std::vector<unsigned int> m_virtualKeys;
};
