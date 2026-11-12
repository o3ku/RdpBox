#include "qtui/QtShortcutSettings.h"

#include "common/ui/MainWindowShortcuts.h"

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

    // Configured chords reach the shared store the low-level keyboard hook
    // reads, or a custom Alt chord would be captured into the session.
    {
        ShortcutSettings custom;
        custom.newConnection = QKeySequence(Qt::ALT | Qt::Key_D);
        custom.openConnections = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F2);
        setCurrentShortcuts(custom);

        const ui::MainWindowShortcutSettings &shared = ui::currentMainWindowShortcuts();
        assert(shared.newConnection.virtualKey == 'D');
        assert(shared.newConnection.alt && !shared.newConnection.ctrl && !shared.newConnection.shift);
        assert(shared.openConnections.virtualKey == VK_F2);
        assert(shared.openConnections.ctrl && shared.openConnections.shift);
        assert(ui::isReservedMainWindowShortcut(false, false, true, 'D'));
        assert(!ui::isReservedMainWindowShortcut(true, false, false, 'N'));

        // An unrepresentable chord (Meta) must not match anything.
        ShortcutSettings meta;
        meta.newConnection = QKeySequence(Qt::META | Qt::Key_D);
        setCurrentShortcuts(meta);
        assert(ui::currentMainWindowShortcuts().newConnection.virtualKey == 0);
        assert(!ui::isReservedMainWindowShortcut(false, false, false, 'D'));

        setCurrentShortcuts(ShortcutSettings());
        assert(ui::isReservedMainWindowShortcut(true, false, false, 'N'));
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
