#include "qt/QtMainWindow.h"

#include "common/AppPaths.h"
#include "common/Win32String.h"
#include "qt/QtProfileDialog.h"
#include "qt/QtRdpSessionWidget.h"
#include "qt/QtWindowChromeBehavior.h"
#include "ui/WindowStateScaling.h"

#include <QApplication>
#include <QBoxLayout>
#include <QByteArray>
#include <QCloseEvent>
#include <QEvent>
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
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <windows.h>

#include <array>
#include <cwchar>

namespace
{
constexpr int kTitleBarHeight = 42;
constexpr int kResizeBorderWidth = 6;

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

bool monitorInfoForRect(const RECT &rect, RECT &monitorRect, RECT &workArea, std::wstring &deviceName)
{
    const HMONITOR monitor = ::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return false;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return false;

    monitorRect = info.rcMonitor;
    workArea = info.rcWork;
    deviceName = info.szDevice;
    return true;
}

struct MonitorLookupContext
{
    const wchar_t *deviceName = nullptr;
    RECT monitorRect = {};
    RECT workArea = {};
    bool found = false;
};

BOOL CALLBACK findMonitorByDeviceName(HMONITOR monitor, HDC, LPRECT, LPARAM userData)
{
    auto *context = reinterpret_cast<MonitorLookupContext *>(userData);
    if (!context || !context->deviceName)
        return TRUE;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return TRUE;

    if (::wcscmp(info.szDevice, context->deviceName) != 0)
        return TRUE;

    context->monitorRect = info.rcMonitor;
    context->workArea = info.rcWork;
    context->found = true;
    return FALSE;
}

bool monitorInfoForDeviceName(const std::wstring &deviceName, RECT &monitorRect, RECT &workArea)
{
    if (deviceName.empty())
        return false;

    MonitorLookupContext context;
    context.deviceName = deviceName.c_str();
    ::EnumDisplayMonitors(nullptr, nullptr, &findMonitorByDeviceName, reinterpret_cast<LPARAM>(&context));
    if (!context.found)
        return false;

    monitorRect = context.monitorRect;
    workArea = context.workArea;
    return true;
}

bool activeMonitorInfo(RECT &monitorRect, RECT &workArea)
{
    POINT point = {};
    if (!::GetCursorPos(&point))
        point = POINT{0, 0};

    const HMONITOR monitor = ::MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
    if (!monitor)
        return false;

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, &info))
        return false;

    monitorRect = info.rcMonitor;
    workArea = info.rcWork;
    return true;
}
}

QtMainWindow::QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                           QWidget *parent)
    : QMainWindow(parent),
      m_repository(AppPaths::profilesFilePath()),
      m_startupConnectionNames(std::move(startupConnectionNames))
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NativeWindow);
    setWindowTitle(QStringLiteral("RdpBox"));
    resize(1180, 760);
    buildUi();
    restoreWindowState();
    refreshProfileList();

    if (!m_startupConnectionNames.empty()) {
        QTimer::singleShot(0, this, [this]() {
            openConnectionsByName(m_startupConnectionNames);
        });
    }
}

bool QtMainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return QMainWindow::nativeEvent(eventType, message, result);

    MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_NCHITTEST)
        return QMainWindow::nativeEvent(eventType, message, result);

    const POINTS screenPoint = MAKEPOINTS(msg->lParam);
    const QPoint windowPoint = mapFromGlobal(QPoint(screenPoint.x, screenPoint.y));
    *result = nativeHitTestForPoint(windowPoint);
    return true;
}

void QtMainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        refreshWindowControls();
        if (!m_restoringWindowState)
            saveWindowState();
    }
}

void QtMainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void QtMainWindow::buildUi()
{
    auto *shell = new QWidget(this);
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    buildTitleBar(shellLayout);

    auto *splitter = new QSplitter(Qt::Horizontal, shell);
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
    configureHomeTab();
    workspaceLayout->addWidget(m_tabs);

    splitter->addWidget(sidebar);
    splitter->addWidget(workspace);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 880});

    shellLayout->addWidget(splitter, 1);
    setCentralWidget(shell);
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
        closeSessionTab(index);
    });

    refreshActions();
}

