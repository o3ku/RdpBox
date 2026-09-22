#include "qtui/QtMainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/Win32String.h"
#include "qtui/QtProfileDialog.h"
#include "qtui/QtRdpSessionWidget.h"
#include "qtui/QtShortcutSettings.h"
#include "qtui/QtShortcutsDialog.h"
#include "qtui/QtWindowChromeBehavior.h"
#include "common/session/SessionResumePolicy.h"
#include "common/ui/ConnectionListBehavior.h"
#include "common/ui/MainWindowActivation.h"
#include "common/ui/MainWindowSessionBehavior.h"
#include "common/ui/MainWindowTabBehavior.h"
#include "common/ui/MainWindowUpdateBehavior.h"
#include "common/ui/WindowStateScaling.h"

#include <QApplication>
#include <QAction>
#include <QBoxLayout>
#include <QByteArray>
#include <QCloseEvent>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QPixmap>
#include <QShortcut>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <windows.h>

#include "qtui/FramelessWin.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace
{
constexpr int kTitleBarHeight = 42;
constexpr int kResizeBorderWidth = 6;
constexpr int kUpdateCheckIntervalMs = 24 * 60 * 60 * 1000;


// Caption glyphs on the Lucide 24-grid, drawn at 16 logical px / 2x dpr.
// QStyle::standardIcon title-bar icons mix unrelated visual styles; these
// match the AtomDataAssistant caption set (same stroke geometry).
enum class CaptionGlyph
{
    Minimize,
    Maximize,
    Restore,
    Close,
    Logo,
    Plus,
};

QIcon captionIcon(CaptionGlyph glyph, const QColor &stroke, int logical = 16)
{
    const qreal k = logical / 24.0;
    const auto P = [k](qreal x, qreal y) { return QPointF(x * k, y * k); };

    QPixmap pixmap(logical * 2, logical * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(stroke);
    pen.setWidthF(1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    switch (glyph) {
    case CaptionGlyph::Minimize:
        painter.drawLine(P(5, 12), P(19, 12));
        break;
    case CaptionGlyph::Maximize:
        painter.drawRoundedRect(QRectF(P(5, 5), P(19, 19)).normalized(), k, k);
        break;
    case CaptionGlyph::Restore: {
        painter.drawRoundedRect(QRectF(P(8, 3), P(22, 17)).normalized(), k, k);
        QPainterPath back;
        back.moveTo(P(16, 21));
        back.lineTo(P(5, 21));
        back.lineTo(P(3, 19));
        back.lineTo(P(3, 8));
        painter.drawPath(back);
        break;
    }
    case CaptionGlyph::Close:
        painter.drawLine(P(18, 6), P(6, 18));
        painter.drawLine(P(6, 6), P(18, 18));
        break;
    case CaptionGlyph::Plus:
        painter.drawLine(P(12, 5), P(12, 19));
        painter.drawLine(P(5, 12), P(19, 12));
        break;
    case CaptionGlyph::Logo: {
        // Bear mark (the app logo), geometry sampled from logo.png: ears
        // drawn behind the head, eyes and muzzle punched through the head
        // with odd-even fill so any button background shows.
        QPainterPath ears;
        ears.addEllipse(QRectF(P(2.0, 0.8), P(9.7, 8.5)).normalized());
        ears.addEllipse(QRectF(P(14.3, 0.8), P(22.0, 8.5)).normalized());
        painter.setPen(Qt::NoPen);
        painter.setBrush(stroke);
        painter.drawPath(ears);
        QPainterPath head;
        head.addEllipse(QRectF(P(2.0, 4.2), P(22.0, 22.2)).normalized());
        head.addEllipse(QRectF(P(7.8, 12.9), P(10.2, 14.9)).normalized());
        head.addEllipse(QRectF(P(13.8, 12.9), P(16.2, 14.9)).normalized());
        head.addEllipse(QRectF(P(9.25, 14.75), P(14.75, 16.75)).normalized());
        head.setFillRule(Qt::OddEvenFill);
        painter.drawPath(head);
        painter.setPen(pen);
        break;
    }
    }
    painter.end();
    return QIcon(pixmap);
}

const QColor kCaptionInk = QColor(0x1f, 0x23, 0x28);
const QColor kLogoOrange = QColor(0xcb, 0x83, 0x06);


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

QString profileListSubtitle(const Profile &profile, const std::vector<std::wstring> &connectedProfileNames)
{
    QString subtitle = profileSubtitle(profile);
    const QString status = QString::fromStdWString(connectionListStatusText(profile.name, connectedProfileNames));
    if (!status.isEmpty())
        subtitle += QStringLiteral("  %1").arg(status);
    return subtitle;
}

QString sessionStateText(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Starting:
        return QObject::tr("Connecting");
    case FreeRdpProcess::State::Running:
        return QObject::tr("Connected");
    case FreeRdpProcess::State::Idle:
    case FreeRdpProcess::State::Finished:
        return QObject::tr("Disconnected");
    }
    return QObject::tr("Disconnected");
}

QString sessionTabTitle(const Profile &profile, FreeRdpProcess::State state)
{
    if (state == FreeRdpProcess::State::Running)
        return profileTitle(profile);

    return QStringLiteral("%1 - %2").arg(profileTitle(profile), sessionStateText(state));
}

QString sessionTabTooltip(const Profile &profile, FreeRdpProcess::State state)
{
    return QStringLiteral("%1\n%2")
        .arg(profileSubtitle(profile), sessionStateText(state));
}

ui::MainWindowConnectionInfo mainWindowConnectionInfo(const FreeRdpProcess::ConnectionInfo &info)
{
    return ui::MainWindowConnectionInfo{info.codecName, info.rtt, info.rttAvailable};
}

QIcon sessionStatusIcon(ui::MainWindowTabStatus status)
{
    QColor color;
    switch (status) {
    case ui::MainWindowTabStatus::ConnectedGood:
        color = QColor(34, 197, 94);
        break;
    case ui::MainWindowTabStatus::ConnectedWarn:
        color = QColor(245, 158, 11);
        break;
    case ui::MainWindowTabStatus::ConnectedBad:
        color = QColor(239, 68, 68);
        break;
    case ui::MainWindowTabStatus::Inactive:
    default:
        return {};
    }

    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(2, 2, 8, 8);
    return QIcon(pixmap);
}

QString aboutVersionText()
{
    return QStringLiteral("RdpBox %1").arg(QString::fromWCharArray(RDPBOX_VERSION));
}

QString aboutBuildDateText()
{
    return QString::fromLatin1(__DATE__);
}

QString repositoryUrlText()
{
    return QString::fromWCharArray(RDPBOX_GITHUB_URL);
}

QString qtUpdateTooltipText(ui::UpdateUiState state,
                            const std::wstring &tagName,
                            int downloadProgress)
{
    return QString::fromStdWString(ui::updateTooltipText(state, tagName, downloadProgress));
}

QString qtUpdateButtonText(ui::UpdateUiState state, int downloadProgress)
{
    return QString::fromStdWString(ui::updateButtonText(state, downloadProgress));
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

class ProfileListWidget final : public QListWidget
{
public:
    using DropCallback = std::function<void(int sourceRow, int insertIndex)>;
    using KeyboardMoveCallback = std::function<bool(int delta)>;
    using ActivateCallback = std::function<void()>;

    explicit ProfileListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
    }

    void setDropCallback(DropCallback callback)
    {
        m_dropCallback = std::move(callback);
    }

    void setKeyboardMoveCallback(KeyboardMoveCallback callback)
    {
        m_keyboardMoveCallback = std::move(callback);
    }

    void setActivateCallback(ActivateCallback callback)
    {
        m_activateCallback = std::move(callback);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event) {
            const bool isEnterKey = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
            if (shouldActivateConnectionListSelection(event->modifiers() != Qt::NoModifier, isEnterKey)
                && m_activateCallback) {
                m_activateCallback();
                event->accept();
                return;
            }
        }

        if (event && event->modifiers() == Qt::NoModifier) {
            QListWidget::keyPressEvent(event);
            return;
        }

        if (event) {
            const Qt::KeyboardModifiers modifiers = event->modifiers();
            const std::optional<int> delta = keyboardMoveDeltaForConnectionList(
                modifiers.testFlag(Qt::ControlModifier),
                modifiers.testFlag(Qt::AltModifier),
                modifiers.testFlag(Qt::ShiftModifier),
                event->key() == Qt::Key_Up,
                event->key() == Qt::Key_Down);
            if (delta.has_value() && m_keyboardMoveCallback && m_keyboardMoveCallback(*delta)) {
                event->accept();
                return;
            }
        }

        QListWidget::keyPressEvent(event);
    }

    void startDrag(Qt::DropActions supportedActions) override
    {
        const QList<QListWidgetItem *> selected = selectedItems();
        if (selected.size() != 1)
            return;

        m_dragSourceRow = row(selected.front());
        QListWidget::startDrag(supportedActions);
        m_dragSourceRow = -1;
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!event)
            return;

        if (m_dragSourceRow < 0 || selectedItems().size() != 1 || !m_dropCallback) {
            QListWidget::dropEvent(event);
            return;
        }

        const int sourceRow = m_dragSourceRow;
        const int insertIndex = dropInsertIndex(event->pos());
        event->setDropAction(Qt::MoveAction);
        event->accept();
        m_dragSourceRow = -1;
        m_dropCallback(sourceRow, insertIndex);
    }

private:
    int dropInsertIndex(const QPoint &point) const
    {
        if (count() <= 0)
            return 0;

        const QListWidgetItem *item = itemAt(point);
        if (!item)
            return point.y() < 0 ? 0 : count();

        const int itemRow = row(item);
        const QRect itemRect = visualItemRect(item);
        const int topThreshold = itemRect.top() + itemRect.height() / 3;
        const int bottomThreshold = itemRect.bottom() - itemRect.height() / 3;

        if (m_dragSourceRow >= 0) {
            if (itemRow > m_dragSourceRow)
                return point.y() < topThreshold ? itemRow : itemRow + 1;
            if (itemRow < m_dragSourceRow)
                return point.y() > bottomThreshold ? itemRow + 1 : itemRow;
        }

        return point.y() < itemRect.center().y() ? itemRow : itemRow + 1;
    }

    DropCallback m_dropCallback;
    KeyboardMoveCallback m_keyboardMoveCallback;
    ActivateCallback m_activateCallback;
    int m_dragSourceRow = -1;
};
}

