#include <cassert>

#include "ui/BrowserTabBehavior.h"

int main()
{
    assert(ui::browserTabWidth(800, 3) == 220);
    assert(ui::browserTabWidth(360, 3) == 120);
    assert(ui::browserTabWidth(200, 3) == 66);
    assert(ui::browserTabWidth(0, 3) == 0);
    assert(ui::browserTabWidth(800, 0) == 0);

    {
        const auto layout = ui::browserTabLayout(800, 40, 3);
        assert(layout.size() == 3);
        assert(layout[0].left == 0);
        assert(layout[0].right == 220);
        assert(layout[0].closeLeft == 198);
        assert(layout[0].closeRight == 214);
        assert(layout[0].closeTop == 12);
        assert(layout[0].closeBottom == 28);
        assert(layout[1].left == 220);
        assert(layout[1].right == 440);
    }

    {
        assert(ui::shouldDrawTabSeparator(0, 0, 3) == false);
        assert(ui::shouldDrawTabSeparator(0, 1, 3) == false);
        assert(ui::shouldDrawTabSeparator(0, 2, 3) == true);
        assert(ui::shouldDrawTabSeparator(1, 2, 3) == false);
        assert(ui::shouldDrawTabSeparator(2, 0, 3) == false);
        assert(ui::shouldDrawTabSeparator(-1, 0, 3) == false);
        assert(ui::shouldDrawTabSeparator(3, 0, 3) == false);
    }

    {
        assert(!ui::hasDraggedTabFarEnough(10, 10, 13, 13));
        assert(ui::hasDraggedTabFarEnough(10, 10, 14, 10));
        assert(ui::hasDraggedTabFarEnough(10, 10, 10, 6));
    }

    {
        assert(!ui::targetTabIndexForDrop(0, -1, 3).has_value());
        assert(!ui::targetTabIndexForDrop(-1, 1, 3).has_value());
        assert(!ui::targetTabIndexForDrop(3, 1, 3).has_value());
        assert(!ui::targetTabIndexForDrop(0, 0, 3).has_value());
        assert(ui::targetTabIndexForDrop(0, 3, 3).value() == 2);
        assert(ui::targetTabIndexForDrop(2, 0, 3).value() == 0);
        assert(ui::targetTabIndexForDrop(1, 0, 3).value() == 0);
        assert(ui::targetTabIndexForDrop(1, 3, 3).value() == 2);
    }

    return 0;
}
