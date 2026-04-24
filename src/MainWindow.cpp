#include "MainWindow.h"
#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionManager.h"
#include "ui/ProfileEditDialog.h"
#include "ui/ConnectionListDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QSvgRenderer>
#include <QTabBar>
#include <QDir>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QProxyStyle>

namespace
{
class NoTabFrameStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override
    {
        if (element == QStyle::PE_FrameTabWidget || element == QStyle::PE_FrameTabBarBase)
            return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override
    {
        if (metric == QStyle::PM_TabCloseIndicatorWidth
            || metric == QStyle::PM_TabCloseIndicatorHeight)
            return 24;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};
constexpr auto kMainWindowGroup = "MainWindow";
constexpr auto kGeometryKey = "Geometry";
constexpr auto kStateKey = "WindowState";

QIcon loadTintedSvgIcon(const QString &resourcePath, const QColor &color, const QSize &size)
{
    if (!QFile::exists(resourcePath))
        return {};

    QSvgRenderer renderer(resourcePath);
    if (!renderer.isValid())
        return {};

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    {
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(0, 0, size.width(), size.height()));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
    }

    return QIcon(pixmap);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tabWidget(new QTabWidget(this))
    , m_sessionManager(nullptr)
    , m_profileRepo(nullptr)
    , m_newAction(nullptr)
    , m_connectionsAction(nullptr)
    , m_reconnectAction(nullptr)
    , m_newButton(nullptr)
    , m_connectionsButton(nullptr)
{
    setWindowTitle("RdpBox");
    setCentralWidget(m_tabWidget);
    resize(1280, 800);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_profileRepo = new ProfileRepository(dataDir + "/profiles.json");
    m_sessionManager = new SessionManager(m_tabWidget, this);

    setupActions();
    setupTabWidget();
    setupTabActions();
    restoreWindowState();

    QTimer::singleShot(0, this, &MainWindow::onOpenConnection);
}

MainWindow::~MainWindow()
{
    if (m_sessionManager)
        m_sessionManager->closeAllSessions();
    delete m_profileRepo;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_isClosing = true;
    if (m_connectionDialog)
        m_connectionDialog->setSelectionRequired(false);
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupActions()
{
    const QColor actionIconColor = palette().color(QPalette::ButtonText);
    const QIcon newIcon = loadTintedSvgIcon(":/add.svg", actionIconColor, QSize(16, 16));
    const QIcon connectionsIcon = loadTintedSvgIcon(":/connections.svg", actionIconColor, QSize(16, 16));

    m_newAction = new QAction(newIcon, "New", this);
    m_connectionsAction = new QAction(connectionsIcon, "Connections", this);
    m_reconnectAction = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), "Reconnect", this);

    m_newAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    m_connectionsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));

    addAction(m_newAction);
    addAction(m_connectionsAction);

    connect(m_newAction, &QAction::triggered, this, &MainWindow::onNewConnection);
    connect(m_connectionsAction, &QAction::triggered, this, &MainWindow::onOpenConnection);
}

void MainWindow::setupTabWidget()
{
    m_tabWidget->setStyle(new NoTabFrameStyle);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setElideMode(Qt::ElideRight);
    m_tabWidget->setIconSize(QSize(16, 16));
    m_tabWidget->setStyleSheet(
        "QTabWidget { border: 0; margin: 0; padding: 0; }"
        "QTabWidget::pane { border: 0; margin: 0; padding: 0; }"
        "QTabWidget::tab-bar { border: 0; }"
        "QTabBar { border: 0; qproperty-drawBase: 0; }"
        "QTabBar::tab { min-height: 30px; font-size: 14px; }"
        "QTabBar::close-button { image: url(:/close.svg); subcontrol-position: right; margin-left: 10px; }"
        "QTabBar::close-button:hover { image: url(:/close.svg); }"
        "#tabBarActions { background: transparent; }"
        "#tabBarActionButton {"
        "  border: 0;"
        "  background: transparent;"
        "  padding: 0 3px;"
        "  margin: 0;"
        "  min-width: 20px;"
        "  min-height: 30px;"
        "}"
        "#tabBarActionButton:hover { background: rgba(0, 0, 0, 18); }"
        "#tabBarActionButton:pressed { background: rgba(0, 0, 0, 32); }");

    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
}