QtMainWindow::QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                           QWidget *parent)
    : QMainWindow(parent),
      m_repository(AppPaths::profilesFilePath()),
      m_startupConnectionNames(std::move(startupConnectionNames))
{
    // Native frame styles survive (snap, double-click maximize, aero shake);
    // WM_NCCALCSIZE in FramelessWin.h removes the visible frame.
    frameless::apply(this);
    setWindowTitle(QStringLiteral("RdpBox"));
    setWindowIcon(QApplication::windowIcon());
    resize(1180, 760);
    buildUi();
    installShortcuts();
    restoreWindowState();
    refreshProfileList();
    refreshUpdateButton();

    m_updateCheckTimer = new QTimer(this);
    m_updateCheckTimer->setInterval(kUpdateCheckIntervalMs);
    connect(m_updateCheckTimer, &QTimer::timeout, this, [this]() {
        startBackgroundUpdateCheck(false);
    });
    m_updateCheckTimer->start();
    QTimer::singleShot(0, this, [this]() {
        startBackgroundUpdateCheck(false);
    });

    m_tabStatusTimer = new QTimer(this);
    m_tabStatusTimer->setInterval(2000);
    connect(m_tabStatusTimer, &QTimer::timeout, this, [this]() {
        refreshSessionTabStatuses();
    });
    m_tabStatusTimer->start();

    if (!m_startupConnectionNames.empty()) {
        QTimer::singleShot(0, this, [this]() {
            openConnectionsByName(m_startupConnectionNames);
        });
    }
}

bool QtMainWindow::eventFilter(QObject *object, QEvent *event)
{
    // White X while the red hover wash is up (icons are pre-rendered
    // pixmaps, QSS cannot recolor them).
    if (object == m_closeButton && event) {
        if (event->type() == QEvent::Enter) {
            m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, Qt::white));
        } else if (event->type() == QEvent::Leave) {
            m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, kCaptionInk));
        }
    }

    return QMainWindow::eventFilter(object, event);
}

