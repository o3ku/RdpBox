#include "qtui/QtShortcutSettings.h"

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

void setCurrentShortcuts(const ShortcutSettings &settings)
{
    mutableShortcuts() = settings;
}

namespace
{
QKeySequence sequenceFromStore(QSettings &store, const char *key, const QKeySequence &fallback)
{
    const QKeySequence sequence(store.value(QLatin1String(key)).toString());
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

bool isReservedSessionShortcut(const ShortcutSettings &settings, const QKeySequence &pressed)
{
    return !pressed.isEmpty()
        && (pressed == settings.newConnection || pressed == settings.openConnections);
}

}
