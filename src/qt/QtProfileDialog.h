#pragma once

#include "profiles/Profile.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;

class QtProfileDialog : public QDialog
{
public:
    explicit QtProfileDialog(QWidget *parent = nullptr);

    void setProfile(const Profile &profile);
    Profile profile() const;

protected:
    void accept() override;

private:
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portEdit = nullptr;
    QLineEdit *m_domainEdit = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_clipboardCheck = nullptr;
    QCheckBox *m_ignoreCertificateCheck = nullptr;
    QCheckBox *m_fullScreenCheck = nullptr;
};