bool QtMainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
        return QMainWindow::nativeEvent(eventType, message, result);

    MSG *msg = static_cast<MSG *>(message);
    if (!msg)
        return QMainWindow::nativeEvent(eventType, message, result);

    if (msg->message == WM_ACTIVATE) {
        if (ui::shouldFocusActiveSessionOnActivate(LOWORD(msg->wParam), HIWORD(msg->wParam) != 0)) {
            if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(m_tabs ? m_tabs->currentIndex() : -1))
                sessionWidget->handleBecameVisible();
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    if (msg->message == WM_POWERBROADCAST) {
        const ui::MainWindowPowerBroadcastPlan plan =
            ui::powerBroadcastPlan(static_cast<unsigned int>(msg->wParam),
                                   m_tabs && m_tabs->count() > 1);
        if (plan.handleHostResume)
            handleHostResume();
        if (result)
            *result = TRUE;
        return true;
    }

    if (msg->message == WM_NCHITTEST && m_isFullScreen) {
        if (result)
            *result = HTCLIENT;
        return true;
    }

    if (frameless::nativeEvent(this, eventType, message, result,
                               [this](const QPoint &pos) {
                                   return nativeHitTestForPoint(pos) == HTCAPTION
                                       ? frameless::Zone::Caption
                                       : frameless::Zone::Client;
                               })) {
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
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

    m_sidebar = new QWidget(splitter);
    m_sidebar->setMinimumWidth(220);
    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(14, 14, 14, 14);
    sidebarLayout->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("Connections"), m_sidebar);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    auto *addButton = new QToolButton(m_sidebar);
    addButton->setObjectName(QStringLiteral("addButton"));
    addButton->setIcon(captionIcon(CaptionGlyph::Plus, kCaptionInk));
    addButton->setIconSize(QSize(16, 16));
    addButton->setFixedSize(24, 24);
    addButton->setAutoRaise(true);
    addButton->setFocusPolicy(Qt::NoFocus);
    addButton->setToolTip(tr("New connection"));
    connect(addButton, &QToolButton::clicked, this, [this]() {
        addProfile();
    });
    titleRow->addWidget(addButton);

    m_searchEdit = new QLineEdit(m_sidebar);
    m_searchEdit->setPlaceholderText(tr("Search"));

    auto *profileList = new ProfileListWidget(m_sidebar);
    profileList->setDropCallback([this](int sourceRow, int insertIndex) {
        moveProfileByDrop(sourceRow, insertIndex);
    });
    profileList->setKeyboardMoveCallback([this](int delta) {
        return moveSelectedProfileBy(delta);
    });
    profileList->setActivateCallback([this]() {
        connectSelectedProfiles();
    });
    m_profileList = profileList;
    m_profileList->setUniformItemSizes(true);
    m_profileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_profileList->setDragEnabled(true);
    m_profileList->setAcceptDrops(true);
    m_profileList->setDragDropMode(QAbstractItemView::InternalMove);
    m_profileList->setDefaultDropAction(Qt::MoveAction);
    m_profileList->setDropIndicatorShown(true);

    sidebarLayout->addLayout(titleRow);
    sidebarLayout->addWidget(m_searchEdit);
    sidebarLayout->addWidget(m_profileList, 1);

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

    splitter->addWidget(m_sidebar);
    splitter->addWidget(workspace);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 880});

    shellLayout->addWidget(splitter, 1);
    setCentralWidget(shell);
    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel, 1);

    m_versionButton = new QToolButton(statusBar());
    m_versionButton->setObjectName(QStringLiteral("versionButton"));
    m_versionButton->setText(QString::fromWCharArray(RDPBOX_VERSION));
    m_versionButton->setToolTip(tr("About RdpBox"));
    m_versionButton->setAutoRaise(true);
    m_versionButton->setFocusPolicy(Qt::NoFocus);
    m_versionButton->setCursor(Qt::PointingHandCursor);
    statusBar()->addPermanentWidget(m_versionButton, 0);
    connect(m_versionButton, &QToolButton::clicked, this, [this]() {
        showAboutDialog();
    });

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        refreshProfileList();
    });
    connect(m_profileList, &QListWidget::itemSelectionChanged, this, [this]() {
        refreshActions();
    });
    connect(m_profileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item)
            return;

        const Profile profile =
            m_repository.profileByName(item->data(Qt::UserRole).toString().toStdWString());
        if (profile.isValid())
            addSessionTab(profile);
    });
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        closeSessionTab(index);
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index))
            sessionWidget->handleBecameVisible();
    });
    if (QTabBar *bar = m_tabs->tabBar()) {
        bar->setMovable(true);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(bar, &QTabBar::tabMoved, this, [this](int fromIndex, int toIndex) {
            handleTabMoved(fromIndex, toIndex);
        });
        connect(bar, &QTabBar::customContextMenuRequested, this, [this](const QPoint &point) {
            showTabContextMenu(point);
        });
    }

    refreshActions();
}


