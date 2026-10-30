#include "common/rdp/RdpRemoteCursorState.h"

namespace
{
// A hidden cursor is the session asking for no cursor at all. There is nothing
// to put back, so it never needs an apply either.
bool hasVisibleCursor(const CursorInfo &cursor)
{
    return cursor.kind != CursorKind::Hidden;
}
}

void RdpRemoteCursorState::adopt(const CursorInfo &cursor, bool applied)
{
    m_cursor = cursor;
    m_hasCursor = true;
    m_pending = hasVisibleCursor(cursor) && !applied;
}

void RdpRemoteCursorState::reset()
{
    m_cursor = CursorInfo{};
    m_hasCursor = false;
    m_pending = false;
}

bool RdpRemoteCursorState::needsApply() const
{
    return m_pending;
}

bool RdpRemoteCursorState::hasCursor() const
{
    return m_hasCursor;
}

bool RdpRemoteCursorState::cursorToApply(CursorInfo &cursor) const
{
    if (!m_hasCursor || !hasVisibleCursor(m_cursor))
        return false;

    cursor = m_cursor;
    return true;
}

const CursorInfo &RdpRemoteCursorState::cursor() const
{
    return m_cursor;
}

void RdpRemoteCursorState::noteApplied()
{
    m_pending = false;
}

void RdpRemoteCursorState::noteReleased()
{
    m_pending = hasVisibleCursor(m_cursor);
}
