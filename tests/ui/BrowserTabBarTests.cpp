#include <cassert>

#include <afxcmn.h>
#include <afxwin.h>

#include "ui/BrowserTabBar.h"

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
        if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK) {
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

    if (!AfxGetApp()) {
        static CWinApp testApp;
    }

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_WIN95_CLASSES | ICC_TAB_CLASSES;
    ::InitCommonControlsEx(&commonControls);

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

CPoint tabCenter(int index, int tabWidth, int height)
{
    return CPoint(index * tabWidth + tabWidth / 2, height / 2);
}

CPoint closeCenter(int index, int tabWidth, int height)
{
    const int right = (index + 1) * tabWidth - 6;
    const int left = right - 16;
    const int top = (height - 16) / 2;
    return CPoint((left + right) / 2, top + 8);
}
}

int main()
{
    assert(initializeMfc());

    RecordingWindow topLevel;
    assert(topLevel.CreateEx(0,
                             windowClassName(),
                             L"BrowserTabBarTests",
                             WS_OVERLAPPEDWINDOW,
                             120, 140, 900, 300,
                             nullptr,
                             0));

    BrowserTabBar tabBar;
    const CRect barRect(0, 0, 800, 40);
    assert(tabBar.create(&topLevel, barRect, 1001));

    int selectedIndexFromCallback = -1;
    int closedIndexFromCallback = -1;
    int reorderedFromIndex = -1;
    int reorderedToIndex = -1;
    tabBar.setSelectionChangedCallback([&](int index) { selectedIndexFromCallback = index; });
    tabBar.setCloseRequestedCallback([&](int index) { closedIndexFromCallback = index; });
    tabBar.setTabReorderedCallback([&](int fromIndex, int toIndex) {
        reorderedFromIndex = fromIndex;
        reorderedToIndex = toIndex;
    });

    assert(tabBar.insertTab(L"one") == 0);
    assert(tabBar.insertTab(L"two") == 1);
    assert(tabBar.insertTab(L"three") == 2);
    assert(tabBar.tabCount() == 3);
    assert(tabBar.selectedIndex() == 0);

    constexpr int kTabWidth = 220;
    constexpr int kHeight = 40;

    const CPoint secondTab = tabCenter(1, kTabWidth, kHeight);
    tabBar.OnLButtonDown(0, secondTab);
    assert(tabBar.selectedIndex() == 1);
    assert(selectedIndexFromCallback == 1);

    const CPoint secondTabClose = closeCenter(1, kTabWidth, kHeight);
    tabBar.OnLButtonDown(0, secondTabClose);
    assert(closedIndexFromCallback == 1);

    closedIndexFromCallback = -1;
    const CPoint thirdTab = tabCenter(2, kTabWidth, kHeight);
    tabBar.OnMButtonUp(0, thirdTab);
    assert(closedIndexFromCallback == 2);

    reorderedFromIndex = -1;
    reorderedToIndex = -1;
    const CPoint firstTab = tabCenter(0, kTabWidth, kHeight);
    tabBar.OnLButtonDown(0, firstTab);
    tabBar.OnMouseMove(0, thirdTab);
    assert(reorderedFromIndex == -1);
    assert(reorderedToIndex == -1);
    assert(tabBar.selectedIndex() == 0);
    tabBar.OnLButtonUp(0, thirdTab);
    assert(reorderedFromIndex == 0);
    assert(reorderedToIndex == 2);
    assert(tabBar.selectedIndex() == 2);

    CPoint secondTabScreen = secondTab;
    tabBar.ClientToScreen(&secondTabScreen);
    assert(tabBar.hitTestTabAtScreenPoint(secondTabScreen) == 1);

    CPoint emptyClientPoint(750, 20);
    CPoint emptyScreenPoint = emptyClientPoint;
    tabBar.ClientToScreen(&emptyScreenPoint);
    assert(tabBar.hitTestTabAtScreenPoint(emptyScreenPoint) == -1);

    topLevel.lastMessage = 0;
    topLevel.lastWParam = 0;
    topLevel.lastLParam = 0;
    tabBar.OnLButtonDown(0, emptyClientPoint);
    assert(topLevel.lastMessage == WM_NCLBUTTONDOWN);
    assert(topLevel.lastWParam == HTCAPTION);
    assert(GET_X_LPARAM(topLevel.lastLParam) == emptyScreenPoint.x);
    assert(GET_Y_LPARAM(topLevel.lastLParam) == emptyScreenPoint.y);

    topLevel.lastMessage = 0;
    topLevel.lastWParam = 0;
    topLevel.lastLParam = 0;
    tabBar.OnLButtonDblClk(0, emptyClientPoint);
    assert(topLevel.lastMessage == WM_NCLBUTTONDBLCLK);
    assert(topLevel.lastWParam == HTCAPTION);

    tabBar.setSelectedIndex(2);
    tabBar.removeTab(1);
    assert(tabBar.tabCount() == 2);
    assert(tabBar.selectedIndex() == 1);

    tabBar.removeTab(1);
    assert(tabBar.tabCount() == 1);
    assert(tabBar.selectedIndex() == 0);

    tabBar.clearTabs();
    assert(tabBar.tabCount() == 0);
    assert(tabBar.selectedIndex() == -1);

    tabBar.DestroyWindow();
    topLevel.DestroyWindow();
    return 0;
}
