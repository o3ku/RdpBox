#include <cassert>

#include <windows.h>

#include "common/ui/MainWindowShortcuts.h"

int main()
{
    using ui::MainWindowShortcutAction;
    using ui::isReservedMainWindowShortcut;
    using ui::shortcutActionForKey;

    // Defaults: strict modifier matching.
    assert(shortcutActionForKey(WM_KEYDOWN, true, false, false, 'N') == MainWindowShortcutAction::NewConnection);
    assert(shortcutActionForKey(WM_SYSKEYDOWN, true, false, false, 'N') == MainWindowShortcutAction::NewConnection);
    assert(shortcutActionForKey(WM_KEYDOWN, false, false, false, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, true, true, false, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, true, false, true, 'N') == MainWindowShortcutAction::None);

    assert(shortcutActionForKey(WM_KEYDOWN, true, false, false, 'P') == MainWindowShortcutAction::OpenConnections);
    assert(shortcutActionForKey(WM_KEYDOWN, true, true, false, 'P') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, true, false, true, 'P') == MainWindowShortcutAction::None);

    assert(shortcutActionForKey(WM_KEYDOWN, false, false, false, VK_F11) == MainWindowShortcutAction::ToggleFullScreen);
    assert(shortcutActionForKey(WM_KEYDOWN, true, false, false, VK_F11) == MainWindowShortcutAction::None);

    assert(shortcutActionForKey(WM_KEYUP, true, false, false, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_SYSKEYUP, true, false, false, 'P') == MainWindowShortcutAction::None);

    assert(isReservedMainWindowShortcut(true, false, false, 'N'));
    assert(isReservedMainWindowShortcut(true, false, false, 'P'));
    assert(!isReservedMainWindowShortcut(true, true, false, 'N'));
    assert(!isReservedMainWindowShortcut(true, false, true, 'N'));
    assert(!isReservedMainWindowShortcut(false, false, false, 'P'));

    // Custom configured chords are matched exactly.
    ui::MainWindowShortcutSettings custom;
    custom.newConnection = {'K', true, true, false};
    custom.openConnections = {VK_OEM_3, false, false, true};
    custom.toggleFullScreen = {VK_F6, false, false, false};
    ui::setCurrentMainWindowShortcuts(custom);

    assert(shortcutActionForKey(WM_KEYDOWN, true, true, false, 'K') == MainWindowShortcutAction::NewConnection);
    assert(shortcutActionForKey(WM_KEYDOWN, true, false, false, 'K') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_KEYDOWN, true, true, false, 'N') == MainWindowShortcutAction::None);
    assert(shortcutActionForKey(WM_SYSKEYDOWN, false, false, true, VK_OEM_3) == MainWindowShortcutAction::OpenConnections);
    assert(shortcutActionForKey(WM_KEYDOWN, false, false, false, VK_F6) == MainWindowShortcutAction::ToggleFullScreen);
    assert(shortcutActionForKey(WM_KEYDOWN, false, false, false, VK_F11) == MainWindowShortcutAction::None);

    assert(isReservedMainWindowShortcut(true, true, false, 'K'));
    assert(!isReservedMainWindowShortcut(true, false, false, 'K'));

    // Chord validation: modifierless printable keys are rejected.
    assert(ui::isValidShortcutChord({'N', false, false, false}) == false);
    assert(ui::isValidShortcutChord({'5', false, false, false}) == false);
    assert(ui::isValidShortcutChord({'N', true, false, false}));
    assert(ui::isValidShortcutChord({'N', false, true, false}));
    assert(ui::isValidShortcutChord({'N', false, false, true}));
    assert(ui::isValidShortcutChord({VK_F5, false, false, false}));
    assert(ui::isValidShortcutChord({VK_F24, false, false, false}));
    assert(ui::isValidShortcutChord({VK_NEXT, false, false, false}));
    assert(ui::isValidShortcutChord({VK_ESCAPE, false, false, false}));
    assert(ui::isValidShortcutChord({VK_DELETE, false, false, false}));
    assert(ui::isValidShortcutChord({0, true, false, false}) == false);
    assert(ui::isValidShortcutChord({0x1FF, true, false, false}) == false);

    // Chord text.
    assert(ui::shortcutChordText({'N', true, false, false}) == L"Ctrl+N");
    assert(ui::shortcutChordText({'P', true, true, false}) == L"Ctrl+Shift+P");
    assert(ui::shortcutChordText({'K', false, false, true}) == L"Alt+K");
    assert(ui::shortcutChordText({VK_F11, false, false, false}) == L"F11");
    assert(ui::shortcutChordText({VK_UP, false, false, false}) == L"Up");
    assert(ui::shortcutChordText({VK_DELETE, true, false, false}) == L"Ctrl+Del");
    assert(ui::shortcutChordText({0, false, false, false}) == L"(none)");
    assert(ui::shortcutChordText({VK_BACK, true, false, false}) == L"Ctrl+Backspace");
    assert(ui::shortcutChordText({VK_HOME, true, false, false}) == L"Ctrl+Home");

    return 0;
}
