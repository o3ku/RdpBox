#include <cassert>

#include "common/NativeTypes.h"
#include "common/rdp/RdpSessionMouseState.h"

namespace
{


void buttonMaskMatchesDocumentedLayout()
{
    RdpSessionMouseState state;
    state.noteButton(MouseButton::Left, true);
    assert(state.pressedButtons() == (1u << 0));
    state.noteButton(MouseButton::Right, true);
    assert(state.pressedButtons() == ((1u << 0) | (1u << 1)));
    state.noteButton(MouseButton::Middle, true);
    assert(state.pressedButtons() == ((1u << 0) | (1u << 1) | (1u << 2)));

    state.noteButton(MouseButton::Left, false);
    assert(state.pressedButtons() == ((1u << 1) | (1u << 2)));

    state.clearPressedButtons();
    assert(state.pressedButtons() == 0);

    // Extended buttons carry no mask and must not disturb the bitmap.
    state.noteButton(MouseButton::Back, true);
    state.noteButton(MouseButton::Forward, true);
    state.noteButton(MouseButton::None, true);
    assert(state.pressedButtons() == 0);
}

void resetPendingMoveKeepsPressAndPosition()
{
    RdpSessionMouseState state;
    const PointI point{11, 7};
    state.notePointerPosition(point);
    state.noteButton(MouseButton::Left, true);

    // A pending coalesced move must be dropped...
    state.resetPendingMove();
    assert(!state.flushPendingPointerMove().has_value());
    assert(!state.onPointerMoveTimer().has_value());

    // ...while a recorded press and the pointer position survive, so a queued
    // releasePressedMouseButtons() can still emit the remote release at the
    // right coordinates.
    assert(state.pressedButtons() == (1u << 0));
    assert(state.currentPointerPosition().x == point.x);
    assert(state.currentPointerPosition().y == point.y);

    state.noteButton(MouseButton::Left, false);
    assert(state.pressedButtons() == 0);
}

void resetClearsEverything()
{
    RdpSessionMouseState state;
    state.notePointerPosition(PointI{5, 6});
    state.noteButton(MouseButton::Right, true);
    state.onPointerMove(PointI{40, 50}); // seed a pending coalesced move
    state.setPointerMoveTimerActive(true);

    state.reset();

    assert(state.pressedButtons() == 0);
    assert(state.currentPointerPosition().x == 0);
    assert(state.currentPointerPosition().y == 0);
    assert(!state.flushPendingPointerMove().has_value());
    assert(!state.pointerMoveTimerActive());
}

void pointerMoveCoalescesThroughTheSharedState()
{
    RdpSessionMouseState state;
    // First move goes out immediately.
    assert(state.onPointerMove(PointI{1, 2}).has_value());
    // Follow-ups are coalesced until a flush or timer tick.
    assert(!state.onPointerMove(PointI{3, 4}).has_value());
    const auto flushed = state.flushPendingPointerMove();
    assert(flushed.has_value());
    assert(flushed->x == 3 && flushed->y == 4);
    assert(!state.flushPendingPointerMove().has_value());
}

} // namespace

int main()
{
    buttonMaskMatchesDocumentedLayout();
    resetPendingMoveKeepsPressAndPosition();
    resetClearsEverything();
    pointerMoveCoalescesThroughTheSharedState();
    return 0;
}