void QtMainWindow::buildTitleBar(QVBoxLayout *rootLayout)
{
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(kTitleBarHeight);

    auto *layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_logoButton = new QToolButton(m_titleBar);
    m_logoButton->setObjectName(QStringLiteral("logoButton"));
    m_logoButton->setFixedSize(45, kTitleBarHeight - 1);
    m_logoButton->setToolTip(tr("Toggle connections panel"));
    m_logoButton->setCheckable(true);
    m_logoButton->setChecked(true);
    m_logoButton->setIcon(captionIcon(
        CaptionGlyph::Logo, m_logoButton->isChecked() ? Qt::white : kLogoOrange, 22));
    m_logoButton->setIconSize(QSize(22, 22));
    m_logoButton->setAutoRaise(true);
    m_logoButton->setFocusPolicy(Qt::NoFocus);
    connect(m_logoButton, &QToolButton::toggled, this, [this](bool checked) {
        m_logoButton->setIcon(captionIcon(CaptionGlyph::Logo, checked ? Qt::white : kLogoOrange, 22));
        if (m_sidebar)
            m_sidebar->setVisible(checked);
    });

    m_titleLabel = new QLabel(QStringLiteral("RdpBox"), m_titleBar);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_updateButton = new QToolButton(m_titleBar);
    m_minimizeButton = new QToolButton(m_titleBar);
    m_maximizeButton = new QToolButton(m_titleBar);
    m_closeButton = new QToolButton(m_titleBar);

    m_updateButton->setObjectName(QStringLiteral("captionButton"));
    m_minimizeButton->setObjectName(QStringLiteral("captionButton"));
    m_maximizeButton->setObjectName(QStringLiteral("captionButton"));
    m_closeButton->setObjectName(QStringLiteral("closeCaptionButton"));
    m_updateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_minimizeButton->setIcon(captionIcon(CaptionGlyph::Minimize, kCaptionInk));
    m_maximizeButton->setIcon(captionIcon(CaptionGlyph::Maximize, kCaptionInk));
    m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, kCaptionInk));
    m_closeButton->installEventFilter(this);
    m_updateButton->setToolTip(tr("Check for updates"));
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_maximizeButton->setToolTip(tr("Maximize"));
    m_closeButton->setToolTip(tr("Close"));
    m_updateButton->setAutoRaise(true);
    m_updateButton->setFixedSize(64, kTitleBarHeight);
    m_updateButton->setFocusPolicy(Qt::NoFocus);
    m_updateButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const std::array<QToolButton *, 3> captionButtons = {
        m_minimizeButton,
        m_maximizeButton,
        m_closeButton,
    };
    for (QToolButton *button : captionButtons) {
        button->setAutoRaise(true);
        button->setFixedSize(46, kTitleBarHeight);
        button->setIconSize(QSize(16, 16));
        button->setFocusPolicy(Qt::NoFocus);
    }

    layout->addWidget(m_logoButton);
    layout->addWidget(m_titleLabel);
    layout->addStretch(1);
    layout->addWidget(m_updateButton);
    layout->addWidget(m_minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(m_closeButton);
    rootLayout->addWidget(m_titleBar);

    connect(m_minimizeButton, &QToolButton::clicked, this, [this]() {
        showMinimized();
    });
    connect(m_updateButton, &QToolButton::clicked, this, [this]() {
        handleUpdateButtonClicked();
    });
    connect(m_maximizeButton, &QToolButton::clicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_closeButton, &QToolButton::clicked, this, [this]() {
        close();
    });

    refreshWindowControls();
}

