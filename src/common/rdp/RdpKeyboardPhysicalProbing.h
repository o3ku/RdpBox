#pragma once

#include "common/rdp/RdpKeyboardPhysicalState.h"

namespace rdp::physical_keyboard
{

// Physical (async) keyboard probing shared by the MFC view, the Qt view and
// the low-level keyboard hook. Off Windows there is no GetAsyncKeyState
// equivalent; probing reports "not down" and modifier sync falls back to
// per-event modifiers.
bool isVirtualKeyPhysicallyDown(unsigned int virtualKey);

RdpKeyboardPhysicalState physicalKeyboardState();

} // namespace rdp::physical_keyboard
