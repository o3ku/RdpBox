#include <afxwin.h>
#include <cassert>
#include <cstring>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include "mfcui/session/RdpSessionView.h"
#include "common/rdp/RdpCursorClassifier.h"
#include "common/rdp/FreeRdpProcessNative.h"

#include <cstdio>
#include <cstdlib>

#undef assert
#define assert(condition) do { if (!(condition)) { std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); std::exit(EXIT_FAILURE); } } while (false)
struct RdpCursorLifecycleTestAccess
{
    static void run(const char *scenario)
    {
        CWnd parent;
        const CString className = AfxRegisterWndClass(0, LoadCursor(nullptr, IDC_ARROW));
        assert(parent.CreateEx(0, className, L"Cursor lifecycle test", WS_OVERLAPPEDWINDOW,
                               0, 0, 300, 200, nullptr, 0));
        CRdpSessionView view;
        assert(view.create(&parent, CRect(0, 0, 250, 150)));
        POINT position{};
        assert(::GetCursorPos(&position));
        parent.SetWindowPos(nullptr, position.x - 80, position.y - 80, 0, 0,
                            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        parent.ShowWindow(SW_SHOWNOACTIVATE);
        view.m_process = std::make_shared<FreeRdpProcess>();
        view.m_process->updateStateFromBackend(FreeRdpProcess::State::Running);
        HCURSOR previous = ::GetCursor();

        if (std::strcmp(scenario, "empty") == 0) {
            assert(!view.m_cursorHandle);
            view.SendMessage(WM_SETCURSOR, reinterpret_cast<WPARAM>(view.GetSafeHwnd()),
                             MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            assert(::GetCursor() == LoadCursor(nullptr, IDC_ARROW));
        } else {
            FrameBuffer frame;
            frame.width = frame.height = 16;
            frame.stride = 64;
            frame.pixels.assign(16 * 64, 0xFF);
            CursorInfo custom = RdpCursorClassifier::createCursor(frame, {0, 0});
            assert(custom.handle);
            view.m_process->updateCursorFromBackend(custom);
            destroyCursorInfo(custom);
            view.updateCursorFromProcess();
            const HCURSOR expected = view.m_cursorHandle;
            assert(expected);
            view.m_remoteCursor.noteReleased();
            ::SetCursor(LoadCursor(nullptr, IDC_IBEAM));
            if (std::strcmp(scenario, "paint") == 0)
                view.RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            else
                view.applyRemoteCursor();
            assert(::GetCursor() == expected);
            view.releaseCursorHandle();
            view.m_remoteCursor.noteReleased();
            view.SendMessage(WM_SETCURSOR, reinterpret_cast<WPARAM>(view.GetSafeHwnd()),
                             MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            assert(::GetCursor() == LoadCursor(nullptr, IDC_ARROW));
            assert(view.m_remoteCursor.needsApply());
        }
        // Hidden is intentional only when explicitly requested by the session.
        view.m_process->updateCursorFromBackend({CursorKind::Hidden, nullptr, false});
        view.updateCursorFromProcess();
        view.SendMessage(WM_SETCURSOR, reinterpret_cast<WPARAM>(view.GetSafeHwnd()),
                         MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        assert(::GetCursor() == nullptr);
        ::SetCursor(previous);
        view.DestroyWindow();
        parent.DestroyWindow();
    }
};

int main(int argc, char **argv)
{
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    CWinApp app;
    assert(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), 0));
    RdpCursorLifecycleTestAccess::run(argc > 1 ? argv[1] : "empty");
    return 0;
}
