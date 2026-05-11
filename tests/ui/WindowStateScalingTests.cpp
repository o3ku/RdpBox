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

    return 0;
}
