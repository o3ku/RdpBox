#include "qtui/QtShortcutsDialog.h"

#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>

QtShortcutsDialog::QtShortcutsDialog(const rdpbox::ShortcutSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Shortcuts"));

    auto *form = new QFormLayout;
    m_newConnectionEdit = new QKeySequenceEdit(m_settings.newConnection, this);
    m_openConnectionsEdit = new QKeySequenceEdit(m_settings.openConnections, this);
    m_toggleFullScreenEdit = new QKeySequenceEdit(m_settings.toggleFullScreen, this);
    form->addRow(tr("New connection"), m_newConnectionEdit);
    form->addRow(tr("Open connections"), m_openConnectionsEdit);
    form->addRow(tr("Toggle full screen"), m_toggleFullScreenEdit);

    auto *exitFullScreenLabel = new QLabel(m_settings.exitFullScreen.toString(QKeySequence::NativeText), this);
    exitFullScreenLabel->setToolTip(tr("Fixed shortcut"));
    form->addRow(tr("Exit full screen"), exitFullScreenLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (hasDuplicateShortcuts()) {
            QMessageBox::warning(this, tr("Shortcuts"), tr("Two actions use the same shortcut."));
            return;
        }
        m_settings.newConnection = m_newConnectionEdit->keySequence();
        m_settings.openConnections = m_openConnectionsEdit->keySequence();
        m_settings.toggleFullScreen = m_toggleFullScreenEdit->keySequence();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

rdpbox::ShortcutSettings QtShortcutsDialog::shortcutSettings() const
{
    return m_settings;
}

bool QtShortcutsDialog::hasDuplicateShortcuts() const
{
    const QKeySequence sequences[] = {
        m_newConnectionEdit->keySequence(),
        m_openConnectionsEdit->keySequence(),
        m_toggleFullScreenEdit->keySequence(),
        m_settings.exitFullScreen
    };
    for (int i = 0; i < 4; ++i) {
        if (sequences[i].isEmpty())
            continue;
        for (int j = i + 1; j < 4; ++j) {
            if (!sequences[j].isEmpty() && sequences[i] == sequences[j])
                return true;
        }
    }
    return false;
}
