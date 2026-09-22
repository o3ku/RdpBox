#pragma once

#include "common/NativeTypes.h"
#include "common/rdp/RdpWinKeyCodes.h"

#include "common/profiles/ProfileRepository.h"

namespace WindowStateScaling
{
RECT workspaceRectForMonitorWorkArea(const RECT &monitorRect,
                                     const RECT &workAreaRect);

bool saveToMonitorWorkArea(const RECT &windowRect,
                           const RECT &workArea,
                           int showCmd,
                           WindowState &state);

bool restoreFromMonitorWorkArea(const WindowState &state,
                                const RECT &workArea,
                                RECT &windowRect);
}
