#pragma once

#include <optional>
#include <vector>

namespace ui
{
struct BrowserTabLayoutItem
{
    int left = 0;
    int right = 0;
    int closeLeft = 0;
    int closeRight = 0;
    int closeTop = 0;
    int closeBottom = 0;
};

int browserTabWidth(int totalWidth, int tabCount);

std::vector<BrowserTabLayoutItem> browserTabLayout(int totalWidth, int height, int tabCount);

bool shouldDrawTabSeparator(int tabIndex, int selectedIndex, int tabCount);

bool hasDraggedTabFarEnough(int startX, int startY, int currentX, int currentY);

std::optional<int> targetTabIndexForDrop(int sourceIndex, int dropInsertIndex, int tabCount);
}
