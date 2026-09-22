#include "qtui/QtShortcutSettings.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>

namespace
{

int runTests()
{
    using namespace rdpbox;

    // Defaults.
    {
        const ShortcutSettings defaults;
        assert(defaults.newConnection == QKeySequence(Qt::CTRL | Qt::Key_N));
        assert(defaults.openConnections == QKeySequence(Qt::CTRL | Qt::Key_P));
        assert(defaults.toggleFullScreen == QKeySequence(Qt::Key_F11));
        assert(defaults.exitFullScreen == QKeySequence(Qt::Key_Escape));
    }

    // Save/load round trip through a QSettings store.
    {
        const QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shortcuts.ini"));

        ShortcutSettings custom;
        custom.newConnection = QKeySequence(Qt::CTRL | Qt::Key_T);
        custom.openConnections = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O);
        custom.toggleFullScreen = QKeySequence(Qt::Key_F4);

        {
            QSettings store(path, QSettings::IniFormat);
            saveShortcutSettings(custom, store);
        }
        {
            QSettings store(path, QSettings::IniFormat);
            const ShortcutSettings loaded = loadShortcutSettings(store);
            assert(loaded == custom);
        }
    }

    // Missing/invalid entries fall back to defaults.
    {
        const QTemporaryDir dir;
        QSettings store(dir.filePath(QStringLiteral("empty.ini")), QSettings::IniFormat);
        assert(loadShortcutSettings(store) == ShortcutSettings());
        store.setValue(QStringLiteral("shortcuts/newConnection"), QStringLiteral("not a key"));
        store.setValue(QStringLiteral("shortcuts/openConnections"), QStringLiteral("Ctrl+T, Ctrl+X"));
        const ShortcutSettings loaded = loadShortcutSettings(store);
        assert(loaded.newConnection == ShortcutSettings().newConnection);
        assert(loaded.openConnections == ShortcutSettings().openConnections);
    }

    // Reserved-shortcut predicate for the session ShortcutOverride path.
    {
        const ShortcutSettings settings;
        assert(isReservedSessionShortcut(settings, QKeySequence(Qt::CTRL | Qt::Key_P)));
        assert(isReservedSessionShortcut(settings, QKeySequence(Qt::CTRL | Qt::Key_N)));
        assert(!isReservedSessionShortcut(settings, QKeySequence(Qt::CTRL | Qt::Key_C)));
        assert(!isReservedSessionShortcut(settings, QKeySequence(Qt::Key_F11)));
        assert(!isReservedSessionShortcut(settings, QKeySequence()));

        ShortcutSettings custom;
        custom.openConnections = QKeySequence(Qt::ALT | Qt::Key_D);
        assert(isReservedSessionShortcut(custom, QKeySequence(Qt::ALT | Qt::Key_D)));
        assert(!isReservedSessionShortcut(custom, QKeySequence(Qt::CTRL | Qt::Key_P)));
    }

    std::printf("QtShortcutSettingsTests passed\n");
    return 0;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return runTests();
}
