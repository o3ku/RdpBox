#include "RdpSessionView.h"

#include "rdp/RdpInputModifiers.h"
#include "rdp/RdpReconnectInteraction.h"
#include "ui/ParentResizeForwarder.h"

namespace
{
constexpr UINT kMouseMoveCoalesceMs = 16;
}

void CRdpSessionView::syncMouseModifiers(UINT mouseFlags)
{
    if (!m_process)
        return;

    sendKeyboardActions(m_keyboardRouter.synchronizeMouseModifiers(
        mouseFlags,
        currentKeyboardPhysicalState(),
        ::GetFocus() == GetSafeHwnd()));
    releaseKeyboardTargetIfInactive();
}

void CRdpSessionView::flushPendingMouseMove()
{
    if (!m_process) {
        if (m_mouseMoveTimerActive) {
            KillTimer(kMouseMoveTimerId);
            m_mouseMoveTimerActive = false;
        }
        return;
    }

    const auto pending = m_mouseMoveCoalescer.flush();
    if (pending)
        m_process->sendMouseMove(*pending, currentViewSize());

    if (m_mouseMoveTimerActive) {
        KillTimer(kMouseMoveTimerId);
        m_mouseMoveTimerActive = false;
    }
}

void CRdpSessionView::OnMouseMove(UINT flags, CPoint point)
{
    updatePointerPosition(PointI{point.x, point.y});
    if (rdp::shouldSynchronizeModifiersForMouseMove(flags))
        syncMouseModifiers(flags);
    if (!m_process)
        return;

    const auto immediate = m_mouseMoveCoalescer.onMouseMove(PointI{point.x, point.y});
    if (immediate)
        m_process->sendMouseMove(*immediate, currentViewSize());

    if (!m_mouseMoveTimerActive) {
        SetTimer(kMouseMoveTimerId, kMouseMoveCoalesceMs, nullptr);
        m_mouseMoveTimerActive = true;
    }
}

void CRdpSessionView::OnLButtonDown(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    const bool processFinished = m_process && m_process->state() == FreeRdpProcess::State::Finished;
    if (shouldReconnectOnPointerDown(m_profile.isValid(),
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
    updatePointerPosition(PointI{point.x, point.y});
    SetCapture();
    sendTrackedMouseButton(MouseButton::Left, true, PointI{point.x, point.y});
}

void CRdpSessionView::OnLButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    updatePointerPosition(PointI{point.x, point.y});
    sendTrackedMouseButton(MouseButton::Left, false, PointI{point.x, point.y});
    if (m_pressedMouseButtons == 0 && GetCapture() == this)
        ReleaseCapture();
}

void CRdpSessionView::OnLButtonDblClk(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    const bool processFinished = m_process && m_process->state() == FreeRdpProcess::State::Finished;
    if (shouldReconnectOnPointerDown(m_profile.isValid(),
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
    updatePointerPosition(PointI{point.x, point.y});
    SetCapture();
    sendTrackedMouseButton(MouseButton::Left, true, PointI{point.x, point.y});
}

void CRdpSessionView::OnRButtonDown(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    updatePointerPosition(PointI{point.x, point.y});
    SetCapture();
    sendTrackedMouseButton(MouseButton::Right, true, PointI{point.x, point.y});
}

void CRdpSessionView::OnRButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    updatePointerPosition(PointI{point.x, point.y});
    sendTrackedMouseButton(MouseButton::Right, false, PointI{point.x, point.y});
    if (m_pressedMouseButtons == 0 && GetCapture() == this)
        ReleaseCapture();
}

void CRdpSessionView::OnRButtonDblClk(UINT flags, CPoint point)
{
    OnRButtonDown(flags, point);
}

void CRdpSessionView::OnMButtonDown(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    updatePointerPosition(PointI{point.x, point.y});
    SetCapture();
    sendTrackedMouseButton(MouseButton::Middle, true, PointI{point.x, point.y});
}

void CRdpSessionView::OnMButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    updatePointerPosition(PointI{point.x, point.y});
    sendTrackedMouseButton(MouseButton::Middle, false, PointI{point.x, point.y});
    if (m_pressedMouseButtons == 0 && GetCapture() == this)
        ReleaseCapture();
}

void CRdpSessionView::OnMButtonDblClk(UINT flags, CPoint point)
{
    OnMButtonDown(flags, point);
}

BOOL CRdpSessionView::OnMouseWheel(UINT flags, short zDelta, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    if (m_process) {
        CPoint clientPoint(point);
        ScreenToClient(&clientPoint);
        updatePointerPosition(PointI{clientPoint.x, clientPoint.y});
        m_process->sendWheel(PointI{0, zDelta}, PointI{clientPoint.x, clientPoint.y}, currentViewSize());
    }
    return TRUE;
}

UINT CRdpSessionView::OnGetDlgCode()
{
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}
