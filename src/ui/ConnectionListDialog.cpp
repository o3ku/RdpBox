#include "ConnectionListDialog.h"
#include "ProfileEditDialog.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

ConnectionListDialog::ConnectionListDialog(ProfileRepository *repo, QWidget *parent)
    : QDialog(parent)
    , m_repo(repo)
{
    setWindowTitle("Connections");
    setMinimumSize(400, 350);

    auto *layout = new QVBoxLayout(this);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search by name or host...");
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    layout->addWidget(m_listWidget);

    auto *btnLayout = new QHBoxLayout;
    auto *newBtn = new QPushButton("New", this);
    auto *connectBtn = new QPushButton("Connect", this);
    auto *editBtn = new QPushButton("Edit", this);
    auto *deleteBtn = new QPushButton("Delete", this);
    auto *closeBtn = new QPushButton("Close", this);
    btnLayout->addWidget(newBtn);
    btnLayout->addWidget(connectBtn);
    btnLayout->addWidget(editBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);

    refreshList(m_repo->profiles());

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ConnectionListDialog::onSearchChanged);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &ConnectionListDialog::onItemDoubleClicked);
    connect(newBtn, &QPushButton::clicked,
            this, &ConnectionListDialog::onNewClicked);
    connect(connectBtn, &QPushButton::clicked,
            this, &ConnectionListDialog::onConnectClicked);
    connect(editBtn, &QPushButton::clicked,
            this, &ConnectionListDialog::onEditClicked);
    connect(deleteBtn, &QPushButton::clicked,
            this, &ConnectionListDialog::onDeleteClicked);
    connect(closeBtn, &QPushButton::clicked,
            this, &QDialog::reject);
}

QString ConnectionListDialog::selectedProfileId() const
{
    return m_selectedId;
}

void ConnectionListDialog::onSearchChanged(const QString &text)
{
    refreshList(m_repo->search(text));
}

void ConnectionListDialog::onItemDoubleClicked()
{
    onConnectClicked();
}

void ConnectionListDialog::onNewClicked()
{
    ProfileEditDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Profile p = dlg.profile();
        if (p.id.isEmpty())
            p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_repo->addProfile(p);
        refreshList(m_repo->search(m_searchEdit->text()));
    }
}

void ConnectionListDialog::onConnectClicked()
{
    auto *item = m_listWidget->currentItem();
    if (!item)
        return;
    m_selectedId = item->data(Qt::UserRole).toString();
    accept();
}

void ConnectionListDialog::onEditClicked()
{
    auto *item = m_listWidget->currentItem();
    if (!item)
        return;
    QString id = item->data(Qt::UserRole).toString();
    Profile p = m_repo->profile(id);
    if (!p.isValid())
        return;

    ProfileEditDialog dlg(this);
    dlg.setProfile(p);
    if (dlg.exec() == QDialog::Accepted) {
        m_repo->updateProfile(dlg.profile());
        refreshList(m_repo->search(m_searchEdit->text()));
    }
}

void ConnectionListDialog::onDeleteClicked()
{
    auto *item = m_listWidget->currentItem();
    if (!item)
        return;
    QString id = item->data(Qt::UserRole).toString();
    Profile p = m_repo->profile(id);
    if (!p.isValid())
        return;

    auto result = QMessageBox::question(this, "Delete Connection",
        QString("Delete \"%1\"?").arg(p.name));
    if (result == QMessageBox::Yes) {
        m_repo->removeProfile(id);
        refreshList(m_repo->search(m_searchEdit->text()));
    }
}

void ConnectionListDialog::refreshList(const QList<Profile> &profiles)
{
    m_listWidget->clear();
    for (const auto &p : profiles) {
        auto *item = new QListWidgetItem(
            QString("%1 (%2:%3)").arg(p.name, p.host).arg(p.port), m_listWidget);
        item->setData(Qt::UserRole, p.id);
    }
}
