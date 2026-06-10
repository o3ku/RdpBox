#include "SessionTabBehavior.h"

#include <algorithm>

std::optional<int> selectedTabAfterClose(int sessionCountBeforeClose, int closedIndex)
{
    if (sessionCountBeforeClose <= 0)
        return std::nullopt;
    if (closedIndex < 0 || closedIndex >= sessionCountBeforeClose)
        return std::nullopt;

    const int sessionCountAfterClose = sessionCountBeforeClose - 1;
    if (sessionCountAfterClose <= 0)
        return std::nullopt;

    return std::min(closedIndex, sessionCountAfterClose - 1);
}

bool canMoveSessionTab(int sessionCount, int fromIndex, int toIndex)
{
    if (fromIndex < 0 || toIndex < 0)
        return false;
    if (fromIndex >= sessionCount || toIndex >= sessionCount)
        return false;
    return fromIndex != toIndex;
}

int activeTabIndexAfterMove(int activeIndex, int fromIndex, int toIndex)
{
    if (activeIndex == fromIndex)
        return toIndex;

    if (fromIndex < toIndex && activeIndex > fromIndex && activeIndex <= toIndex)
        return activeIndex - 1;

    if (fromIndex > toIndex && activeIndex >= toIndex && activeIndex < fromIndex)
        return activeIndex + 1;

    return activeIndex;
}
