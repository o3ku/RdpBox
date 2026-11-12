#pragma once

#include <QKeySequence>
#include <QSettings>

namespace rdpbox
{

struct ShortcutSettings
{
    QKeySequence newConnection = QKeySequence(Qt::CTRL | Qt::Key_N);
    QKeySequence openConnections = QKeySequence(Qt::CTRL | Qt::Key_P);
    QKeySequence toggleFullScreen = QKeySequence(Qt::Key_F11);
    QKeySequence exitFullScreen = QKeySequence(Qt::Key_Escape); // fixed, not editable

    bool operator==(const ShortcutSettings &other) const;
};

const ShortcutSettings &currentShortcuts();
void setCurrentShortcuts(const ShortcutSettings &settings);

void saveShortcutSettings(const ShortcutSettings &settings, QSettings &store);
ShortcutSettings loadShortcutSettings(QSettings &store);

}
