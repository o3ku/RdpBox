#include "RdpSessionView.h"

#include "ui/MainWindowShortcuts.h"

#include <utility>

LRESULT CRdpSessionView::OnRdpStateChanged(WPARAM state, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)))
        return 0;

    onStateChanged(static_cast<FreeRdpProcess::State>(state));
    return 0;
}

LRESULT CRdpSessionView::OnRdpFrameUpdated(WPARAM, LPARAM generation)
{
    if (!IsWindowVisible())
        return 0;

    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)))
        return 0;

    // Drain any queued frame messages so we only paint the latest.
    MSG msg = {};
    while (::PeekMessageW(&msg, GetSafeHwnd(), WM_APP_RDP_FRAME, WM_APP_RDP_FRAME, PM_REMOVE)) {
        // discarded stale frame notification
    }

    Invalidate(FALSE);
    return 0;
}

LRESULT CRdpSessionView::OnRdpCursorUpdated(WPARAM, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)))
        return 0;

    updateCursorFromProcess();
    return 0;
}

LRESULT CRdpSessionView::OnRdpCertRequest(WPARAM, LPARAM generation)
{
    if (!isCurrentGeneration(static_cast<std::uintptr_t>(generation)) || !m_process)
        return 0;

    std::optional<FreeRdpProcess::CertificateChallenge> challenge;
    {
        std::scoped_lock lock(m_certMutex);
        challenge = std::move(m_pendingCert);
    }

    bool accept = false;
    if (challenge) {
        CString message;
        message.Format(L"%s\n\nHost: %s:%d\nCommon Name: %s\nSubject: %s\nIssuer: %s\nFingerprint: %s\n\nAccept this certificate?",
                       challenge->changed
                           ? L"The remote host's certificate has CHANGED since the previous connection."
                           : L"The remote host's certificate could not be verified.",
                       challenge->host.c_str(), challenge->port,
                       challenge->commonName.c_str(),
                       challenge->subject.c_str(),
                       challenge->issuer.c_str(),
                       challenge->fingerprint.c_str());
        const UINT icon = challenge->changed ? MB_ICONWARNING : MB_ICONQUESTION;
        accept = MessageBox(message, L"Verify Certificate", MB_YESNO | icon | MB_DEFBUTTON2) == IDYES;
    }

    m_process->resolveCertificateChallenge(accept);
    return 0;
}

LRESULT CRdpSessionView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_MOUSEACTIVATE) {
        setFocusToFreeRdp();
        return MA_ACTIVATE;
    }

    if (message == WM_IME_SETCONTEXT
        || message == WM_IME_STARTCOMPOSITION
        || message == WM_IME_COMPOSITION
        || message == WM_IME_ENDCOMPOSITION
        || message == WM_IME_NOTIFY
        || message == WM_IME_CHAR
        || message == WM_CHAR
        || message == WM_SYSCHAR
        || message == WM_UNICHAR
        || message == WM_DEADCHAR
        || message == WM_SYSDEADCHAR) {
        return 0;
    }

    if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
        if (m_process && m_process->state() == FreeRdpProcess::State::Running) {
            ::SetCursor(m_cursorHandle);
            return TRUE;
        }
    }

    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
        const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            && ui::isReservedMainWindowShortcut(controlDown,
                                                altDown,
                                                static_cast<unsigned int>(wParam))) {
            return CWnd::WindowProc(message, wParam, lParam);
        }

        forwardNativeKeyMessage(static_cast<std::uint32_t>(message),
                                static_cast<std::uintptr_t>(wParam),
                                static_cast<std::intptr_t>(lParam));

        const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
        m_modifierTracker.recordKeyState(static_cast<unsigned int>(wParam), down);
        return 0;
    }

    return CWnd::WindowProc(message, wParam, lParam);
}

bool CRdpSessionView::canCaptureSystemKeys() const
{
    return m_process
        && m_process->state() == FreeRdpProcess::State::Running
        && GetSafeHwnd()
        && IsWindowVisible()
        && ::GetFocus() == GetSafeHwnd();
}

void CRdpSessionView::forwardNativeKeyMessage(std::uint32_t message,
                                              std::uintptr_t wParam,
                                              std::intptr_t lParam)
{
    if (!m_process)
        return;

    m_process->sendKeyMessage(message, wParam, lParam);
}
