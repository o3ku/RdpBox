#include "MainWindow.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "rdp/FreeRdpDownloader.h"
#include "session/SessionManager.h"
#include "ui/ProfileEditDialog.h"
#include "ui/ConnectionListDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabWidget(new QTabWidget(this))
    , m_sessionManager(nullptr)
    , m_profileRepo(nullptr)
{
    setWindowTitle("RdpBox");
    setCentralWidget(m_tabWidget);
    resize(1280, 800);

    m_tabWidget->setTabsClosable(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);

    m_sessionManager = new SessionManager(m_tabWidget, this);

    const QString appDir = QCoreApplication::applicationDirPath();
    FreeRdpDownloader downloader;
    QString exePath = downloader.ensureAvailable(appDir, this);

    if (exePath.isEmpty()) {
        QMessageBox::critical(this, "Error",
            "Failed to download wfreerdp.exe.\n"
            "Please manually place wfreerdp.exe in:\n" + appDir);
    } else {
        m_sessionManager->setExePath(exePath);
    }

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_profileRepo = new ProfileRepository(dataDir + "/profiles.json");

    setupToolbar();
}

MainWindow::~MainWindow()
{
    if (m_sessionManager)
        m_sessionManager->closeAllSessions();
    delete m_profileRepo;
}

void MainWindow::setupToolbar()
{
    auto *toolbar = addToolBar("Main");
    toolbar->setMovable(false);

    auto *newAction = toolbar->addAction("New");
    auto *connectAction = toolbar->addAction("Connect");

    connect(newAction, &QAction::triggered, this, &MainWindow::onNewConnection);
    connect(connectAction, &QAction::triggered, this, &MainWindow::onOpenConnection);
}

void MainWindow::onNewConnection()
{
    ProfileEditDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        Profile p = dlg.profile();
        if (p.id.isEmpty())
            p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_profileRepo->addProfile(p);
        m_sessionManager->openSession(p);
    }
}

void MainWindow::onOpenConnection()
{
    ConnectionListDialog dlg(m_profileRepo, this);
    if (dlg.exec() == QDialog::Accepted) {
        Profile p = m_profileRepo->profile(dlg.selectedProfileId());
        if (p.isValid())
            m_sessionManager->openSession(p);
    }
}

void MainWindow::onTabCloseRequested(int index)
{
    auto *widget = m_tabWidget->widget(index);
    QString sessionId = m_sessionManager->sessionIdByWidget(widget);
    if (!sessionId.isEmpty())
        m_sessionManager->closeSession(sessionId);
}
