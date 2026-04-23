#include "ProfileEditDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

ProfileEditDialog::ProfileEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Connection Properties");
    setMinimumWidth(350);

    auto *layout = new QFormLayout(this);

    m_nameEdit = new QLineEdit(this);
    m_hostEdit = new QLineEdit(this);
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(3389);
    m_userEdit = new QLineEdit(this);
    m_passEdit = new QLineEdit(this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_clipboardCheck = new QCheckBox("Enable clipboard", this);
    m_clipboardCheck->setChecked(true);
    m_certCheck = new QCheckBox("Ignore certificate errors", this);
    m_certCheck->setChecked(true);

    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Host:", m_hostEdit);
    layout->addRow("Port:", m_portSpin);
    layout->addRow("Username:", m_userEdit);
    layout->addRow("Password:", m_passEdit);
    layout->addRow(m_clipboardCheck);
    layout->addRow(m_certCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().isEmpty() || m_hostEdit->text().isEmpty())
            return;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ProfileEditDialog::setProfile(const Profile &profile)
{
    m_profile = profile;
    m_nameEdit->setText(profile.name);
    m_hostEdit->setText(profile.host);
    m_portSpin->setValue(profile.port);
    m_userEdit->setText(profile.username);
    m_passEdit->setText(profile.password);
    m_clipboardCheck->setChecked(profile.clipboardEnabled);
    m_certCheck->setChecked(profile.ignoreCertificate);
}

Profile ProfileEditDialog::profile() const
{
    Profile p = m_profile;
    p.name = m_nameEdit->text();
    p.host = m_hostEdit->text();
    p.port = m_portSpin->value();
    p.username = m_userEdit->text();
    p.password = m_passEdit->text();
    p.clipboardEnabled = m_clipboardCheck->isChecked();
    p.ignoreCertificate = m_certCheck->isChecked();
    return p;
}
