#pragma once

#include <optional>

std::optional<int> selectedTabAfterClose(int sessionCountBeforeClose, int closedIndex);

bool canMoveSessionTab(int sessionCount, int fromIndex, int toIndex);

int activeTabIndexAfterMove(int activeIndex, int fromIndex, int toIndex);
