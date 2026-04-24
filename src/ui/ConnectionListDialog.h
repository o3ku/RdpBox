#pragma once

#include <QDialog>
#include <QStringList>
#include "profiles/Profile.h"

class QListWidget;
class QLineEdit;
class QPushButton;
class QListWidgetItem;
class QCloseEvent;
class ProfileRepository;

class ConnectionListDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionListDialog(ProfileRepository *repo, QWidget *parent = nullptr);

    QString selectedProfileId() const;
    QStringList selectedProfileIds() const;
    void setSelectionRequired(bool required);

private slots:
    void onSearchChanged(const QString &text);
    void onItemActivated(QListWidgetItem *item);
    void onNewClicked();
    void onConnectClicked();
    void onEditClicked();
    void onDuplicateClicked();
    void onDeleteClicked();
    void onContextMenuRequested(const QPoint &pos);

private:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;
    void refreshList(const QList<Profile> &profiles);
    void updateCloseAvailability();
    QList<QListWidgetItem*> selectedItems() const;
    Profile currentProfile() const;
    void duplicateProfile(const Profile &profile);

    ProfileRepository *m_repo;
    QListWidget *m_listWidget;
    QLineEdit *m_searchEdit;
    QPushButton *m_connectButton;
    QPushButton *m_editButton;
    QPushButton *m_deleteButton;
    QStringList m_selectedIds;
    bool m_selectionRequired = false;
};
