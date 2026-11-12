#include "qtui/QtShortcutSettings.h"

#include "common/ui/MainWindowShortcuts.h"

namespace rdpbox
{

bool ShortcutSettings::operator==(const ShortcutSettings &other) const
{
    return newConnection == other.newConnection
        && openConnections == other.openConnections
        && toggleFullScreen == other.toggleFullScreen
        && exitFullScreen == other.exitFullScreen;
}

namespace
{
ShortcutSettings &mutableShortcuts()
{
    static ShortcutSettings settings;
    return settings;
}
}

const ShortcutSettings &currentShortcuts()
{
    return mutableShortcuts();
}

namespace
{
// Qt key code -> Win32 virtual key for the keys a shortcut may use. Letters,
// digits and Space already share their values with VK_*; the rest need the
// table.
unsigned int virtualKeyFromQtKey(int key)
{
    if ((key >= Qt::Key_0 && key <= Qt::Key_9) || (key >= Qt::Key_A && key <= Qt::Key_Z)
        || key == Qt::Key_Space)
        return static_cast<unsigned int>(key);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return static_cast<unsigned int>(VK_F1 + (key - Qt::Key_F1));

    switch (key) {
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Return:
    case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Delete: return VK_DELETE;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_Print: return VK_SNAPSHOT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    default: return 0;
    }
}

ui::ShortcutChord chordFromSequence(const QKeySequence &sequence)
{
    if (sequence.count() != 1)
        return {};

    const int combined = sequence[0];
    const int modifiers = combined & static_cast<int>(Qt::KeyboardModifierMask);
    // The shared chord carries Ctrl/Shift/Alt only: a Meta combination has no
    // representation, so it stays unmatched (virtualKey 0) instead of matching
    // the wrong chord.
    if ((modifiers & Qt::MetaModifier) != 0)
        return {};

    ui::ShortcutChord chord;
    chord.virtualKey = virtualKeyFromQtKey(combined & ~static_cast<int>(Qt::KeyboardModifierMask));
    chord.ctrl = (modifiers & Qt::ControlModifier) != 0;
    chord.shift = (modifiers & Qt::ShiftModifier) != 0;
    chord.alt = (modifiers & Qt::AltModifier) != 0;
    if (chord.virtualKey == 0)
        return {};
    return chord;
}
}

void setCurrentShortcuts(const ShortcutSettings &settings)
{
    mutableShortcuts() = settings;

    // The low-level keyboard hook decides what to capture against the shared
    // chord store (ui::isReservedMainWindowShortcut). Without this push it
    // would still hold the defaults, and a configured Alt/Win chord would be
    // captured into the session before the main window's QShortcut could run.
    ui::MainWindowShortcutSettings shared;
    shared.newConnection = chordFromSequence(settings.newConnection);
    shared.openConnections = chordFromSequence(settings.openConnections);
    shared.toggleFullScreen = chordFromSequence(settings.toggleFullScreen);
    shared.exitFullScreen = chordFromSequence(settings.exitFullScreen);
    ui::setCurrentMainWindowShortcuts(shared);
}

namespace
{
QKeySequence sequenceFromStore(QSettings &store, const char *key, const QKeySequence &fallback)
{
    // An explicitly empty stored value means "shortcut disabled"; a missing
    // key means "never configured" and keeps the default.
    const QString stored = store.value(QLatin1String(key)).toString();
    if (store.contains(QLatin1String(key)) && stored.isEmpty())
        return QKeySequence();
    const QKeySequence sequence(stored);
    // Single-key shortcuts only; unparseable text parses to an unknown-key
    // sequence whose toString() is empty.
    return sequence.count() == 1 && !sequence.toString().isEmpty() ? sequence : fallback;
}
}

void saveShortcutSettings(const ShortcutSettings &settings, QSettings &store)
{
    store.beginGroup(QStringLiteral("shortcuts"));
    store.setValue(QStringLiteral("newConnection"), settings.newConnection.toString());
    store.setValue(QStringLiteral("openConnections"), settings.openConnections.toString());
    store.setValue(QStringLiteral("toggleFullScreen"), settings.toggleFullScreen.toString());
    store.endGroup();
}

ShortcutSettings loadShortcutSettings(QSettings &store)
{
    ShortcutSettings settings;
    store.beginGroup(QStringLiteral("shortcuts"));
    settings.newConnection = sequenceFromStore(store, "newConnection", settings.newConnection);
    settings.openConnections = sequenceFromStore(store, "openConnections", settings.openConnections);
    settings.toggleFullScreen = sequenceFromStore(store, "toggleFullScreen", settings.toggleFullScreen);
    store.endGroup();
    return settings;
}


}
