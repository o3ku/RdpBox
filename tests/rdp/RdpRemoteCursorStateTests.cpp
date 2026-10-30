#include <cassert>

#include "common/rdp/RdpRemoteCursorState.h"

namespace
{
CursorInfo arrowCursor()
{
    return CursorInfo{CursorKind::Arrow, nullptr, false};
}

CursorInfo hiddenCursor()
{
    return CursorInfo{CursorKind::Hidden, nullptr, false};
}

// cursorToApply() fills an out-parameter, so what it is given outlives the call.
CursorInfo &unusedCursor()
{
    static CursorInfo cursor;
    cursor = CursorInfo{};
    return cursor;
}
}

int main()
{
    {
        // Nothing was adopted yet: nothing to put back.
        RdpRemoteCursorState state;
        assert(!state.needsApply());
        assert(!state.hasCursor());
        CursorInfo cursor;
        assert(!state.cursorToApply(cursor));
    }

    {
        // The view could apply it right away, so the cursor is in place.
        RdpRemoteCursorState state;
        state.adopt(arrowCursor(), true);
        assert(!state.needsApply());
        assert(state.hasCursor());
        CursorInfo cursor;
        assert(state.cursorToApply(cursor));
        assert(cursor.kind == CursorKind::Arrow);
    }

    {
        // Applied while hidden: the cursor still has to be put back once the
        // view is on screen.
        RdpRemoteCursorState state;
        state.adopt(arrowCursor(), false);
        assert(state.needsApply());

        state.noteApplied();
        assert(!state.needsApply());
    }

    {
        // Another tab moved the window cursor: the session cursor is only a
        // memory now.
        RdpRemoteCursorState state;
        state.adopt(arrowCursor(), true);
        state.noteReleased();
        assert(state.needsApply());
        assert(state.cursorToApply(unusedCursor()));
    }

    {
        // A hidden session cursor asks for no cursor, so there is nothing to
        // re-apply - and that must not leave the state stuck "pending".
        RdpRemoteCursorState state;
        state.adopt(hiddenCursor(), false);
        assert(!state.needsApply());
        assert(!state.cursorToApply(unusedCursor()));

        state.adopt(arrowCursor(), true);
        state.adopt(hiddenCursor(), true);
        state.noteReleased();
        assert(!state.needsApply());
    }

    {
        // A session reset forgets the cursor entirely.
        RdpRemoteCursorState state;
        state.adopt(arrowCursor(), false);
        state.reset();
        assert(!state.needsApply());
        assert(!state.hasCursor());
        assert(!state.cursorToApply(unusedCursor()));
        assert(state.cursor().kind == CursorKind::Arrow);
    }

    {
        // The shape survives the handle: a view that released the handle can
        // still tell that what is missing is this session's custom cursor.
        RdpRemoteCursorState state;
        state.adopt(CursorInfo{CursorKind::Custom, nullptr, false}, false);
        assert(state.needsApply());
        assert(state.cursor().kind == CursorKind::Custom);
        state.reset();
        assert(state.cursor().kind == CursorKind::Arrow);
    }

    {
        // Focus coming home clears the pending state without a re-apply: the
        // view re-reads the session cursor, so the stale "put it back" flag
        // must not survive and fire again on the next paint.
        RdpRemoteCursorState state;
        state.adopt(arrowCursor(), true);
        state.noteReleased();
        assert(state.needsApply());
        state.noteApplied();
        assert(!state.needsApply());
    }

    return 0;
}
