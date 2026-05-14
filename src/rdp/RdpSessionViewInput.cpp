#include "RdpSessionView.h"

#include "rdp/RdpInputModifiers.h"
#include "ui/ParentResizeForwarder.h"

#include <vector>

namespace
{
constexpr UINT_PTR kMouseMoveTimerId = 2;
constexpr UINT kMouseMoveCoalesceMs = 16;
}

void CRdpSessionView::syncMouseModifiers(UINT mouseFlags)
{
    if (!m_process)
        return;

    const std::vector<RdpModifierSyncTracker::KeyAction> actions =
        m_modifierTracker.synchronize(
            rdp::mouseInputModifiers(mouseFlags, currentModifiers()));
    for (const auto &action : actions)
        m_process->sendKeyMessage(action.message, action.virtualKey, 0);
}

unsigned int CRdpSessionView::currentModifiers() const
{
    unsigned int modifiers = ModifierNone;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        modifiers |= ModifierControl;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        modifiers |= ModifierShift;
    if (GetKeyState(VK_MENU) & 0x8000)
        modifiers |= ModifierAlt;
    return modifiers;
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

    if (m_process && m_process->state() == FreeRdpProcess::State::Finished) {
        if (m_reconnectRequested)
            m_reconnectRequested();
        return;
    }

    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(MouseButton::Left, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnLButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(MouseButton::Left, false, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnLButtonDblClk(UINT flags, CPoint point)
{
    CPoint screenPoint(point);
    ClientToScreen(&screenPoint);
    if (ParentResizeForwarder::forwardLButtonDown(this, screenPoint))
        return;

    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(MouseButton::Left, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnRButtonDown(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    setFocusToFreeRdp();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(MouseButton::Right, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnRButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(MouseButton::Right, false, PointI{point.x, point.y}, currentViewSize());
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
    if (m_process)
        m_process->sendMouseButton(MouseButton::Middle, true, PointI{point.x, point.y}, currentViewSize());
}

void CRdpSessionView::OnMButtonUp(UINT flags, CPoint point)
{
    flushPendingMouseMove();
    syncMouseModifiers(flags);
    if (m_process)
        m_process->sendMouseButton(MouseButton::Middle, false, PointI{point.x, point.y}, currentViewSize());
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
        m_process->sendWheel(PointI{0, zDelta}, PointI{clientPoint.x, clientPoint.y}, currentViewSize());
    }
    return TRUE;
}

UINT CRdpSessionView::OnGetDlgCode()
{
    return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}