void QtMainWindow::installShortcuts()
{
    {
        QSettings store;
        rdpbox::setCurrentShortcuts(rdpbox::loadShortcutSettings(store));
    }

    const auto bindShortcut = [this](QShortcut *&slot, auto handler) {
        auto *shortcut = new QShortcut(this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
        slot = shortcut;
    };

    bindShortcut(m_newConnectionShortcut, [this]() {
        addProfile(true);
    });
    bindShortcut(m_openConnectionsShortcut, [this]() {
        focusConnections();
    });
    bindShortcut(m_fullScreenShortcut, [this]() {
        toggleFullScreen();
    });
    bindShortcut(m_exitFullScreenShortcut, [this]() {
        if (m_isFullScreen)
            setFullScreen(false);
    });

    applyShortcutSettings();
}

void QtMainWindow::applyShortcutSettings()
{
    const rdpbox::ShortcutSettings &settings = rdpbox::currentShortcuts();
    m_newConnectionShortcut->setKey(settings.newConnection);
    m_openConnectionsShortcut->setKey(settings.openConnections);
    m_fullScreenShortcut->setKey(settings.toggleFullScreen);
    m_exitFullScreenShortcut->setKey(settings.exitFullScreen);
}

void QtMainWindow::showShortcutsDialog()
{
    QtShortcutsDialog dialog(rdpbox::currentShortcuts(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    rdpbox::setCurrentShortcuts(dialog.shortcutSettings());
    applyShortcutSettings();
    QSettings store;
    rdpbox::saveShortcutSettings(rdpbox::currentShortcuts(), store);
}

void QtMainWindow::refreshProfileList()
{
    const std::vector<std::wstring> previousSelection = selectedProfileNames();
    const std::vector<Profile> profiles = currentVisibleProfiles();
    const std::vector<std::wstring> connectedNames = connectedProfileNames();
    const std::vector<int> retainedRows =
        retainedSelectionRowsForProfiles(profiles, previousSelection);

    m_profileList->clear();
    for (const Profile &profile : profiles) {
        auto *item = new QListWidgetItem(m_profileList);
        item->setText(profileTitle(profile)
                      + QStringLiteral("\n")
                      + profileListSubtitle(profile, connectedNames));
        item->setData(Qt::UserRole, QString::fromStdWString(profile.name));
        item->setSizeHint(QSize(0, 54));
    }

    if (!retainedRows.empty()) {
        m_profileList->setCurrentRow(retainedRows.front());
        for (int row : retainedRows) {
            if (QListWidgetItem *item = m_profileList->item(row))
                item->setSelected(true);
        }
    }

    m_statusLabel->setText(tr("%n connection(s)", nullptr, static_cast<int>(m_repository.profiles().size())));
    refreshActions();
}

void QtMainWindow::refreshActions()
{
    // Sidebar entry points: add button (always on), double-click to connect,
    // drag / keyboard to reorder - nothing left to enable/disable here.
}

void QtMainWindow::refreshUpdateButton()
{
    if (!m_updateButton)
        return;

    const ui::UpdateUiState state = updateUiState();
    const bool visible = ui::shouldShowUpdateButton(state);
    m_updateButton->setVisible(visible);
    if (!visible)
        return;

    m_updateButton->setToolTip(qtUpdateTooltipText(state, m_updateRelease.tagName, m_updateDownloadProgress));
    m_updateButton->setText(qtUpdateButtonText(state, m_updateDownloadProgress));
    m_updateButton->setEnabled(state != ui::UpdateUiState::Downloading);
    if (state == ui::UpdateUiState::Downloaded)
        m_updateButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    else if (state == ui::UpdateUiState::Downloading)
        m_updateButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    else
        m_updateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
}

void QtMainWindow::refreshWindowControls()
{
    if (!m_maximizeButton)
        return;

    m_maximizeButton->setIcon(captionIcon(
        isMaximized() ? CaptionGlyph::Restore : CaptionGlyph::Maximize, kCaptionInk));
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
    if (m_isFullScreen)
        return;

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
    if (m_isFullScreen)
        return HTCLIENT;

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

void QtMainWindow::addProfile(bool connectAfterAdd)
{
    QtProfileDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Profile profile = dialog.profile();
    if (!m_repository.addProfile(profile)) {
        QMessageBox::warning(this, tr("Connection"), tr("A connection with this name already exists."));
        return;
    }

    refreshProfileList();
    selectProfileByName(profile.name);
    if (connectAfterAdd)
        addSessionTab(profile);
}

void QtMainWindow::editSelectedProfile()
{
    const std::vector<std::wstring> names = selectedProfileNames();
    if (names.size() != 1)
        return;

    const std::wstring &currentName = names.front();
    QtProfileDialog dialog(this);
    dialog.setProfile(m_repository.profileByName(currentName));
    if (dialog.exec() != QDialog::Accepted)
        return;

    const Profile profile = dialog.profile();
    if (!m_repository.updateProfile(currentName, profile)) {
        QMessageBox::warning(this, tr("Connection"), tr("A connection with this name already exists."));
        return;
    }

    refreshProfileList();
    selectProfileByName(profile.name);
}

void QtMainWindow::duplicateSelectedProfile()
{
    const std::vector<int> selectedRows = selectedProfileRows();
    if (selectedRows.empty())
        return;

    std::vector<std::wstring> duplicateNames;
    std::vector<std::wstring> takenNames;
    for (const Profile &profile : m_repository.profiles())
        takenNames.push_back(profile.name);
    const std::vector<Profile> visibleProfiles = currentVisibleProfiles();
    for (int row : selectedRows) {
        if (row < 0 || row >= static_cast<int>(visibleProfiles.size()))
            continue;

        const Profile duplicate = duplicateProfileDraft(visibleProfiles[static_cast<std::size_t>(row)],
                                                        takenNames);
        if (!m_repository.addProfile(duplicate)) {
            QMessageBox::warning(
                this,
                tr("Connection"),
                tr("A connection with this name already exists."));
            continue;
        }
        duplicateNames.push_back(duplicate.name);
        takenNames.push_back(duplicate.name);
    }

    refreshProfileList();
    if (!duplicateNames.empty())
        selectProfileByName(duplicateNames.front());
}

void QtMainWindow::deleteSelectedProfile()
{
    const std::vector<std::wstring> names = selectedProfileNames();
    if (names.empty())
        return;

    const QString message = names.size() == 1
        ? tr("Delete \"%1\"?").arg(QString::fromStdWString(names.front()))
        : tr("Delete %n connections?", nullptr, static_cast<int>(names.size()));
    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Delete Connection"),
        message,
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
        return;

    for (const std::wstring &name : names)
        m_repository.removeProfile(name);
    refreshProfileList();
}

bool QtMainWindow::moveSelectedProfileBy(int delta)
{
    if (!m_profileList)
        return false;

    const std::vector<int> selectedRows = selectedProfileRows();
    if (selectedRows.size() != 1)
        return false;

    const int currentRow = selectedRows.front();
    const std::vector<Profile> visibleProfiles = currentVisibleProfiles();
    const std::optional<int> targetRow =
        targetSelectionIndex(currentRow, static_cast<int>(visibleProfiles.size()), delta);
    if (!targetRow.has_value())
        return false;

    const Profile movedProfile =
        currentRow >= 0 && currentRow < static_cast<int>(visibleProfiles.size())
            ? visibleProfiles[static_cast<std::size_t>(currentRow)]
            : Profile();
    if (!movedProfile.isValid())
        return false;

    const int insertIndex = delta > 0 ? *targetRow + 1 : *targetRow;
    const std::size_t targetIndex =
        repositoryTargetIndexForVisibleInsertIndex(m_repository.profiles(), visibleProfiles, insertIndex);
    if (!m_repository.moveProfile(movedProfile.name, targetIndex))
        return false;

    refreshProfileList();
    selectProfileByName(movedProfile.name);
    return true;
}

void QtMainWindow::moveProfileByDrop(int sourceRow, int insertIndex)
{
    const std::vector<Profile> visibleProfiles = currentVisibleProfiles();
    if (sourceRow < 0 || sourceRow >= static_cast<int>(visibleProfiles.size()))
        return;

    const Profile movedProfile = visibleProfiles[static_cast<std::size_t>(sourceRow)];
    if (!movedProfile.isValid())
        return;

    const std::size_t targetIndex =
        repositoryTargetIndexForVisibleInsertIndex(m_repository.profiles(), visibleProfiles, insertIndex);
    if (!m_repository.moveProfile(movedProfile.name, targetIndex))
        return;

    refreshProfileList();
    selectProfileByName(movedProfile.name);
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
    refreshProfileList();
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

void QtMainWindow::toggleFullScreen()
{
    setFullScreen(!m_isFullScreen);
}

void QtMainWindow::setFullScreen(bool enabled)
{
    if (enabled == m_isFullScreen)
        return;

    if (enabled)
        saveWindowState();

    if (enabled)
        m_wasMaximizedBeforeFullScreen = isMaximized();

    m_isFullScreen = enabled;
    if (m_titleBar)
        m_titleBar->setVisible(!enabled);
    if (m_sidebar)
        m_sidebar->setVisible(!enabled);
    if (m_tabs && m_tabs->tabBar())
        m_tabs->tabBar()->setVisible(!enabled);
    if (statusBar())
        statusBar()->setVisible(!enabled);

    if (enabled)
        showFullScreen();
    else if (m_wasMaximizedBeforeFullScreen)
        showMaximized();
    else
        showNormal();

    refreshWindowControls();
}

void QtMainWindow::focusConnections()
{
    if (m_isFullScreen)
        setFullScreen(false);
    if (m_sidebar)
        m_sidebar->show();
    if (m_searchEdit) {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    }
}

void QtMainWindow::showAboutDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About RdpBox"));
    dialog.setWindowIcon(windowIcon());
    dialog.setModal(true);

    auto *layout = new QGridLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setHorizontalSpacing(14);
    layout->setVerticalSpacing(8);

    auto *icon = new QLabel(&dialog);
    icon->setFixedSize(56, 56);
    icon->setPixmap(windowIcon().pixmap(48, 48));
    icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    auto *title = new QLabel(aboutVersionText(), &dialog);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *buildLabel = new QLabel(tr("Built"), &dialog);
    auto *buildValue = new QLabel(aboutBuildDateText(), &dialog);
    auto *repoLabel = new QLabel(tr("Repository"), &dialog);
    auto *repoValue = new QLabel(&dialog);
    const QString repoUrl = repositoryUrlText();
    repoValue->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(repoUrl.toHtmlEscaped()));
    repoValue->setTextFormat(Qt::RichText);
    repoValue->setTextInteractionFlags(Qt::TextBrowserInteraction);
    repoValue->setOpenExternalLinks(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    layout->addWidget(icon, 0, 0, 4, 1);
    layout->addWidget(title, 0, 1, 1, 2);
    layout->addWidget(buildLabel, 1, 1);
    layout->addWidget(buildValue, 1, 2);
    layout->addWidget(repoLabel, 2, 1);
    layout->addWidget(repoValue, 2, 2);
    layout->addWidget(buttons, 4, 0, 1, 3);
    layout->setColumnStretch(2, 1);

    dialog.exec();
}

void QtMainWindow::handleUpdateButtonClicked()
{
    switch (updateUiState()) {
    case ui::UpdateUiState::Available:
        startBackgroundUpdateDownload();
        break;
    case ui::UpdateUiState::Downloaded:
        confirmLaunchDownloadedUpdate();
        break;
    case ui::UpdateUiState::Hidden:
        startBackgroundUpdateCheck(true);
        break;
    case ui::UpdateUiState::Downloading:
    default:
        break;
    }
}

ui::UpdateUiState QtMainWindow::updateUiState() const
{
    return m_updateState;
}

void QtMainWindow::startBackgroundUpdateCheck(bool userInitiated)
{
    if (m_updateCheckInFlight || m_updateDownloadInFlight)
        return;

    m_updateCheckInFlight = true;
    const std::uint64_t generation = ++m_updateCheckGeneration;
    if (userInitiated && m_statusLabel)
        m_statusLabel->setText(tr("Checking for updates..."));

    QPointer<QtMainWindow> target(this);
    std::thread([target, generation, userInitiated]() {
        auto result = std::make_shared<updater::ReleaseAsset>();
        std::wstring error;
        bool hasUpdate = false;
        const bool success = updater::fetchLatestRelease(L"o3ku", L"RdpBox", L"RdpBox.exe", *result, error);
        if (success)
            hasUpdate = updater::isNewerReleaseTag(RDPBOX_VERSION, result->tagName);

        if (!target)
            return;

        QMetaObject::invokeMethod(target.data(), [target,
                                                  generation,
                                                  userInitiated,
                                                  success,
                                                  error,
                                                  result,
                                                  hasUpdate]() {
            if (!target)
                return;

            target->handleUpdateCheckCompleted(generation,
                                               userInitiated,
                                               success,
                                               error,
                                               *result,
                                               hasUpdate);
        }, Qt::QueuedConnection);
    }).detach();
}

void QtMainWindow::startBackgroundUpdateDownload()
{
    if (m_updateDownloadInFlight || m_updateRelease.downloadUrl.empty())
        return;

    m_updateDownloadInFlight = true;
    m_updateState = ui::UpdateUiState::Downloading;
    m_updateDownloadProgress = 0;
    refreshUpdateButton();
    if (m_statusLabel)
        m_statusLabel->setText(tr("Downloading update..."));

    const std::uint64_t generation = ++m_updateDownloadGeneration;
    const updater::ReleaseAsset release = m_updateRelease;
    QPointer<QtMainWindow> target(this);
    std::thread([target, generation, release]() {
        std::wstring error;
        const std::wstring updatesDir = AppPaths::updatesDirectoryPath();
        const std::wstring targetPath = updatesDir.empty()
            ? std::wstring()
            : (updatesDir + L"\\" + ui::updateReleaseFileName(release.tagName));

        auto progressCallback = [target, generation](std::uint64_t bytesReceived, std::uint64_t totalBytes) {
            if (!target)
                return;

            const int progress = ui::updateDownloadProgressPercent(bytesReceived, totalBytes);
            QMetaObject::invokeMethod(target.data(), [target, generation, progress]() {
                if (target)
                    target->handleUpdateDownloadProgress(generation, progress);
            }, Qt::QueuedConnection);
        };

        const bool success = !targetPath.empty()
            && updater::downloadReleaseAsset(release, targetPath, error, progressCallback);

        if (!target)
            return;

        QMetaObject::invokeMethod(target.data(), [target, generation, success, error]() {
            if (target)
                target->handleUpdateDownloadCompleted(generation, success, error);
        }, Qt::QueuedConnection);
    }).detach();
}

void QtMainWindow::handleUpdateCheckCompleted(std::uint64_t generation,
                                              bool userInitiated,
                                              bool success,
                                              const std::wstring &errorMessage,
                                              const updater::ReleaseAsset &release,
                                              bool hasUpdate)
{
    m_updateCheckInFlight = false;
    if (generation != m_updateCheckGeneration)
        return;

    if (!success) {
        if (userInitiated) {
            QMessageBox::warning(this,
                                 tr("Update Check Failed"),
                                 QString::fromStdWString(errorMessage.empty()
                                     ? std::wstring(L"Failed to check for updates.")
                                     : errorMessage));
        }
        refreshProfileList();
        return;
    }

    if (!hasUpdate) {
        m_updateRelease = {};
        m_updateState = ui::UpdateUiState::Hidden;
        m_updateDownloadProgress = -1;
        refreshUpdateButton();
        if (userInitiated)
            QMessageBox::information(this, tr("RdpBox"), tr("RdpBox is up to date."));
        refreshProfileList();
        return;
    }

    m_updateRelease = release;
    const std::wstring path = downloadedUpdatePath();
    m_updateState = (!path.empty() && std::filesystem::exists(path))
        ? ui::UpdateUiState::Downloaded
        : ui::UpdateUiState::Available;
    m_updateDownloadProgress = (m_updateState == ui::UpdateUiState::Downloaded) ? 100 : -1;
    refreshUpdateButton();
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Update %1 available").arg(QString::fromStdWString(m_updateRelease.tagName)));
    }
}

void QtMainWindow::handleUpdateDownloadProgress(std::uint64_t generation, int progress)
{
    if (generation != m_updateDownloadGeneration)
        return;

    m_updateDownloadProgress = progress;
    refreshUpdateButton();
}

void QtMainWindow::handleUpdateDownloadCompleted(std::uint64_t generation,
                                                 bool success,
                                                 const std::wstring &errorMessage)
{
    m_updateDownloadInFlight = false;
    if (generation != m_updateDownloadGeneration)
        return;

    if (!success) {
        QMessageBox::warning(this,
                             tr("Update Download Failed"),
                             QString::fromStdWString(errorMessage.empty()
                                 ? std::wstring(L"Failed to download update.")
                                 : errorMessage));
        m_updateState = ui::UpdateUiState::Available;
        m_updateDownloadProgress = -1;
        refreshUpdateButton();
        if (m_statusLabel)
            m_statusLabel->setText(tr("Update download failed"));
        return;
    }

    m_updateState = ui::UpdateUiState::Downloaded;
    m_updateDownloadProgress = 100;
    refreshUpdateButton();
    if (m_statusLabel)
        m_statusLabel->setText(tr("Update downloaded"));

    confirmLaunchDownloadedUpdate();
}

std::wstring QtMainWindow::downloadedUpdatePath() const
{
    const std::wstring updateDir = AppPaths::updatesDirectoryPath();
    if (updateDir.empty())
        return {};
    return updateDir + L"\\" + ui::updateReleaseFileName(m_updateRelease.tagName);
}

std::vector<std::wstring> QtMainWindow::openProfileNames() const
{
    std::vector<std::wstring> names;
    if (!m_tabs || !m_tabs->tabBar())
        return names;

    QTabBar *bar = m_tabs->tabBar();
    for (int index = 1; index < m_tabs->count(); ++index) {
        const std::wstring name = bar->tabData(index).toString().toStdWString();
        if (!name.empty())
            names.push_back(name);
    }
    return names;
}

std::vector<std::wstring> QtMainWindow::connectedProfileNames() const
{
    std::vector<std::wstring> names;
    if (!m_tabs || !m_tabs->tabBar())
        return names;

    for (int index = 1; index < m_tabs->count(); ++index) {
        const QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index);
        if (!sessionWidget || !sessionWidget->isConnected())
            continue;

        const QString name = m_tabs->tabBar()->tabData(index).toString();
        if (!name.isEmpty())
            names.push_back(name.toStdWString());
    }
    return names;
}

bool QtMainWindow::confirmLaunchDownloadedUpdate()
{
    QMessageBox box(QMessageBox::Question,
                    tr("Update Downloaded"),
                    QString::fromStdWString(ui::downloadedUpdatePrompt(m_updateRelease.tagName)),
                    QMessageBox::Yes | QMessageBox::No,
                    this);
    box.setDefaultButton(QMessageBox::Yes);
    // The prompt often fires while RdpBox is in the background; stay on top so it is seen.
    box.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    box.show();
    box.raise();
    box.activateWindow();
    if (box.exec() != QMessageBox::Yes)
        return false;

    if (launchDownloadedUpdate()) {
        close();
        return true;
    }

    QMessageBox::warning(this, tr("Update Launch Failed"), tr("Failed to launch downloaded update."));
    return false;
}

bool QtMainWindow::launchDownloadedUpdate() const
{
    const std::wstring downloadedPath = downloadedUpdatePath();
    const std::wstring currentExePath = AppPaths::executablePath();
    if (downloadedPath.empty() || currentExePath.empty())
        return false;

    return updater::applyDownloadedUpdate(downloadedPath, currentExePath, openProfileNames());
}

void QtMainWindow::handleTabMoved(int fromIndex, int toIndex)
{
    if (m_adjustingTabMove || !m_tabs || !m_tabs->tabBar())
        return;

    if (fromIndex != 0 && toIndex != 0)
        return;

    QTabBar *bar = m_tabs->tabBar();
    m_adjustingTabMove = true;
    if (fromIndex == 0)
        bar->moveTab(toIndex, 0);
    else
        bar->moveTab(0, 1);
    m_adjustingTabMove = false;
    configureHomeTab();
}

void QtMainWindow::showTabContextMenu(const QPoint &tabBarPoint)
{
    if (!m_tabs || !m_tabs->tabBar())
        return;

    const int index = m_tabs->tabBar()->tabAt(tabBarPoint);
    if (index < 0)
        return;

    const bool hasSession = index > 0 && sessionWidgetForTab(index);
    const ui::TabContextMenuState state =
        ui::tabContextMenuState(index, m_tabs->currentIndex(), hasSession, m_isFullScreen);

    QMenu menu(this);
    QAction *fullScreenAction = menu.addAction(QString::fromStdWString(state.fullScreenText));
    fullScreenAction->setEnabled(state.fullScreenEnabled);
    menu.addSeparator();
    QAction *reconnectAction = menu.addAction(tr("Reconnect"));
    reconnectAction->setEnabled(state.reconnectEnabled);
    QAction *closeAction = menu.addAction(tr("Close"));
    closeAction->setEnabled(state.closeEnabled);

    QAction *selected = menu.exec(m_tabs->tabBar()->mapToGlobal(tabBarPoint));
    if (!selected)
        return;

    if (selected == fullScreenAction) {
        toggleFullScreen();
    } else if (selected == reconnectAction) {
        reconnectSessionTab(index);
    } else if (selected == closeAction) {
        closeSessionTab(index);
    }
}

void QtMainWindow::reconnectSessionTab(int index)
{
    if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index))
        sessionWidget->reconnect();
}

