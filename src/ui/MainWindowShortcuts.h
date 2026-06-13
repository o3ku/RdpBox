#pragma once

#include <windows.h>

namespace ui
{
enum class MainWindowShortcutAction
{
    None,
    NewConnection,
    OpenConnections
};

inline MainWindowShortcutAction shortcutActionForKey(UINT message,
                                                     bool controlDown,
                                                     bool altDown,
                                                     unsigned int virtualKey)
{
    (void)altDown;

    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
        return MainWindowShortcutAction::None;

    if (!controlDown)
        return MainWindowShortcutAction::None;

    switch (virtualKey) {
    case 'N':
        return MainWindowShortcutAction::NewConnection;
    case 'P':
        return MainWindowShortcutAction::OpenConnections;
    default:
        return MainWindowShortcutAction::None;
    }
}

inline bool isReservedMainWindowShortcut(bool controlDown, bool altDown, unsigned int virtualKey)
{
    return shortcutActionForKey(WM_SYSKEYDOWN, controlDown, altDown, virtualKey) != MainWindowShortcutAction::None;
}
}
