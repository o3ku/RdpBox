#include <cassert>

#include <afxwin.h>

#include "ui/ParentResizeForwarder.h"

namespace
{
class RecordingWindow : public CWnd
{
public:
    UINT lastMessage = 0;
    WPARAM lastWParam = 0;
    LPARAM lastLParam = 0;

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override
    {
        if (message == WM_NCLBUTTONDOWN) {
            lastMessage = message;
            lastWParam = wParam;
            lastLParam = lParam;
            return 0;
        }

        return CWnd::WindowProc(message, wParam, lParam);
    }
};

bool initializeMfc()
{
    static bool initialized = false;
    if (initialized)
        return true;

    CWinApp *app = AfxGetApp();
    if (!app) {
        static CWinApp testApp;
    }

    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    initialized = AfxWinInit(instance, nullptr, ::GetCommandLineW(), 0) != FALSE;
    return initialized;
}

CString windowClassName()
{
    static CString className = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                                   ::LoadCursor(nullptr, IDC_ARROW),
                                                   reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                                   nullptr);
    return className;
}

RECT monitorRectForWindow(HWND hwnd)
{
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    const HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    assert(monitor != nullptr);
    assert(::GetMonitorInfoW(monitor, &info));
    return info.rcMonitor;
}
}

int main()
{
    assert(initializeMfc());

    RecordingWindow topLevel;
    assert(topLevel.CreateEx(0,
                             windowClassName(),
                             L"ParentResizeForwarderTests",
                             WS_OVERLAPPEDWINDOW,
                             120, 140, 640, 420,
                             nullptr,
                             0));

    CWnd child;
    assert(child.CreateEx(0,
                          windowClassName(),
                          L"Child",
                          WS_CHILD | WS_VISIBLE,
                          20, 20, 300, 120,
                          topLevel.GetSafeHwnd(),
                          reinterpret_cast<HMENU>(1)));

    const RECT windowRect = [] (HWND hwnd) {
        RECT rect = {};
        assert(::GetWindowRect(hwnd, &rect));
        return rect;
    }(topLevel.GetSafeHwnd());

    const CPoint nearLeft(windowRect.left + 1, (windowRect.top + windowRect.bottom) / 2);
    const CPoint nearTop(windowRect.left + 20, windowRect.top + 1);
    const CPoint nearBottomRight(windowRect.right - 1, windowRect.bottom - 1);
    const CPoint center((windowRect.left + windowRect.right) / 2, (windowRect.top + windowRect.bottom) / 2);

    assert(ParentResizeForwarder::hitTestParentFrame(&child, nearLeft) == HTLEFT);
    assert(ParentResizeForwarder::hitTestParentFrame(&child, nearTop) == HTTOP);
    assert(ParentResizeForwarder::hitTestParentFrame(&child, nearBottomRight) == HTBOTTOMRIGHT);
    assert(ParentResizeForwarder::hitTestParentFrame(&child, center) == 0);

    topLevel.lastMessage = 0;
    topLevel.lastWParam = 0;
    topLevel.lastLParam = 0;
    assert(ParentResizeForwarder::forwardLButtonDown(&child, nearLeft));
    assert(topLevel.lastMessage == WM_NCLBUTTONDOWN);
    assert(topLevel.lastWParam == HTLEFT);
    assert(GET_X_LPARAM(topLevel.lastLParam) == nearLeft.x);
    assert(GET_Y_LPARAM(topLevel.lastLParam) == nearLeft.y);

    topLevel.lastMessage = 0;
    assert(!ParentResizeForwarder::forwardLButtonDown(&child, center));
    assert(topLevel.lastMessage == 0);

    const RECT monitorRect = monitorRectForWindow(topLevel.GetSafeHwnd());
    assert(::SetWindowPos(topLevel.GetSafeHwnd(),
                          nullptr,
                          monitorRect.left,
                          monitorRect.top,
                          monitorRect.right - monitorRect.left,
                          monitorRect.bottom - monitorRect.top,
                          SWP_NOZORDER | SWP_NOACTIVATE));

    const CPoint fullScreenEdge(monitorRect.left + 1, (monitorRect.top + monitorRect.bottom) / 2);
    assert(ParentResizeForwarder::hitTestParentFrame(&child, fullScreenEdge) == 0);
    assert(!ParentResizeForwarder::forwardLButtonDown(&child, fullScreenEdge));

    child.DestroyWindow();
    topLevel.DestroyWindow();
    return 0;
}
