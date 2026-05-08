#include <cassert>

#include <windows.h>

#include "ui/MainWindowShortcuts.h"

int main()
{
    using ui::MainWindowShortcutAction;
    using ui::isReservedMainWindowShortcut;
    using ui::shortcutActionForKey;

    assert(shortcutActionForKey(WM_KEYDOWN, true, false, 'P') == MainWindowShortcutAction::OpenConnections);
    assert(shortcutActionForKey(WM_KEYDOWN, true, true, 'P') == MainWindowShortcutAction::OpenConnections);
    assert(shortcutActionForKey(WM_SYSKEYDOWN, true, false, 'P') == MainWindowShortcutAction::OpenConnections);
    assert(shortcutActionForKey(WM_SYSKEYDOWN, true, true, 'P') == MainWindowShortcutAction::OpenConnections);

    assert(shortcutActionForKey(WM_KEYDOWN, true, false, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, true, false, 'O') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, false, true, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, false, true, 'O') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, false, false, 'P') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYUP, true, false, 'P') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_SYSKEYUP, true, true, 'P') == MainWindowShortcutAction::None);

    assert(isReservedMainWindowShortcut(true, false, 'P'));
    assert(isReservedMainWindowShortcut(true, true, 'P'));
    assert(!isReservedMainWindowShortcut(true, false, 'N'));
    assert(!isReservedMainWindowShortcut(false, false, 'P'));
    assert(!isReservedMainWindowShortcut(false, true, 'P'));

    return 0;
}
