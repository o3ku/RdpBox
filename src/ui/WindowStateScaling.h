#pragma once

#include <windows.h>

#include "profiles/ProfileRepository.h"

namespace WindowStateScaling
{
bool saveToMonitorWorkArea(const RECT &windowRect,
                           const RECT &workArea,
                           int showCmd,
                           WindowState &state);

bool restoreFromMonitorWorkArea(const WindowState &state,
                                const RECT &workArea,
                                RECT &windowRect);
}
