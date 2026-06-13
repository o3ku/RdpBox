#include "qt/QtProfileDialog.h"

#include "ui/ProfileEditBehavior.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

QtProfileDialog::QtProfileDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connection"));
    setModal(true);
    resize(420, 0);

    m_nameEdit = new QLineEdit(this);
    m_hostEdit = new QLineEdit(this);
    m_portEdit = new QSpinBox(this);
    m_domainEdit = new QLineEdit(this);
    m_usernameEdit = new QLineEdit(this);
    m_passwordEdit = new QLineEdit(this);
    m_clipboardCheck = new QCheckBox(tr("Enable clipboard"), this);
    m_ignoreCertificateCheck = new QCheckBox(tr("Ignore certificate errors"), this);
    m_fullScreenCheck = new QCheckBox(tr("Open full screen"), this);

    m_portEdit->setRange(1, 65535);
    m_portEdit->setValue(3389);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_clipboardCheck->setChecked(true);

    auto *formLayout = new QFormLayout;
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formLayout->addRow(tr("Name"), m_nameEdit);
    formLayout->addRow(tr("Host"), m_hostEdit);
    formLayout->addRow(tr("Port"), m_portEdit);
    formLayout->addRow(tr("Domain"), m_domainEdit);
    formLayout->addRow(tr("Username"), m_usernameEdit);
    formLayout->addRow(tr("Password"), m_passwordEdit);
    formLayout->addRow(QString(), m_clipboardCheck);
    formLayout->addRow(QString(), m_ignoreCertificateCheck);
    formLayout->addRow(QString(), m_fullScreenCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QtProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QtProfileDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(buttons);
}

void QtProfileDialog::setProfile(const Profile &profile)
{
    m_profile = profile;
    m_nameEdit->setText(QString::fromStdWString(profile.name));
    m_hostEdit->setText(QString::fromStdWString(profile.host));
    m_portEdit->setValue(normalizedProfilePort(profile.port));
    m_domainEdit->setText(QString::fromStdWString(profile.domain));
    m_usernameEdit->setText(QString::fromStdWString(profile.username));
    m_passwordEdit->setText(QString::fromStdWString(profile.password));
    m_clipboardCheck->setChecked(profile.clipboardEnabled);
    m_ignoreCertificateCheck->setChecked(profile.ignoreCertificate);
    m_fullScreenCheck->setChecked(profile.fullScreenOnConnect);
}

Profile QtProfileDialog::profile() const
{
    Profile profile = m_profile;
    profile.name = m_nameEdit->text().trimmed().toStdWString();
    profile.host = m_hostEdit->text().trimmed().toStdWString();
    profile.port = normalizedProfilePort(m_portEdit->value());
    profile.domain = m_domainEdit->text().trimmed().toStdWString();
    profile.username = m_usernameEdit->text().trimmed().toStdWString();
    profile.password = m_passwordEdit->text().toStdWString();
    profile.clipboardEnabled = m_clipboardCheck->isChecked();
    profile.ignoreCertificate = m_ignoreCertificateCheck->isChecked();
    profile.fullScreenOnConnect = m_fullScreenCheck->isChecked();
    return profile;
}

void QtProfileDialog::accept()
{
    const Profile candidate = profile();
    const ProfileEditValidationResult validation =
        validateProfileEditFields(candidate.name, candidate.host);

    if (validation == ProfileEditValidationResult::MissingRequiredField) {
        QMessageBox::warning(this, tr("Connection"), tr("Name and host are required."));
        return;
    }

    if (validation == ProfileEditValidationResult::InvalidNameCharacter) {
        QMessageBox::warning(
            this,
            tr("Invalid Connection Name"),
            tr("Connection name cannot contain ',' or '\"'."));
        return;
    }

    QDialog::accept();
}
