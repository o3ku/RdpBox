#include "RdpSessionView.h"

#include "rdp/RdpInputModifiers.h"
#include "rdp/RdpReconnectInteraction.h"
#include "ui/ParentResizeForwarder.h"

namespace
{
constexpr UINT kMouseMoveCoalesceMs = 16;

PointI pointFromCPoint(CPoint point)
{
    return PointI{point.x, point.y};
}
}

void CRdpSessionView::syncMouseModifiers(UINT mouseFlags)
{
    if (!m_process)
        return;

    sendKeyboardActions(m_keyboardState.synchronizeMouseModifiers(
        mouseFlags,
        currentKeyboardPhysicalState(),
        ::GetFocus() == GetSafeHwnd()));
    releaseKeyboardTargetIfInactive();
}

void CRdpSessionView::flushPendingMouseMove()
{
    if (!m_process) {
        if (m_mouseState.pointerMoveTimerActive()) {
            KillTimer(kMouseMoveTimerId);
            m_mouseState.setPointerMoveTimerActive(false);
        }
        return;
    }

    const auto pending = m_mouseState.flushPendingPointerMove();
    if (pending)
        m_process->sendMouseMove(*pending, currentViewSize());

    if (m_mouseState.pointerMoveTimerActive()) {
        KillTimer(kMouseMoveTimerId);
        m_mouseState.setPointerMoveTimerActive(false);
    }
}

void CRdpSessionView::OnMouseMove(UINT flags, CPoint point)
{
    const PointI pointer{point.x, point.y};
    m_mouseState.notePointerPosition(pointer);
    if (rdp::shouldSynchronizeModifiersForMouseMove(flags))
        syncMouseModifiers(flags);
    if (!m_process)
        return;

    const auto immediate = m_mouseState.onPointerMove(pointer);
    if (immediate)
        m_process->sendMouseMove(*immediate, currentViewSize());

    if (!m_mouseState.pointerMoveTimerActive()) {
        SetTimer(kMouseMoveTimerId, kMouseMoveCoalesceMs, nullptr);
        m_mouseState.setPointerMoveTimerActive(true);
    }
}

void CRdpSessionView::OnLButtonDown(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    handleMouseButtonDown(flags, point, MouseButton::Left, true);
}

void CRdpSessionView::OnLButtonUp(UINT flags, CPoint point)
{
    handleMouseButtonUp(flags, point, MouseButton::Left);
}

void CRdpSessionView::OnLButtonDblClk(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    handleMouseButtonDown(flags, point, MouseButton::Left, true);
}

void CRdpSessionView::OnRButtonDown(UINT flags, CPoint point)
{
    handleMouseButtonDown(flags, point, MouseButton::Right, false);
}

void CRdpSessionView::OnRButtonUp(UINT flags, CPoint point)
{
    handleMouseButtonUp(flags, point, MouseButton::Right);
}

void CRdpSessionView::OnRButtonDblClk(UINT flags, CPoint point)
{
    handleMouseButtonDown(flags, point, MouseButton::Right, false);
}

void CRdpSessionView::OnMButtonDown(UINT flags, CPoint point)
{
    handleMouseButtonDown(flags, point, MouseButton::Middle, false);
}

void CRdpSessionView::OnMButtonUp(UINT flags, CPoint point)
{
    handleMouseButtonUp(flags, point, MouseButton::Middle);
}

void CRdpSessionView::OnMButtonDblClk(UINT flags, CPoint point)
{
    handleMouseButtonDown(flags, point, MouseButton::Middle, false);
}

void CRdpSessionView::handleMouseButtonDown(UINT flags, CPoint point, MouseButton button, bool allowReconnect)
{
    const bool processFinished = m_process && m_process->state() == FreeRdpProcess::State::Finished;
    if (allowReconnect
        && shouldReconnectOnPointerDown(m_profile.isValid(),
                                        m_connected,
                                        m_process != nullptr,
                                        processFinished,
                                        m_resolutionUpdatePending)) {
        if (m_reconnectRequested)
            m_reconnectRequested();
        return;
    }

    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    m_mouseState.notePointerPosition(pointFromCPoint(point));
    SetCapture();
    sendTrackedMouseButton(button, true, pointFromCPoint(point));
}

void CRdpSessionView::handleMouseButtonUp(UINT flags, CPoint point, MouseButton button)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    m_mouseState.notePointerPosition(pointFromCPoint(point));
    sendTrackedMouseButton(button, false, pointFromCPoint(point));
    if (m_mouseState.pressedButtons() == 0 && GetCapture() == this)
        ReleaseCapture();
}

BOOL CRdpSessionView::OnMouseWheel(UINT flags, short zDelta, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    if (m_process) {
        CPoint clientPoint(point);
        ScreenToClient(&clientPoint);
        const PointI pointer = pointFromCPoint(clientPoint);
        m_mouseState.notePointerPosition(pointer);
        m_process->sendWheel(PointI{0, zDelta}, pointer, currentViewSize());
    }
    return TRUE;
}

UINT CRdpSessionView::OnGetDlgCode()
{
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}
