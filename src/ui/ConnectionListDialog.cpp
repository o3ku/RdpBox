#include "ConnectionListDialog.h"
#include "ProfileEditDialog.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"

#include <QAction>
#include <QDebug>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
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
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    auto *layout = new QVBoxLayout(this);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search by name or host...");
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_listWidget);

    auto *btnLayout = new QHBoxLayout;
    auto *newBtn = new QPushButton("New", this);
    m_editButton = new QPushButton("Edit", this);
    m_deleteButton = new QPushButton("Delete", this);
    m_connectButton = new QPushButton("Connect", this);
    btnLayout->addStretch();
    btnLayout->addWidget(newBtn);
    btnLayout->addWidget(m_editButton);
    btnLayout->addWidget(m_deleteButton);
    btnLayout->addSpacing(20);
    btnLayout->addWidget(m_connectButton);
    layout->addLayout(btnLayout);

    refreshList(m_repo->profiles());

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ConnectionListDialog::onSearchChanged);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &ConnectionListDialog::onItemActivated);
    connect(m_listWidget, &QListWidget::itemActivated,
            this, &ConnectionListDialog::onItemActivated);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &ConnectionListDialog::onContextMenuRequested);
    connect(newBtn, &QPushButton::clicked,
            this, &ConnectionListDialog::onNewClicked);
    connect(m_connectButton, &QPushButton::clicked,
            this, &ConnectionListDialog::onConnectClicked);
    connect(m_editButton, &QPushButton::clicked,
            this, &ConnectionListDialog::onEditClicked);
    connect(m_deleteButton, &QPushButton::clicked,
            this, &ConnectionListDialog::onDeleteClicked);

    updateCloseAvailability();
}

QString ConnectionListDialog::selectedProfileId() const
{
    return m_selectedIds.isEmpty() ? QString() : m_selectedIds.constFirst();
}

QStringList ConnectionListDialog::selectedProfileIds() const
{
    return m_selectedIds;
}

void ConnectionListDialog::setSelectionRequired(bool required)
{
    m_selectionRequired = required;
    updateCloseAvailability();
}

void ConnectionListDialog::onSearchChanged(const QString &text)
{
    refreshList(m_repo->search(text));
}

void ConnectionListDialog::onItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;
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
    const auto items = selectedItems();
    if (items.isEmpty())
        return;

    m_selectedIds.clear();
    for (auto *item : items) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            m_selectedIds.append(id);
    }

    if (m_selectedIds.isEmpty())
        return;

    accept();
}

void ConnectionListDialog::onEditClicked()
{
    Profile p = currentProfile();
    if (p.id.isEmpty())
        return;

    ProfileEditDialog dlg(this);
    dlg.setProfile(p);
    if (dlg.exec() == QDialog::Accepted) {
        m_repo->updateProfile(dlg.profile());
        refreshList(m_repo->search(m_searchEdit->text()));
    }
}

void ConnectionListDialog::onDuplicateClicked()
{
    const auto items = selectedItems();
    if (items.isEmpty())
        return;

    for (auto *item : items) {
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty())
            continue;

        const Profile p = m_repo->profile(id);
        if (!p.id.isEmpty())
            duplicateProfile(p);
    }

    refreshList(m_repo->search(m_searchEdit->text()));
}

void ConnectionListDialog::onDeleteClicked()
{
    const auto items = selectedItems();
    if (items.isEmpty()) {
        qDebug("Delete: no item selected");
        return;
    }

    QStringList names;
    QStringList ids;
    for (auto *item : items) {
        const QString id = item->data(Qt::UserRole).toString();
        if (id.isEmpty())
            continue;

        const Profile p = m_repo->profile(id);
        names.append(p.name.isEmpty() ? QString("(unnamed - %1)").arg(id.left(8)) : p.name);
        ids.append(id);
    }

    if (ids.isEmpty())
        return;

    const QString message = ids.size() == 1
        ? QString("Delete \"%1\"?").arg(names.constFirst())
        : QString("Delete %1 connections?").arg(ids.size());

    auto result = QMessageBox::question(this, "Delete Connection", message);
    if (result == QMessageBox::Yes) {
        for (const QString &id : ids)
            m_repo->removeProfile(id);
        refreshList(m_repo->search(m_searchEdit->text()));
    }
}