void QtMainWindow::refreshSessionTabStatuses()
{
    if (!m_tabs || !m_tabs->tabBar())
        return;

    for (int index = 1; index < m_tabs->count(); ++index) {
        QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index);
        if (!sessionWidget)
            continue;

        updateSessionTabState(
            m_tabs->tabBar()->tabData(index).toString().toStdWString(),
            sessionWidget->state());
    }
}

void QtMainWindow::handleHostResume()
{
    if (!m_tabs)
        return;

    const int activeSessionIndex = m_tabs->currentIndex() > 0 ? m_tabs->currentIndex() - 1 : -1;
    for (int tabIndex = 1; tabIndex < m_tabs->count(); ++tabIndex) {
        QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(tabIndex);
        if (!sessionWidget)
            continue;

        const bool autoReconnect =
            sessionResumeActionForTab(tabIndex - 1, activeSessionIndex)
            == SessionResumeAction::AutoReconnect;
        sessionWidget->handleHostResume(autoReconnect);
    }
    refreshSessionTabStatuses();
}

void QtMainWindow::connectSelectedProfiles()
{
    const std::vector<std::wstring> names =
        connectableProfileNamesForSelection(currentVisibleProfiles(),
                                            selectedProfileRows(),
                                            openProfileNames());
    if (names.empty())
        return;

    openConnectionsByName(names);
}