void QtMainWindow::buildTitleBar(QVBoxLayout *rootLayout)
{
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(kTitleBarHeight);

    auto *layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(12, 0, 0, 0);
    layout->setSpacing(6);

    auto *iconLabel = new QLabel(m_titleBar);
    iconLabel->setPixmap(windowIcon().pixmap(20, 20));
    iconLabel->setFixedSize(24, 24);

    m_titleLabel = new QLabel(QStringLiteral("RdpBox"), m_titleBar);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_minimizeButton = new QToolButton(m_titleBar);
    m_maximizeButton = new QToolButton(m_titleBar);
    m_closeButton = new QToolButton(m_titleBar);

    m_minimizeButton->setObjectName(QStringLiteral("captionButton"));
    m_maximizeButton->setObjectName(QStringLiteral("captionButton"));
    m_closeButton->setObjectName(QStringLiteral("closeCaptionButton"));
    m_minimizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    m_maximizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_maximizeButton->setToolTip(tr("Maximize"));
    m_closeButton->setToolTip(tr("Close"));

    const std::array<QToolButton *, 3> captionButtons = {
        m_minimizeButton,
        m_maximizeButton,
        m_closeButton,
    };
    for (QToolButton *button : captionButtons) {
        button->setAutoRaise(true);
        button->setFixedSize(46, kTitleBarHeight);
        button->setFocusPolicy(Qt::NoFocus);
    }

    layout->addWidget(iconLabel);
    layout->addWidget(m_titleLabel);
    layout->addStretch(1);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);
    rootLayout->addWidget(m_titleBar);

    connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
        showMinimized();
    });
    connect(m_maximizeButton, &QToolButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_closeButton, &QToolButton::clicked, this, [this]() {
        close();
    });

    refreshWindowControls();
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

void QtMainWindow::refreshWindowControls()
{
    if (!m_maximizeButton)
        return;

    m_maximizeButton->setIcon(style()->standardIcon(
        isMaximized() ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
    m_maximizeButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
}

void QtMainWindow::configureHomeTab()
{
    if (!m_tabs || m_tabs->count() == 0)
        return;

    QTabBar *bar = m_tabs->tabBar();
    if (!bar)
        return;

    bar->setTabButton(0, QTabBar::LeftSide, nullptr);
    bar->setTabButton(0, QTabBar::RightSide, nullptr);
}

void QtMainWindow::saveWindowState() const
{
    HWND hwnd = reinterpret_cast<HWND>(const_cast<QtMainWindow *>(this)->winId());
    if (!hwnd || ::IsIconic(hwnd))
        return;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!::GetWindowPlacement(hwnd, &placement))
        return;

    RECT monitorRect = {};
    RECT workArea = {};
    std::wstring deviceName;
    if (!monitorInfoForRect(placement.rcNormalPosition, monitorRect, workArea, deviceName))
        return;

    const RECT workspaceRect = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, workArea);

    WindowState state;
    if (!WindowStateScaling::saveToMonitorWorkArea(placement.rcNormalPosition,
                                                   workspaceRect,
                                                   static_cast<int>(placement.showCmd),
                                                   state)) {
        return;
    }

    state.monitorDeviceName = deviceName;
    m_repository.saveWindowState(state);
}

bool QtMainWindow::restoreWindowState()
{
    const WindowState state = m_repository.loadWindowState();
    if (!state.valid)
        return false;

    RECT monitorRect = {};
    RECT workArea = {};
    if (!monitorInfoForDeviceName(state.monitorDeviceName, monitorRect, workArea)
        && !activeMonitorInfo(monitorRect, workArea)) {
        return false;
    }

    const RECT workspaceRect = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, workArea);
    RECT restoredRect = {};
    if (!WindowStateScaling::restoreFromMonitorWorkArea(state, workspaceRect, restoredRect))
        return false;

    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return false;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!::GetWindowPlacement(hwnd, &placement))
        return false;

    placement.rcNormalPosition = restoredRect;
    placement.showCmd = state.showCmd;

    m_restoringWindowState = true;
    const bool restored = ::SetWindowPlacement(hwnd, &placement) != FALSE;
    m_restoringWindowState = false;
    refreshWindowControls();
    return restored;
}

