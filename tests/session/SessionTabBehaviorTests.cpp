#include <cassert>

#include "session/SessionTabBehavior.h"

int main()
{
    assert(!selectedTabAfterClose(0, 0).has_value());
    assert(!selectedTabAfterClose(1, 0).has_value());
    assert(!selectedTabAfterClose(3, -1).has_value());
    assert(!selectedTabAfterClose(3, 3).has_value());
    assert(selectedTabAfterClose(3, 0).value() == 0);
    assert(selectedTabAfterClose(3, 1).value() == 1);
    assert(selectedTabAfterClose(3, 2).value() == 1);

    assert(canMoveSessionTab(3, 0, 2));
    assert(canMoveSessionTab(3, 2, 0));
    assert(!canMoveSessionTab(3, -1, 0));
    assert(!canMoveSessionTab(3, 0, -1));
    assert(!canMoveSessionTab(3, 0, 3));
    assert(!canMoveSessionTab(3, 1, 1));

    assert(activeTabIndexAfterMove(0, 0, 2) == 2);
    assert(activeTabIndexAfterMove(1, 0, 2) == 0);
    assert(activeTabIndexAfterMove(2, 0, 2) == 1);
    assert(activeTabIndexAfterMove(2, 2, 0) == 0);
    assert(activeTabIndexAfterMove(1, 2, 0) == 2);
    assert(activeTabIndexAfterMove(0, 2, 0) == 1);
    assert(activeTabIndexAfterMove(3, 0, 2) == 3);

    return 0;
}
