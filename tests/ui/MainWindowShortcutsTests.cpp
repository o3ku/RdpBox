#include <cassert>

#include <windows.h>

#include "ui/MainWindowShortcuts.h"

int main()
{
    using ui::MainWindowShortcutAction;
    using ui::shortcutActionForKey;

    assert(shortcutActionForKey(true, true, 'N') == MainWindowShortcutAction::NewConnection);
    assert(shortcutActionForKey(true, true, 'O') == MainWindowShortcutAction::OpenConnections);

    assert(shortcutActionForKey(true, false, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(true, false, 'O') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(false, true, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(false, true, 'O') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(true, true, 'P') == MainWindowShortcutAction::None);

    return 0;
}
