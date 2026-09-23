#include "common/ui/MainWindowShortcuts.h"

#include <cwchar>

namespace ui
{

bool ShortcutChord::operator==(const ShortcutChord &other) const
{
    return virtualKey == other.virtualKey
        && ctrl == other.ctrl
        && shift == other.shift
        && alt == other.alt;
}

bool MainWindowShortcutSettings::operator==(const MainWindowShortcutSettings &other) const
{
    return newConnection == other.newConnection
        && openConnections == other.openConnections
        && toggleFullScreen == other.toggleFullScreen
        && exitFullScreen == other.exitFullScreen;
}

namespace
{
MainWindowShortcutSettings &mutableShortcuts()
{
    static MainWindowShortcutSettings settings;
    return settings;
}

bool chordMatches(const ShortcutChord &chord,
                  bool controlDown,
                  bool shiftDown,
                  bool altDown,
                  unsigned int virtualKey)
{
    return chord.virtualKey == virtualKey
        && chord.ctrl == controlDown
        && chord.shift == shiftDown
        && chord.alt == altDown;
}
}

const MainWindowShortcutSettings &currentMainWindowShortcuts()
{
    return mutableShortcuts();
}

void setCurrentMainWindowShortcuts(const MainWindowShortcutSettings &settings)
{
    mutableShortcuts() = settings;
}

MainWindowShortcutAction shortcutActionForKey(UINT message,
                                              bool controlDown,
                                              bool shiftDown,
                                              bool altDown,
                                              unsigned int virtualKey)
{
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
        return MainWindowShortcutAction::None;

    const MainWindowShortcutSettings &settings = currentMainWindowShortcuts();
    if (chordMatches(settings.newConnection, controlDown, shiftDown, altDown, virtualKey))
        return MainWindowShortcutAction::NewConnection;
    if (chordMatches(settings.openConnections, controlDown, shiftDown, altDown, virtualKey))
        return MainWindowShortcutAction::OpenConnections;
    if (chordMatches(settings.toggleFullScreen, controlDown, shiftDown, altDown, virtualKey))
        return MainWindowShortcutAction::ToggleFullScreen;
    return MainWindowShortcutAction::None;
}

bool isReservedMainWindowShortcut(bool controlDown,
                                  bool shiftDown,
                                  bool altDown,
                                  unsigned int virtualKey)
{
    const MainWindowShortcutSettings &settings = currentMainWindowShortcuts();
    return chordMatches(settings.newConnection, controlDown, shiftDown, altDown, virtualKey)
        || chordMatches(settings.openConnections, controlDown, shiftDown, altDown, virtualKey);
}

bool isValidShortcutChord(const ShortcutChord &chord)
{
    if (chord.virtualKey == 0 || chord.virtualKey > 0xFF)
        return false;
    if (chord.ctrl || chord.shift || chord.alt)
        return true;

    // ponytail: modifierless whitelist covers function/navigation keys; plain
    // letters/digits stay reserved for typing until someone needs them
    return (chord.virtualKey >= VK_F1 && chord.virtualKey <= VK_F24)
        || (chord.virtualKey >= VK_PRIOR && chord.virtualKey <= VK_DOWN)
        || chord.virtualKey == VK_ESCAPE
        || chord.virtualKey == VK_SNAPSHOT
        || chord.virtualKey == VK_INSERT
        || chord.virtualKey == VK_DELETE;
}

std::wstring shortcutChordText(const ShortcutChord &chord)
{
    std::wstring text;
    if (chord.virtualKey == 0)
        return L"(none)";
    if (chord.ctrl)
        text += L"Ctrl+";
    if (chord.shift)
        text += L"Shift+";
    if (chord.alt)
        text += L"Alt+";

    if (chord.virtualKey >= VK_F1 && chord.virtualKey <= VK_F24)
        text += L"F" + std::to_wstring(chord.virtualKey - VK_F1 + 1);
    else if (chord.virtualKey == VK_PRIOR)
        text += L"PgUp";
    else if (chord.virtualKey == VK_NEXT)
        text += L"PgDn";
    else if (chord.virtualKey == VK_SNAPSHOT)
        text += L"PrtSc";
    else if (chord.virtualKey == VK_ESCAPE)
        text += L"Esc";
    else if (chord.virtualKey >= VK_LEFT && chord.virtualKey <= VK_DOWN) {
        static const wchar_t *const arrowNames[] = {L"Left", L"Up", L"Right", L"Down"};
        text += arrowNames[chord.virtualKey - VK_LEFT];
    }
    else if (chord.virtualKey >= 'A' && chord.virtualKey <= 'Z')
        text += static_cast<wchar_t>(chord.virtualKey);
    else if (chord.virtualKey >= '0' && chord.virtualKey <= '9')
        text += static_cast<wchar_t>(chord.virtualKey);
    else {
        static const struct
        {
            unsigned int virtualKey;
            const wchar_t *name;
        } kKeyNames[] = {
            {VK_BACK, L"Backspace"},
            {VK_TAB, L"Tab"},
            {VK_RETURN, L"Enter"},
            {VK_PAUSE, L"Pause"},
            {VK_CAPITAL, L"CapsLock"},
            {VK_SPACE, L"Space"},
            {VK_PRIOR, L"PgUp"},
            {VK_NEXT, L"PgDn"},
            {VK_END, L"End"},
            {VK_HOME, L"Home"},
            {VK_LEFT, L"Left"},
            {VK_UP, L"Up"},
            {VK_RIGHT, L"Right"},
            {VK_DOWN, L"Down"},
            {VK_SNAPSHOT, L"PrtSc"},
            {VK_INSERT, L"Ins"},
            {VK_DELETE, L"Del"},
            {VK_NUMPAD0, L"Num 0"},
            {VK_NUMPAD1, L"Num 1"},
            {VK_NUMPAD2, L"Num 2"},
            {VK_NUMPAD3, L"Num 3"},
            {VK_NUMPAD4, L"Num 4"},
            {VK_NUMPAD5, L"Num 5"},
            {VK_NUMPAD6, L"Num 6"},
            {VK_NUMPAD7, L"Num 7"},
            {VK_NUMPAD8, L"Num 8"},
            {VK_NUMPAD9, L"Num 9"},
            {VK_MULTIPLY, L"Num *"},
            {VK_ADD, L"Num +"},
            {VK_SUBTRACT, L"Num -"},
            {VK_DECIMAL, L"Num ."},
            {VK_DIVIDE, L"Num /"},
            {VK_OEM_1, L";"},
            {VK_OEM_PLUS, L"="},
            {VK_OEM_COMMA, L","},
            {VK_OEM_MINUS, L"-"},
            {VK_OEM_PERIOD, L"."},
            {VK_OEM_2, L"/"},
            {VK_OEM_3, L"`"},
            {VK_OEM_4, L"["},
            {VK_OEM_5, L"\\"},
            {VK_OEM_6, L"]"},
            {VK_OEM_7, L"'"},
        };
        bool named = false;
        for (const auto &entry : kKeyNames) {
            if (chord.virtualKey == entry.virtualKey) {
                text += entry.name;
                named = true;
                break;
            }
        }
        if (!named)
            text += L"Key " + std::to_wstring(chord.virtualKey);
    }
    return text;
}

}
