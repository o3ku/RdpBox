#include "BrowserTabBehavior.h"

#include <algorithm>
#include <cstdlib>

namespace ui
{
namespace
{
constexpr int kTabMinWidth = 120;
constexpr int kTabMaxWidth = 220;
constexpr int kCloseButtonSize = 16;
constexpr int kCloseButtonMargin = 6;
constexpr int kDragThreshold = 4;
}

int browserTabWidth(int totalWidth, int tabCount)
{
    if (totalWidth <= 0 || tabCount <= 0)
        return 0;

    int tabWidth = totalWidth / tabCount;
    tabWidth = std::clamp(tabWidth, kTabMinWidth, kTabMaxWidth);
    if (tabWidth * tabCount > totalWidth)
        tabWidth = std::max(40, totalWidth / tabCount);

    return tabWidth;
}

std::vector<BrowserTabLayoutItem> browserTabLayout(int totalWidth, int height, int tabCount)
{
    std::vector<BrowserTabLayoutItem> items;
    if (tabCount <= 0)
        return items;

    const int tabWidth = browserTabWidth(totalWidth, tabCount);
    if (tabWidth <= 0)
        return items;

    items.reserve(static_cast<std::size_t>(tabCount));
    int x = 0;
    for (int index = 0; index < tabCount; ++index) {
        BrowserTabLayoutItem item;
        item.left = x;
        item.right = x + tabWidth;
        item.closeRight = item.right - kCloseButtonMargin;
        item.closeLeft = item.closeRight - kCloseButtonSize;
        item.closeTop = (height - kCloseButtonSize) / 2;
        item.closeBottom = item.closeTop + kCloseButtonSize;
        items.push_back(item);
        x += tabWidth;
    }
    return items;
}

bool shouldDrawTabSeparator(int tabIndex, int selectedIndex, int tabCount)
{
    if (tabIndex < 0 || tabIndex >= tabCount)
        return false;

    const bool hasNext = tabIndex + 1 < tabCount;
    const bool selected = tabIndex == selectedIndex;
    const bool nextSelected = hasNext && tabIndex + 1 == selectedIndex;
    return hasNext && !selected && !nextSelected;
}

bool hasDraggedTabFarEnough(int startX, int startY, int currentX, int currentY)
{
    return std::abs(currentX - startX) >= kDragThreshold
        || std::abs(currentY - startY) >= kDragThreshold;
}

std::optional<int> targetTabIndexForDrop(int sourceIndex, int dropInsertIndex, int tabCount)
{
    if (sourceIndex < 0 || sourceIndex >= tabCount)
        return std::nullopt;
    if (dropInsertIndex < 0)
        return std::nullopt;

    int targetIndex = dropInsertIndex;
    if (targetIndex > sourceIndex)
        --targetIndex;
    if (targetIndex >= tabCount)
        targetIndex = tabCount - 1;
    if (targetIndex < 0 || targetIndex == sourceIndex)
        return std::nullopt;

    return targetIndex;
}
}
