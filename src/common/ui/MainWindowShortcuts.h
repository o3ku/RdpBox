#pragma once

#include "common/NativeTypes.h"
#include "common/rdp/RdpWinKeyCodes.h"

#include <string>

namespace ui
{

enum class MainWindowShortcutAction
{
    None,
    NewConnection,
    OpenConnections,
    ToggleFullScreen
};

struct ShortcutChord
{
    unsigned int virtualKey = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool operator==(const ShortcutChord &other) const;
};

struct MainWindowShortcutSettings
{
    ShortcutChord newConnection{'N', true, false, false};
    ShortcutChord openConnections{'P', true, false, false};
    ShortcutChord toggleFullScreen{VK_F11, false, false, false};
    ShortcutChord exitFullScreen{VK_ESCAPE, false, false, false}; // fixed, not editable

    bool operator==(const MainWindowShortcutSettings &other) const;
};

// Process-wide configured shortcuts. Defaults until loadMainWindowShortcutSettings() runs.
const MainWindowShortcutSettings &currentMainWindowShortcuts();
void setCurrentMainWindowShortcuts(const MainWindowShortcutSettings &settings);

// Exact modifier+key match against the configured shortcuts: Ctrl+Shift+N does
// NOT trigger a Ctrl+N shortcut. Both plain (WM_KEYDOWN) and Alt (WM_SYSKEYDOWN)
// key messages are considered; matching is chord-based.
MainWindowShortcutAction shortcutActionForKey(UINT message,
                                              bool controlDown,
                                              bool shiftDown,
                                              bool altDown,
                                              unsigned int virtualKey);

// True when the key event is a configured shortcut that must not reach the
// remote session. Same exact matching as shortcutActionForKey.
bool isReservedMainWindowShortcut(bool controlDown,
                                  bool shiftDown,
                                  bool altDown,
                                  unsigned int virtualKey);

// Editable chords must carry a modifier, or be a non-text key (F-keys,
// navigation cluster, Esc, Ins/Del, PrintScreen) so typing in ordinary
// controls never fires a shortcut.
bool isValidShortcutChord(const ShortcutChord &chord);

std::wstring shortcutChordText(const ShortcutChord &chord);

}