void QtMainWindow::openConnectionsByName(const std::vector<std::wstring> &connectionNames)
{
    const ui::MainWindowOpenPlan plan =
        ui::openPlanForConnectionNames(connectionNames, m_repository.profiles());
    for (const Profile &profile : plan.profilesToOpen)
        addSessionTab(profile);
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

std::vector<int> QtMainWindow::selectedProfileRows() const
{
    std::vector<int> rows;
    if (!m_profileList)
        return rows;

    for (const QListWidgetItem *item : m_profileList->selectedItems()) {
        const int row = m_profileList->row(item);
        if (row >= 0)
            rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end());
    return rows;
}

std::vector<std::wstring> QtMainWindow::selectedProfileNames() const
{
    std::vector<std::wstring> names;
    if (!m_profileList)
        return names;

    for (const QListWidgetItem *item : m_profileList->selectedItems()) {
        if (item)
            names.push_back(item->data(Qt::UserRole).toString().toStdWString());
    }
    return names;
}

std::vector<Profile> QtMainWindow::currentVisibleProfiles() const
{
    const std::wstring query = m_searchEdit ? m_searchEdit->text().toStdWString() : std::wstring();
    return query.empty()
        ? m_repository.profiles()
        : m_repository.search(query);
}

void QtMainWindow::selectProfileByName(const std::wstring &profileName)
{
    if (!m_profileList || profileName.empty())
        return;

    const QString name = QString::fromStdWString(profileName);
    for (int row = 0; row < m_profileList->count(); ++row) {
        QListWidgetItem *item = m_profileList->item(row);
        if (!item)
            continue;

        if (QString::compare(item->data(Qt::UserRole).toString(), name, Qt::CaseInsensitive) == 0) {
            m_profileList->clearSelection();
            m_profileList->setCurrentRow(row);
            m_profileList->scrollToItem(item);
            return;
        }
    }
}

void QtMainWindow::addSessionTab(const Profile &profile)
{
    if (!shouldOpenProfileSession(profile))
        return;

    const int existingIndex = sessionTabIndexForProfileName(profile.name);
    if (existingIndex >= 0) {
        m_tabs->setCurrentIndex(existingIndex);
        return;
    }

    QWidget *page = createSessionPage(profile);
    const int index = m_tabs->addTab(page, profileTitle(profile));
    if (m_tabs->tabBar())
        m_tabs->tabBar()->setTabData(index, QString::fromStdWString(profile.name));
    updateSessionTabState(profile.name, FreeRdpProcess::State::Idle);
    m_tabs->setCurrentIndex(index);
    refreshProfileList();
}

void QtMainWindow::updateSessionTabState(const std::wstring &profileName, FreeRdpProcess::State state)
{
    const int index = sessionTabIndexForProfileName(profileName);
    if (index <= 0 || !m_tabs)
        return;

    const Profile profile = m_repository.profileByName(profileName);
    if (!profile.isValid())
        return;

    const QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index);
    const FreeRdpProcess::ConnectionInfo info =
        sessionWidget ? sessionWidget->connectionInfo() : FreeRdpProcess::ConnectionInfo{};
    const ui::MainWindowConnectionInfo uiInfo = mainWindowConnectionInfo(info);
    const bool connected = sessionWidget && sessionWidget->isConnected();

    QString tooltip = sessionTabTooltip(profile, state);
    const QString connectionTooltip = QString::fromStdWString(ui::tabTooltipText(uiInfo));
    if (!connectionTooltip.isEmpty())
        tooltip += QStringLiteral("\n") + connectionTooltip;

    m_tabs->setTabText(index, sessionTabTitle(profile, state));
    m_tabs->setTabToolTip(index, tooltip);
    m_tabs->setTabIcon(index, sessionStatusIcon(ui::tabStatusForConnection(connected, uiInfo)));
}

