#include "qt/QtMainWindow.h"

#include "common/AppPaths.h"
#include "qt/QtProfileDialog.h"

#include <QApplication>
#include <QBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>

namespace
{
QString profileTitle(const Profile &profile)
{
    return QString::fromStdWString(profile.name);
}

QString profileSubtitle(const Profile &profile)
{
    QString subtitle = QString::fromStdWString(profile.host);
    if (profile.port != 3389)
        subtitle += QStringLiteral(":%1").arg(profile.port);
    if (!profile.username.empty())
        subtitle += QStringLiteral("  %1").arg(QString::fromStdWString(profile.username));
    return subtitle;
}
}

QtMainWindow::QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                           QWidget *parent)
    : QMainWindow(parent),
      m_repository(AppPaths::profilesFilePath()),
      m_startupConnectionNames(std::move(startupConnectionNames))
{
    setWindowTitle(QStringLiteral("RdpBox"));
    resize(1180, 760);
    buildUi();
    refreshProfileList();

    if (!m_startupConnectionNames.empty()) {
        QTimer::singleShot(0, this, [this]() {
            openConnectionsByName(m_startupConnectionNames);
        });
    }
}

void QtMainWindow::buildUi()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto *sidebar = new QWidget(splitter);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(14, 14, 14, 14);
    sidebarLayout->setSpacing(10);

    auto *title = new QLabel(tr("Connections"), sidebar);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);

    m_searchEdit = new QLineEdit(sidebar);
    m_searchEdit->setPlaceholderText(tr("Search"));

    m_profileList = new QListWidget(sidebar);
    m_profileList->setUniformItemSizes(true);
    m_profileList->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *primaryButtons = new QHBoxLayout;
    auto *newButton = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), tr("New"), sidebar);
    m_connectButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), tr("Connect"), sidebar);
    primaryButtons->addWidget(newButton);
    primaryButtons->addWidget(m_connectButton);

    auto *secondaryButtons = new QHBoxLayout;
    m_editButton = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Edit"), sidebar);
    m_deleteButton = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), tr("Delete"), sidebar);
    secondaryButtons->addWidget(m_editButton);
    secondaryButtons->addWidget(m_deleteButton);

    sidebarLayout->addWidget(title);
    sidebarLayout->addWidget(m_searchEdit);
    sidebarLayout->addWidget(m_profileList, 1);
    sidebarLayout->addLayout(primaryButtons);
    sidebarLayout->addLayout(secondaryButtons);

    auto *workspace = new QWidget(splitter);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    m_tabs = new QTabWidget(workspace);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->addTab(createHomePage(), tr("Home"));
    workspaceLayout->addWidget(m_tabs);

    splitter->addWidget(sidebar);
    splitter->addWidget(workspace);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 880});

    setCentralWidget(splitter);
    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel, 1);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        refreshProfileList();
    });
    connect(m_profileList, &QListWidget::itemSelectionChanged, this, [this]() {
        refreshActions();
    });
    connect(m_profileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        connectSelectedProfile();
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        addProfile();
    });
    connect(m_editButton, &QPushButton::clicked, this, [this]() {
        editSelectedProfile();
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        deleteSelectedProfile();
    });
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
        connectSelectedProfile();
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index > 0)
            m_tabs->removeTab(index);
    });

    refreshActions();
}

void QtMainWindow::refreshProfileList()
{
    const std::wstring query = m_searchEdit ? m_searchEdit->text().toStdWString() : std::wstring();
    const std::vector<Profile> profiles = query.empty()
        ? m_repository.profiles()
        : m_repository.search(query);

    m_profileList->clear();
    for (const Profile &profile : profiles) {
        auto *item = new QListWidgetItem(m_profileList);
        item->setText(profileTitle(profile) + QStringLiteral("\n") + profileSubtitle(profile));
        item->setData(Qt::UserRole, QString::fromStdWString(profile.name));
        item->setSizeHint(QSize(0, 54));
    }

    m_statusLabel->setText(tr("%n connection(s)", nullptr, static_cast<int>(m_repository.profiles().size())));
    refreshActions();
}

void QtMainWindow::refreshActions()
{
    const bool hasSelection = m_profileList && m_profileList->currentItem();
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_connectButton->setEnabled(hasSelection);
}

void QtMainWindow::addProfile()
{
    QtProfileDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!m_repository.addProfile(dialog.profile())) {
        QMessageBox::warning(this, tr("Connection"), tr("A connection with this name already exists."));
        return;
    }

    refreshProfileList();
}

void QtMainWindow::editSelectedProfile()
{
    const std::wstring currentName = selectedProfileName();
    if (currentName.empty())
        return;

    QtProfileDialog dialog(this);
    dialog.setProfile(m_repository.profileByName(currentName));
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!m_repository.updateProfile(currentName, dialog.profile())) {
        QMessageBox::warning(this, tr("Connection"), tr("A connection with this name already exists."));
        return;
    }

    refreshProfileList();
}

void QtMainWindow::deleteSelectedProfile()
{
    const std::wstring currentName = selectedProfileName();
    if (currentName.empty())
        return;

    const QString name = QString::fromStdWString(currentName);
    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Delete Connection"),
        tr("Delete \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
        return;

    m_repository.removeProfile(currentName);
    refreshProfileList();
}

void QtMainWindow::connectSelectedProfile()
{
    const Profile profile = selectedProfile();
    if (!profile.isValid())
        return;

    addSessionTab(profile);
}

void QtMainWindow::openConnectionsByName(const std::vector<std::wstring> &connectionNames)
{
    for (const std::wstring &name : connectionNames) {
        const Profile profile = m_repository.profileByName(name);
        if (profile.isValid())
            addSessionTab(profile);
    }
}

Profile QtMainWindow::selectedProfile() const
{
    return m_repository.profileByName(selectedProfileName());
}

std::wstring QtMainWindow::selectedProfileName() const
{
    if (!m_profileList || !m_profileList->currentItem())
        return {};

    return m_profileList->currentItem()->data(Qt::UserRole).toString().toStdWString();
}

void QtMainWindow::addSessionTab(const Profile &profile)
{
    QWidget *page = createSessionPage(profile);
    const int index = m_tabs->addTab(page, profileTitle(profile));
    m_tabs->setCurrentIndex(index);
}

QWidget *QtMainWindow::createHomePage() const
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(14);

    auto *title = new QLabel(tr("RdpBox"), page);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *status = new QLabel(tr("No active sessions"), page);
    status->setObjectName(QStringLiteral("mutedLabel"));

    layout->addWidget(title);
    layout->addWidget(status);
    layout->addStretch(1);
    return page;
}

QWidget *QtMainWindow::createSessionPage(const Profile &profile) const
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(22, 22, 22, 22);
    layout->setSpacing(12);

    auto *summary = new QGroupBox(profileTitle(profile), page);
    auto *summaryLayout = new QVBoxLayout(summary);
    summaryLayout->addWidget(new QLabel(profileSubtitle(profile), summary));
    summaryLayout->addWidget(new QLabel(tr("Disconnected"), summary));

    auto *surface = new QFrame(page);
    surface->setObjectName(QStringLiteral("sessionSurface"));
    surface->setFrameShape(QFrame::StyledPanel);
    auto *surfaceLayout = new QVBoxLayout(surface);
    auto *surfaceLabel = new QLabel(tr("No signal"), surface);
    surfaceLabel->setAlignment(Qt::AlignCenter);
    surfaceLayout->addWidget(surfaceLabel, 1);

    layout->addWidget(summary);
    layout->addWidget(surface, 1);
    return page;
}