int QtMainWindow::nativeHitTestForPoint(const QPoint &windowPoint) const
{
    const QRect captionRect = m_titleBar ? QRect(m_titleBar->pos(), m_titleBar->size()) : QRect();
    const qt::chrome::HitArea area = qt::chrome::hitAreaForPoint(
        windowPoint,
        size(),
        captionRect,
        captionExclusionRects(),
        kResizeBorderWidth,
        isMaximized());

    switch (area) {
    case qt::chrome::HitArea::Caption:
        return HTCAPTION;
    case qt::chrome::HitArea::Left:
        return HTLEFT;
    case qt::chrome::HitArea::Right:
        return HTRIGHT;
    case qt::chrome::HitArea::Top:
        return HTTOP;
    case qt::chrome::HitArea::Bottom:
        return HTBOTTOM;
    case qt::chrome::HitArea::TopLeft:
        return HTTOPLEFT;
    case qt::chrome::HitArea::TopRight:
        return HTTOPRIGHT;
    case qt::chrome::HitArea::BottomLeft:
        return HTBOTTOMLEFT;
    case qt::chrome::HitArea::BottomRight:
        return HTBOTTOMRIGHT;
    case qt::chrome::HitArea::Client:
    default:
        return HTCLIENT;
    }
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

void QtMainWindow::closeSessionTab(int index)
{
    if (!m_tabs || index <= 0 || index >= m_tabs->count())
        return;

    QWidget *page = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (page)
        page->deleteLater();
    configureHomeTab();
}

void QtMainWindow::touchLastConnectedAt(const Profile &profile)
{
    Profile stored = m_repository.profileByName(profile.name);
    if (!stored.isValid())
        return;

    stored.lastConnectedAt = currentUtcIso8601();
    if (m_repository.updateProfile(stored.name, stored))
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

QWidget *QtMainWindow::createSessionPage(const Profile &profile)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(22, 22, 22, 22);
    layout->setSpacing(12);

    auto *summary = new QGroupBox(profileTitle(profile), page);
    auto *summaryLayout = new QVBoxLayout(summary);
    summaryLayout->addWidget(new QLabel(profileSubtitle(profile), summary));
    auto *statusLabel = new QLabel(tr("Disconnected"), summary);
    auto *reconnectButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), tr("Reconnect"), summary);
    summaryLayout->addWidget(statusLabel);
    summaryLayout->addWidget(reconnectButton, 0, Qt::AlignLeft);

    auto *surface = new QtRdpSessionWidget(profile, page);
    surface->setObjectName(QStringLiteral("sessionSurface"));
    surface->setStateChangedCallback([this, profile, statusLabel](FreeRdpProcess::State state) {
        switch (state) {
        case FreeRdpProcess::State::Idle:
            statusLabel->setText(QObject::tr("Disconnected"));
            break;
        case FreeRdpProcess::State::Starting:
            statusLabel->setText(QObject::tr("Connecting"));
            break;
        case FreeRdpProcess::State::Running:
            statusLabel->setText(QObject::tr("Connected"));
            touchLastConnectedAt(profile);
            break;
        case FreeRdpProcess::State::Finished:
            statusLabel->setText(QObject::tr("Disconnected"));
            break;
        }
    });
    connect(reconnectButton, &QPushButton::clicked, surface, [surface]() {
        surface->reconnect();
    });

    layout->addWidget(summary);
    layout->addWidget(surface, 1);
    QTimer::singleShot(0, surface, [surface]() {
        surface->connectToHost();
    });
    return page;
}

std::vector<QRect> QtMainWindow::captionExclusionRects() const
{
    std::vector<QRect> rects;
    for (QWidget *widget : {static_cast<QWidget *>(m_minimizeButton),
                            static_cast<QWidget *>(m_maximizeButton),
                            static_cast<QWidget *>(m_closeButton)}) {
        if (!widget || !m_titleBar)
            continue;

        const QPoint topLeft = widget->mapTo(this, QPoint(0, 0));
        rects.push_back(QRect(topLeft, widget->size()));
    }
    return rects;
}