int QtMainWindow::sessionTabIndexForProfileName(const std::wstring &profileName) const
{
    if (!m_tabs || profileName.empty())
        return -1;

    const QString name = QString::fromStdWString(profileName);
    QTabBar *bar = m_tabs->tabBar();
    if (!bar)
        return -1;

    for (int index = 1; index < m_tabs->count(); ++index) {
        if (QString::compare(bar->tabData(index).toString(), name, Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

QtRdpSessionWidget *QtMainWindow::sessionWidgetForTab(int index) const
{
    if (!m_tabs || index <= 0 || index >= m_tabs->count())
        return nullptr;

    QWidget *page = m_tabs->widget(index);
    return page ? page->findChild<QtRdpSessionWidget *>() : nullptr;
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
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *surface = new QtRdpSessionWidget(profile, page);
    surface->setObjectName(QStringLiteral("sessionSurface"));
    surface->setStateChangedCallback([this, profile](FreeRdpProcess::State state) {
        updateSessionTabState(profile.name, state);
        if (state == FreeRdpProcess::State::Running) {
            touchLastConnectedAt(profile);
            if (ui::connectionCompletedPlan(profile, m_isFullScreen).enterFullScreen)
                setFullScreen(true);
        }
        refreshProfileList();
    });

    layout->addWidget(surface, 1);
    QTimer::singleShot(0, surface, [surface]() {
        surface->connectToHost();
    });
    return page;
}

std::vector<QRect> QtMainWindow::captionExclusionRects() const
{
    std::vector<QRect> rects;
    for (QWidget *widget : {static_cast<QWidget *>(m_updateButton),
                            static_cast<QWidget *>(m_minimizeButton),
                            static_cast<QWidget *>(m_maximizeButton),
                            static_cast<QWidget *>(m_closeButton)}) {
        if (!widget || !m_titleBar)
            continue;

        const QPoint topLeft = widget->mapTo(this, QPoint(0, 0));
        rects.push_back(QRect(topLeft, widget->size()));
    }
    return rects;
}
