#pragma once

#include "qt/QtShortcutSettings.h"

#include <QDialog>

class QKeySequenceEdit;

class QtShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QtShortcutsDialog(const rdpbox::ShortcutSettings &settings, QWidget *parent = nullptr);

    rdpbox::ShortcutSettings shortcutSettings() const;

private:
    bool hasDuplicateShortcuts() const;

    rdpbox::ShortcutSettings m_settings;
    QKeySequenceEdit *m_newConnectionEdit = nullptr;
    QKeySequenceEdit *m_openConnectionsEdit = nullptr;
    QKeySequenceEdit *m_toggleFullScreenEdit = nullptr;
};