void MainWindow::setupTabActions()
{
    auto *tabBar = m_tabWidget->tabBar();
    tabBar->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(tabBar, &QTabBar::customContextMenuRequested,
            this, &MainWindow::onTabContextMenuRequested);

    auto *cornerWidget = new QWidget(this);
    cornerWidget->setObjectName("tabBarActions");
    cornerWidget->setMinimumHeight(30);
    auto *layout = new QHBoxLayout(cornerWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_newButton = new QToolButton(cornerWidget);
    m_newButton->setObjectName("tabBarActionButton");
    m_newButton->setAutoRaise(true);
    m_newButton->setDefaultAction(m_newAction);
    m_newButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_newButton->setIconSize(QSize(14, 14));
    m_newButton->setToolTip("New");

    m_connectionsButton = new QToolButton(cornerWidget);
    m_connectionsButton->setObjectName("tabBarActionButton");
    m_connectionsButton->setAutoRaise(true);
    m_connectionsButton->setDefaultAction(m_connectionsAction);
    m_connectionsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_connectionsButton->setIconSize(QSize(14, 14));
    m_connectionsButton->setToolTip("Connections");

    layout->addWidget(m_newButton);
    layout->addWidget(m_connectionsButton);
    m_tabWidget->setCornerWidget(cornerWidget, Qt::TopRightCorner);
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    settings.beginGroup(kMainWindowGroup);

    const QByteArray geometry = settings.value(kGeometryKey).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray state = settings.value(kStateKey).toByteArray();
    if (!state.isEmpty())
        restoreState(state);

    settings.endGroup();
}

void MainWindow::openConnectionDialog(bool selectionRequired)
{
    if (m_connectionDialog) {
        m_connectionDialog->setSelectionRequired(selectionRequired || !hasOpenTabs());
        m_connectionDialog->raise();
        m_connectionDialog->activateWindow();
        return;
    }

    ConnectionListDialog dlg(m_profileRepo, this);
    m_connectionDialog = &dlg;

    const bool requireSelection = selectionRequired || !hasOpenTabs();
    dlg.setSelectionRequired(requireSelection);

    if (dlg.exec() == QDialog::Accepted) {
        for (const QString &profileId : dlg.selectedProfileIds()) {
            const Profile p = m_profileRepo->profile(profileId);
            if (p.isValid())
                m_sessionManager->openSession(p);
        }
    } else if (!m_isClosing && !hasOpenTabs()) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_isClosing && !hasOpenTabs())
                openConnectionDialog(true);
        });
    }

    if (m_connectionDialog == &dlg)
        m_connectionDialog = nullptr;
}

void MainWindow::saveWindowState() const
{
    QSettings settings;
    settings.beginGroup(kMainWindowGroup);
    settings.setValue(kGeometryKey, saveGeometry());
    settings.setValue(kStateKey, saveState());
    settings.endGroup();
}

QString MainWindow::sessionIdByTabIndex(int index) const
{
    if (index < 0)
        return {};

    return m_sessionManager->sessionIdByWidget(m_tabWidget->widget(index));
}

bool MainWindow::hasOpenTabs() const
{
    return m_tabWidget->count() > 0;
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
    openConnectionDialog(false);
}

void MainWindow::onTabCloseRequested(int index)
{
    const QString sessionId = sessionIdByTabIndex(index);
    if (!sessionId.isEmpty())
        m_sessionManager->closeSession(sessionId);

    if (!m_isClosing && !hasOpenTabs()) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_isClosing && !hasOpenTabs())
                openConnectionDialog(true);
        });
    }
}

void MainWindow::onTabContextMenuRequested(const QPoint &pos)
{
    auto *tabBar = m_tabWidget->tabBar();
    const int index = tabBar->tabAt(pos);
    const QString sessionId = sessionIdByTabIndex(index);
    if (sessionId.isEmpty())
        return;

    QMenu menu(this);
    QAction *reconnectAction = menu.addAction(m_reconnectAction->icon(), m_reconnectAction->text());
    QAction *closeAction = menu.addAction(QIcon(":/close.svg"), "Close");

    QAction *chosen = menu.exec(tabBar->mapToGlobal(pos));
    if (chosen == reconnectAction) {
        m_sessionManager->reconnectSession(sessionId);
    } else if (chosen == closeAction) {
        m_sessionManager->closeSession(sessionId);
    }
}
