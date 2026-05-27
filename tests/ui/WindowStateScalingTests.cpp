#include <cassert>

#include <windows.h>

#include "profiles/ProfileRepository.h"
#include "ui/WindowStateScaling.h"

namespace
{
RECT makeRect(int left, int top, int right, int bottom)
{
    RECT rect = { left, top, right, bottom };
    return rect;
}
}

int main()
{
    {
        const RECT monitorRect = makeRect(0, 0, 1920, 1080);
        const RECT workArea = makeRect(0, 40, 1920, 1080);
        const RECT workspace = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, workArea);
        assert(workspace.left == 0);
        assert(workspace.top == 0);
        assert(workspace.right == 1920);
        assert(workspace.bottom == 1040);
    }

    {
        const RECT monitorRect = makeRect(0, 0, 1920, 1080);
        const RECT leftDockWorkArea = makeRect(80, 0, 1920, 1080);
        const RECT workspace = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, leftDockWorkArea);
        assert(workspace.left == 0);
        assert(workspace.top == 0);
        assert(workspace.right == 1840);
        assert(workspace.bottom == 1080);
    }

    {
        const RECT monitorRect = makeRect(0, 0, 1920, 1080);
        const RECT rightDockWorkArea = makeRect(0, 0, 1840, 1080);
        const RECT workspace = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, rightDockWorkArea);
        assert(workspace.left == 80);
        assert(workspace.top == 0);
        assert(workspace.right == 1920);
        assert(workspace.bottom == 1080);
    }

    {
        const RECT monitorRect = makeRect(0, 0, 1920, 1080);
        const RECT bottomDockWorkArea = makeRect(0, 0, 1920, 1000);
        const RECT workspace = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, bottomDockWorkArea);
        assert(workspace.left == 0);
        assert(workspace.top == 80);
        assert(workspace.right == 1920);
        assert(workspace.bottom == 1080);
    }

    {
        const RECT workArea = makeRect(0, 0, 1920, 1080);
        const RECT windowRect = makeRect(192, 108, 1152, 648);

        WindowState state;
        const bool saved = WindowStateScaling::saveToMonitorWorkArea(windowRect, workArea, SW_SHOWNORMAL, state);
        assert(saved);
        assert(state.valid);
        assert(state.leftRatio > 0.09 && state.leftRatio < 0.11);
        assert(state.topRatio > 0.09 && state.topRatio < 0.11);
        assert(state.widthRatio > 0.49 && state.widthRatio < 0.51);
        assert(state.heightRatio > 0.49 && state.heightRatio < 0.51);

        RECT restoredRect = {};
        const RECT targetWorkArea = makeRect(0, 0, 1280, 720);
        const bool restored = WindowStateScaling::restoreFromMonitorWorkArea(state, targetWorkArea, restoredRect);
        assert(restored);
        assert(restoredRect.left == 128);
        assert(restoredRect.top == 72);
        assert(restoredRect.right == 768);
        assert(restoredRect.bottom == 432);
    }

    {
        WindowState state;
        state.leftRatio = 0.9;
        state.topRatio = 0.9;
        state.widthRatio = 0.5;
        state.heightRatio = 0.5;
        state.showCmd = SW_SHOWNORMAL;
        state.valid = true;

        RECT restoredRect = {};
        const RECT targetWorkArea = makeRect(100, 50, 1380, 770);
        const bool restored = WindowStateScaling::restoreFromMonitorWorkArea(state, targetWorkArea, restoredRect);
        assert(restored);
        assert(restoredRect.right <= targetWorkArea.right);
        assert(restoredRect.bottom <= targetWorkArea.bottom);
        assert(restoredRect.left >= targetWorkArea.left);
        assert(restoredRect.top >= targetWorkArea.top);
    }

    {
        const RECT workArea = makeRect(0, 0, 1920, 1080);
        const RECT windowRect = makeRect(160, 90, 1120, 630);

        WindowState state;
        const bool saved = WindowStateScaling::saveToMonitorWorkArea(windowRect, workArea, SW_SHOWMINIMIZED, state);
        assert(saved);
        assert(state.valid);
        assert(state.showCmd == SW_SHOWNORMAL);
    }

    {
        WindowState invalidState;
        RECT restoredRect = {};
        const RECT targetWorkArea = makeRect(0, 0, 1280, 720);
        assert(!WindowStateScaling::restoreFromMonitorWorkArea(invalidState, targetWorkArea, restoredRect));
    }

    {
        const RECT invalidWorkArea = makeRect(0, 0, 0, 720);
        const RECT validWindowRect = makeRect(10, 10, 410, 310);
        WindowState state;
        assert(!WindowStateScaling::saveToMonitorWorkArea(validWindowRect, invalidWorkArea, SW_SHOWNORMAL, state));
    }

    {
        WindowState state;
        state.leftRatio = 0.0;
        state.topRatio = 0.0;
        state.widthRatio = 0.01;
        state.heightRatio = 0.01;
        state.showCmd = SW_SHOWNORMAL;
        state.valid = true;

        RECT restoredRect = {};
        const RECT targetWorkArea = makeRect(50, 70, 1450, 970);
        const bool restored = WindowStateScaling::restoreFromMonitorWorkArea(state, targetWorkArea, restoredRect);
        assert(restored);
        assert(restoredRect.left == 50);
        assert(restoredRect.top == 70);
        assert(restoredRect.right - restoredRect.left == 400);
        assert(restoredRect.bottom - restoredRect.top == 300);
    }

    return 0;
}
