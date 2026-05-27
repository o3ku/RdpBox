#include <cassert>
#include <optional>

#include "common/NativeTypes.h"
#include "rdp/RdpMouseMoveCoalescer.h"

namespace
{
void expectPoint(const std::optional<PointI> &actual, int x, int y)
{
    assert(actual.has_value());
    assert(actual->x == x);
    assert(actual->y == y);
}
}

int main()
{
    RdpMouseMoveCoalescer coalescer;

    expectPoint(coalescer.onMouseMove(PointI{10, 20}), 10, 20);
    assert(!coalescer.onMouseMove(PointI{15, 25}).has_value());
    assert(!coalescer.onMouseMove(PointI{20, 30}).has_value());
    expectPoint(coalescer.onTimer(), 20, 30);
    assert(!coalescer.onTimer().has_value());

    expectPoint(coalescer.onMouseMove(PointI{30, 40}), 30, 40);
    assert(!coalescer.onMouseMove(PointI{35, 45}).has_value());
    expectPoint(coalescer.flush(), 35, 45);
    assert(!coalescer.onTimer().has_value());

    expectPoint(coalescer.onMouseMove(PointI{50, 60}), 50, 60);
    assert(!coalescer.onMouseMove(PointI{50, 60}).has_value());
    assert(!coalescer.onTimer().has_value());
    expectPoint(coalescer.onMouseMove(PointI{70, 80}), 70, 80);
    assert(!coalescer.flush().has_value());
    expectPoint(coalescer.onMouseMove(PointI{71, 81}), 71, 81);

    coalescer.reset();
    expectPoint(coalescer.onMouseMove(PointI{1, 2}), 1, 2);
    assert(!coalescer.flush().has_value());
    expectPoint(coalescer.onMouseMove(PointI{3, 4}), 3, 4);
    assert(!coalescer.onTimer().has_value());

    return 0;
}
