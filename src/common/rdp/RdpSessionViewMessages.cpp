#include "RdpSessionView.h"

#include "common/rdp/RdpSessionKeyboardHook.h"
#include "common/rdp/RdpInputEventUtil.h"
#include "common/ui/MainWindowShortcuts.h"
#include "common/rdp/RdpCertificatePromptBehavior.h"
#include "common/rdp/RdpProcessEventBehavior.h"

#include <utility>

namespace rdp::session_view_input
{
KeyboardMessageDisposition handleWindowKeyMessage(CRdpSessionView &target,
                                                  std::uint32_t message,
                                                  std::uintptr_t wParam,
                                                  std::intptr_t lParam)
{
    if (message != WM_KEYDOWN && message != WM_KEYUP && message != WM_SYSKEYDOWN && message != WM_SYSKEYUP)
        return KeyboardMessageDisposition::NotHandled;

    const bool down = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
    const bool controlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (down && ui::isReservedMainWindowShortcut(controlDown,
                                                 shiftDown,
                                                 altDown,
                                                 static_cast<unsigned int>(wParam))) {
        target.noteConsumedLocalShortcutKey(static_cast<unsigned int>(wParam));
        return KeyboardMessageDisposition::PassThrough;
    }

    if (!down && target.consumeReservedShortcutKey(static_cast<unsigned int>(wParam)))
        return KeyboardMessageDisposition::PassThrough;

    target.forwardNativeKeyMessage(static_cast<std::uint32_t>(message),
                                   static_cast<std::uintptr_t>(wParam),
                                   static_cast<std::intptr_t>(lParam));
    return KeyboardMessageDisposition::Handled;
}
}

namespace
{
rdp::certificate_prompt::Challenge promptChallengeFromProcessChallenge(
    const FreeRdpProcess::CertificateChallenge &challenge)
{
    return rdp::certificate_prompt::Challenge{
        challenge.host,
        challenge.port,
        challenge.commonName,
        challenge.subject,
        challenge.issuer,
        challenge.fingerprint,
        challenge.changed,
    };
}
}

LRESULT CRdpSessionView::OnRdpStateChanged(WPARAM state, LPARAM generation)
{
    if (!rdp::process_event::shouldHandleProcessEvent(m_processGeneration,
                                                      static_cast<std::uintptr_t>(generation)))
        return 0;

    onStateChanged(static_cast<FreeRdpProcess::State>(state));
    return 0;
}

LRESULT CRdpSessionView::OnRdpFrameUpdated(WPARAM, LPARAM generation)
{
    if (!rdp::process_event::shouldHandleFrameEvent(IsWindowVisible() != FALSE,
                                                    m_processGeneration,
                                                    static_cast<std::uintptr_t>(generation)))
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
    if (!rdp::process_event::shouldHandleProcessEvent(m_processGeneration,
                                                      static_cast<std::uintptr_t>(generation)))
        return 0;

    updateCursorFromProcess();
    return 0;
}

LRESULT CRdpSessionView::OnRdpCertRequest(WPARAM, LPARAM generation)
{
    const std::uintptr_t requestGeneration = static_cast<std::uintptr_t>(generation);
    const std::shared_ptr<ProcessBinding> binding = m_processBinding;
    const std::shared_ptr<FreeRdpProcess> process = m_process;
    if (!rdp::process_event::shouldHandleProcessEvent(m_processGeneration, requestGeneration)
        || !binding || !process) {
        if (binding) {
            std::scoped_lock lock(binding->certMutex);
            if (rdp::process_event::shouldClearPendingCertificateRequest(
                    binding->pendingCert.has_value(),
                    binding->pendingCert ? binding->pendingCert->generation : 0,
                    requestGeneration))
                binding->pendingCert.reset();
        }
        return 0;
    }

    std::optional<FreeRdpProcess::CertificateChallenge> challenge;
    {
        std::scoped_lock lock(binding->certMutex);
        if (!rdp::process_event::shouldShowCertificatePrompt(
                m_processGeneration,
                requestGeneration,
                true,
                true,
                binding->pendingCert.has_value(),
                binding->pendingCert ? binding->pendingCert->generation : 0))
            return 0;
        challenge = std::move(binding->pendingCert->challenge);
        binding->pendingCert.reset();
    }

    bool accept = false;
    if (challenge) {
        const auto prompt = rdp::certificate_prompt::promptForChallenge(
            promptChallengeFromProcessChallenge(*challenge));
        const UINT icon = prompt.icon == rdp::certificate_prompt::PromptIcon::Warning
            ? MB_ICONWARNING
            : MB_ICONQUESTION;
        accept = MessageBox(prompt.message.c_str(),
                            L"Verify Certificate",
                            MB_YESNO | icon | MB_DEFBUTTON2) == IDYES;
    }

    process->resolveCertificateChallenge(accept);
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

    const auto keyDisposition =
        rdp::session_view_input::handleWindowKeyMessage(*this, message, wParam, lParam);
    if (keyDisposition == rdp::session_view_input::KeyboardMessageDisposition::PassThrough)
        return CWnd::WindowProc(message, wParam, lParam);
    if (keyDisposition == rdp::session_view_input::KeyboardMessageDisposition::Handled)
        return 0;

    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
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

bool CRdpSessionView::hasWindowFocus() const
{
    return GetSafeHwnd() && ::GetFocus() == GetSafeHwnd();
}

unsigned int CRdpSessionView::activeKeyboardModifiers() const
{
    return m_keyboardState.activeKeyboardModifiers();
}

bool CRdpSessionView::shouldCaptureLowLevelKey(const RdpLowLevelKeyEvent &event,
                                               const RdpKeyboardPhysicalState &physical) const
{
    return m_keyboardState.shouldCaptureLowLevelKey(event, physical);
}

std::uint32_t CRdpSessionView::messageForLowLevelKey(const RdpLowLevelKeyEvent &event,
                                                     const RdpKeyboardPhysicalState &physical) const
{
    return m_keyboardState.messageForLowLevelKey(event, physical);
}

void CRdpSessionView::forwardNativeKeyMessage(std::uint32_t message,
                                              std::uintptr_t wParam,
                                              std::intptr_t lParam)
{
    if (!m_process)
        return;

    const unsigned int virtualKey = static_cast<unsigned int>(wParam);
    const auto event = keyEventInfoFromMessage(message, wParam, lParam);
    if (!event)
        return;

    sendKeyboardActions(m_keyboardState.handleKeyMessage(message,
                                                         virtualKey,
                                                         *event,
                                                         currentKeyboardPhysicalState(),
                                                         ::GetFocus() == GetSafeHwnd()));
    releaseKeyboardTargetIfInactive();
}
