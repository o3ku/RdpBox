#pragma once

namespace ui
{
enum class MainWindowShortcutAction
{
    None,
    NewConnection,
    OpenConnections
};

inline MainWindowShortcutAction shortcutActionForKey(bool controlDown, bool altDown, unsigned int virtualKey)
{
    if (!controlDown || !altDown)
        return MainWindowShortcutAction::None;

    switch (virtualKey) {
    case 'N':
        return MainWindowShortcutAction::NewConnection;
    case 'O':
        return MainWindowShortcutAction::OpenConnections;
    default:
        return MainWindowShortcutAction::None;
    }
}
}
