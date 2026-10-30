#pragma once

#include "common/NativeTypes.h"

// Whether the cursor the window shows is still the one the session asked for.
//
// A session cursor is claimed by the view that received it, but the OS owns the
// window cursor: anything that runs WM_SETCURSOR in between (another tab, a
// child control, the window frame) moves it back to the class cursor, and
// nothing tells us. A view that only remembers the last CursorInfo therefore
// serves a stale pointer until the server happens to send a new one - the
// "remote cursor came back but the wrong shape" report.
class RdpRemoteCursorState
{
public:
    // The session announced a cursor. `applied` says whether the window cursor
    // is that cursor right now: false when the view is hidden or unfocused.
    void adopt(const CursorInfo &cursor, bool applied);
    void reset();

    // True when the window cursor was moved away and should be re-applied.
    bool needsApply() const;
    // True once the session announced a cursor for the view to apply.
    bool hasCursor() const;
    // The cursor to put back, or nothing while the session wants no cursor.
    bool cursorToApply(CursorInfo &cursor) const;
    // The cursor the session asked for, by shape. A custom cursor whose handle
    // is gone is still this shape: the view decides what it can still put on
    // the window.
    const CursorInfo &cursor() const;
    // The window cursor is the session cursor again.
    void noteApplied();
    // Something else may own the window cursor now (another tab, the frame).
    void noteReleased();

private:
    CursorInfo m_cursor;
    bool m_hasCursor = false;
    bool m_pending = false;
};
