#pragma once

#include <QDialog>
#include "profiles/Profile.h"

class QLineEdit;
class QSpinBox;
class QCheckBox;

class ProfileEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfileEditDialog(QWidget *parent = nullptr);

    void setProfile(const Profile &profile);
    Profile profile() const;

private:
    QLineEdit *m_nameEdit;
    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpin;
    QLineEdit *m_userEdit;
    QLineEdit *m_passEdit;
    QCheckBox *m_clipboardCheck;
    QCheckBox *m_certCheck;
    Profile m_profile;
};
