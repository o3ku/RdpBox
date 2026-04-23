#pragma once

#include <QDialog>
#include "profiles/Profile.h"

class QListWidget;
class QLineEdit;
class ProfileRepository;

class ConnectionListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionListDialog(ProfileRepository *repo, QWidget *parent = nullptr);

    QString selectedProfileId() const;

private slots:
    void onSearchChanged(const QString &text);
    void onItemDoubleClicked();
    void onConnectClicked();
    void onEditClicked();
    void onDeleteClicked();

private:
    void refreshList(const QList<Profile> &profiles);

    ProfileRepository *m_repo;
    QListWidget *m_listWidget;
    QLineEdit *m_searchEdit;
    QString m_selectedId;
};