void ConnectionListDialog::onContextMenuRequested(const QPoint &pos)
{
    const auto items = selectedItems();
    QListWidgetItem *item = m_listWidget->itemAt(pos);
    if (items.isEmpty() && !item)
        return;

    if (item && !item->isSelected()) {
        m_listWidget->clearSelection();
        item->setSelected(true);
        m_listWidget->setCurrentItem(item);
    }

    const auto selection = selectedItems();
    const bool hasSelection = !selection.isEmpty();
    const bool singleSelection = selection.size() == 1;

    QMenu menu(this);
    QAction *connectAction = menu.addAction("Connect");
    QAction *editAction = menu.addAction("Edit");
    QAction *duplicateAction = menu.addAction("duplicate");
    menu.addSeparator();
    QAction *deleteAction = menu.addAction("Delete");

    connectAction->setEnabled(hasSelection);
    editAction->setEnabled(singleSelection);
    duplicateAction->setEnabled(hasSelection);
    deleteAction->setEnabled(hasSelection);

    QAction *chosen = menu.exec(m_listWidget->viewport()->mapToGlobal(pos));
    if (chosen == connectAction)
        onConnectClicked();
    else if (chosen == editAction)
        onEditClicked();
    else if (chosen == duplicateAction)
        onDuplicateClicked();
    else if (chosen == deleteAction)
        onDeleteClicked();
}

void ConnectionListDialog::refreshList(const QList<Profile> &profiles)
{
    QStringList previouslySelected;
    for (auto *item : selectedItems()) {
        const QString id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            previouslySelected.append(id);
    }

    m_listWidget->clear();
    for (const auto &p : profiles) {
        QString label = QString("%1 (%2:%3)")
            .arg(p.name.isEmpty() ? QStringLiteral("(unnamed)") : p.name,
                 p.host.isEmpty() ? QStringLiteral("?") : p.host)
            .arg(p.port);
        auto *item = new QListWidgetItem(label, m_listWidget);
        item->setData(Qt::UserRole, p.id);
        if (previouslySelected.contains(p.id))
            item->setSelected(true);
    }

    if (!m_listWidget->currentItem() && m_listWidget->count() > 0)
        m_listWidget->setCurrentRow(0);
}

void ConnectionListDialog::closeEvent(QCloseEvent *event)
{
    if (m_selectionRequired) {
        event->ignore();
        return;
    }

    QDialog::closeEvent(event);
}

void ConnectionListDialog::reject()
{
    if (m_selectionRequired)
        return;

    QDialog::reject();
}

void ConnectionListDialog::updateCloseAvailability()
{
    setWindowFlag(Qt::WindowCloseButtonHint, !m_selectionRequired);
    if (m_connectButton)
        m_connectButton->setDefault(true);
}

QList<QListWidgetItem*> ConnectionListDialog::selectedItems() const
{
    return m_listWidget ? m_listWidget->selectedItems() : QList<QListWidgetItem*>{};
}

Profile ConnectionListDialog::currentProfile() const
{
    if (!m_listWidget || !m_listWidget->currentItem())
        return {};

    const QString id = m_listWidget->currentItem()->data(Qt::UserRole).toString();
    return id.isEmpty() ? Profile{} : m_repo->profile(id);
}

void ConnectionListDialog::duplicateProfile(const Profile &profile)
{
    Profile duplicate = profile;
    duplicate.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    duplicate.name = profile.name.isEmpty()
        ? QStringLiteral("(unnamed)")
        : QString("%1(n)").arg(profile.name);
    m_repo->addProfile(duplicate);
}
