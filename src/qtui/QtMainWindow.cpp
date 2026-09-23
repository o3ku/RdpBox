#include "qtui/QtMainWindow.h"

void applyApplicationTheme(QApplication &application); // QtMain.cpp (global)

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/Win32String.h"
#include "qtui/QtProfileDialog.h"
#include "qtui/QtRdpSessionWidget.h"
#include "qtui/QtShortcutSettings.h"
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
#include <QScreen>
#include <QWindow>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <cmath>

#include <QSvgRenderer>

#include <QComboBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#ifdef _WIN32
#include <windows.h>
#endif

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
    Info,
    Settings,
};

// Lucide (MIT) 24-grid stroke paths, same set family as AtomDataAssistant.
QString lucidePaths(CaptionGlyph glyph)
{
    switch (glyph) {
    case CaptionGlyph::Minimize:
        return QStringLiteral("<path d=\"M5 12h14\"/>");
    case CaptionGlyph::Maximize:
        return QStringLiteral("<rect width=\"14\" height=\"14\" x=\"5\" y=\"5\" rx=\"2\"/>");
    case CaptionGlyph::Restore:
        return QStringLiteral(
            "<rect width=\"13\" height=\"13\" x=\"9\" y=\"9\" rx=\"2\"/>"
            "<path d=\"M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1\"/>");
    case CaptionGlyph::Close:
        return QStringLiteral("<path d=\"M18 6 6 18\"/><path d=\"m6 6 12 12\"/>");
    case CaptionGlyph::Plus:
        return QStringLiteral("<path d=\"M5 12h14\"/><path d=\"M12 5v14\"/>");
    case CaptionGlyph::Info:
        return QStringLiteral(
            "<circle cx=\"12\" cy=\"12\" r=\"10\"/><path d=\"M12 16v-4\"/><path d=\"M12 8h.01\"/>");
    case CaptionGlyph::Settings:
        return QStringLiteral(
            "<path d=\"M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0"
            "l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51"
            "a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0"
            "l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25"
            "a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74"
            "v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08"
            "a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z\"/>"
            "<circle cx=\"12\" cy=\"12\" r=\"3\"/>");
    case CaptionGlyph::Logo:
        break;
    }
    return QString();
}

QIcon captionIcon(CaptionGlyph glyph, const QColor &stroke, int logical = 16)
{
    // Lucide glyphs render via QSvgRenderer; the bear Logo stays hand-drawn
    // below (brand mark, not an open-source icon).
    if (glyph != CaptionGlyph::Logo) {
        const QString svg = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" "
            "stroke=\"%1\" stroke-width=\"2.25\" stroke-linecap=\"round\" "
            "stroke-linejoin=\"round\">%2</svg>")
            .arg(stroke.name(), lucidePaths(glyph));
        QSvgRenderer renderer(svg.toUtf8());
        QPixmap pixmap(logical * 2, logical * 2);
        pixmap.setDevicePixelRatio(2.0);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        // Explicit logical bounds: the bounds-less overload renders into the
        // physical viewport, stacking the DPR transform (AtomDataAssistant
        // Theme.h lesson) - 2x-oversized glyph clipped to a corner fragment.
        renderer.render(&painter, QRectF(0, 0, logical, logical));
        painter.end();
        return QIcon(pixmap);
    }

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

// Title-bar icon ink follows the active theme palette (dark theme needs a
// light ink on the dark title bar).
QColor captionInk()
{
    return QApplication::palette().color(QPalette::WindowText);
}

// Frameless settings-style dialog: themed caption row (reuses the global
// #titleBar QSS tokens + Lucide close glyph) on the FramelessWin plumbing.
class FramelessDialogShell : public QDialog
{
public:
    FramelessDialogShell(QWidget* parent, const QString& title)
        : QDialog(parent)
    {
        frameless::apply(this);
        setWindowTitle(title);
        m_titleBar = new QWidget(this);
        m_titleBar->setObjectName(QStringLiteral("titleBar"));
        m_titleBar->setFixedHeight(42);
        auto* row = new QHBoxLayout(m_titleBar);
        row->setContentsMargins(16, 0, 0, 0);
        row->setSpacing(0);
        auto* label = new QLabel(title, m_titleBar);
        m_closeButton = new QToolButton(m_titleBar);
        m_closeButton->setObjectName(QStringLiteral("closeCaptionButton"));
        m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, captionInk()));
        m_closeButton->setIconSize(QSize(16, 16));
        m_closeButton->setFixedSize(46, 41);
        m_closeButton->setAutoRaise(true);
        m_closeButton->setFocusPolicy(Qt::NoFocus);
        m_closeButton->installEventFilter(this);
        connect(m_closeButton, &QToolButton::clicked, this, &QDialog::reject);
        row->addWidget(label);
        row->addStretch(1);
        row->addWidget(m_closeButton);
    }

    QWidget* titleBar() const { return m_titleBar; }

protected:
    // White X while the red hover wash is up (mirrors the main window).
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (object == m_closeButton && event) {
            if (event->type() == QEvent::Enter)
                m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, Qt::white));
            else if (event->type() == QEvent::Leave)
                m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, captionInk()));
        }
        return QDialog::eventFilter(object, event);
    }

    bool nativeEvent(const QByteArray& type, void* message, long* result) override
    {
        return frameless::nativeEvent(this, type, message, result, [this](const QPoint& pos) {
            if (!m_titleBar->geometry().contains(pos))
                return frameless::Zone::Client;
            if (m_closeButton->geometry().contains(pos - m_titleBar->pos()))
                return frameless::Zone::Client;
            return frameless::Zone::Caption;
        });
    }

private:
    QWidget* m_titleBar = nullptr;
    QToolButton* m_closeButton = nullptr;
};
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

QString connectionDotStyleSheet(FreeRdpProcess::State state)
{
    switch (state) {
    case FreeRdpProcess::State::Running:
        return QStringLiteral("background: #22c55e; border-radius: 5px;");
    case FreeRdpProcess::State::Starting:
        return QStringLiteral("background: #f59e0b; border-radius: 5px;");
    default:
        return QStringLiteral("background: #9aa3ad; border-radius: 5px;");
    }
}

ui::MainWindowConnectionInfo mainWindowConnectionInfo(const FreeRdpProcess::ConnectionInfo &info)
{
    return ui::MainWindowConnectionInfo{info.codecName, info.rtt, info.rttAvailable};
}

QIcon sessionStatusIcon(ui::MainWindowTabStatus status)
{
    QColor color;    switch (status) {
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

#ifdef _WIN32
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

#else
RECT rectFromQRect(const QRect &rect)
{
    RECT value = {};
    value.left = rect.left();
    value.top = rect.top();
    value.right = rect.left() + rect.width();
    value.bottom = rect.top() + rect.height();
    return value;
}

bool monitorInfoForScreen(const QScreen *screen, RECT &monitorRect, RECT &workArea, std::wstring &deviceName)
{
    if (!screen)
        return false;
    monitorRect = rectFromQRect(screen->geometry());
    workArea = rectFromQRect(screen->availableGeometry());
    deviceName = screen->name().toStdWString();
    return true;
}

bool monitorInfoForScreen(const QScreen *screen, RECT &monitorRect, RECT &workArea)
{
    std::wstring ignored;
    return monitorInfoForScreen(screen, monitorRect, workArea, ignored);
}
#endif
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
    QTimer::singleShot(0, this, [this]() {
        updateTabBarOffset();
    });
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
            m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, captionInk()));
        }
    }

    return QMainWindow::eventFilter(object, event);
}

bool QtMainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifndef _WIN32
    return QMainWindow::nativeEvent(eventType, message, result);
#else
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
                                   m_tabs && m_tabs->count() > 0);
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
#endif // _WIN32

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

    m_splitter = new QSplitter(Qt::Horizontal, shell);
    auto *splitter = m_splitter;
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    // Left pane: connections panel (its header lives in the title bar).
    m_sidebar = new QWidget(splitter);
    m_sidebar->setObjectName(QStringLiteral("connectionsPanel"));
    m_sidebar->setMinimumWidth(220);
    auto *sidebarLayout = new QVBoxLayout(m_sidebar);
    sidebarLayout->setContentsMargins(14, 14, 14, 14);
    sidebarLayout->setSpacing(10);

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

    sidebarLayout->addWidget(m_searchEdit);
    sidebarLayout->addWidget(m_profileList, 1);

    // Right pane: session tabs.
    auto *workspace = new QWidget(splitter);
    auto *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    m_tabs = new QTabWidget(workspace);
    m_tabs->setDocumentMode(true);
    workspaceLayout->addWidget(m_tabs);
    // The visible tab bar lives in the title bar; hide QTabWidget's own one.
    m_tabs->tabBar()->setVisible(false);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_tabBar && m_tabBar->currentIndex() != index)
            m_tabBar->setCurrentIndex(index);
    });

    splitter->addWidget(m_sidebar);
    splitter->addWidget(workspace);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 880});
    connect(splitter, &QSplitter::splitterMoved, this, [this]() {
        updateTabBarOffset();
    });
    updateTabBarOffset();

    shellLayout->addWidget(splitter, 1);
    setCentralWidget(shell);

    m_profileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_profileList, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_profileList->itemAt(pos);
        if (!item)
            return;
        m_profileList->setCurrentItem(item);

        QMenu menu(this);
        QAction *connectAction = menu.addAction(tr("Connect"));
        menu.addSeparator();
        QAction *editAction = menu.addAction(tr("Edit"));
        QAction *duplicateAction = menu.addAction(tr("Duplicate"));
        menu.addSeparator();
        QAction *removeAction = menu.addAction(tr("Remove"));
        QAction *selected = menu.exec(m_profileList->mapToGlobal(pos));
        if (selected == connectAction)
            connectSelectedProfiles();
        else if (selected == editAction)
            editSelectedProfile();
        else if (selected == duplicateAction)
            duplicateSelectedProfile();
        else if (selected == removeAction)
            deleteSelectedProfile();
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
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index))
            sessionWidget->handleBecameVisible();
    });
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
        if (m_connectionsHeader)
            m_connectionsHeader->setVisible(checked);
        if (!m_sidebar || isFullScreen())
            return;
        // Window size stays fixed; the session pane absorbs the width change.
        if (!checked && m_sidebar->isVisible()) {
            m_collapsedSidebarWidth = m_sidebar->width();
            m_sidebar->setVisible(false);
        } else if (checked && !m_sidebar->isVisible()) {
            m_sidebar->setVisible(true);
            if (m_splitter)
                m_splitter->setSizes({m_collapsedSidebarWidth,
                                      m_splitter->width() - m_collapsedSidebarWidth});
        }
        updateTabBarOffset();
    });

    m_connectionsHeader = new QWidget(m_titleBar);
    m_connectionsHeader->setObjectName(QStringLiteral("connectionsHeader"));
    auto *connectionsLayout = new QHBoxLayout(m_connectionsHeader);
    connectionsLayout->setContentsMargins(0, 0, 0, 0);
    connectionsLayout->setSpacing(8);
    auto *title = new QLabel(tr("Connections"), m_connectionsHeader);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    connectionsLayout->addWidget(title);
    connectionsLayout->addStretch(1);
    m_addButton = new QToolButton(m_connectionsHeader);
    m_addButton->setObjectName(QStringLiteral("addButton"));
    m_addButton->setIcon(captionIcon(CaptionGlyph::Plus, captionInk()));
    m_addButton->setIconSize(QSize(16, 16));
    m_addButton->setFixedSize(24, 24);
    m_addButton->setAutoRaise(true);
    m_addButton->setFocusPolicy(Qt::NoFocus);
    m_addButton->setToolTip(tr("New connection"));
    connect(m_addButton, &QToolButton::clicked, this, [this]() {
        addProfile();
    });
    connectionsLayout->addWidget(m_addButton);
    auto *headerSeparator = new QWidget(m_connectionsHeader);
    headerSeparator->setObjectName(QStringLiteral("headerSeparator"));
    headerSeparator->setFixedSize(1, 20);
    connectionsLayout->addSpacing(8);
    connectionsLayout->addWidget(headerSeparator);

    m_tabBar = new QTabBar(m_titleBar);
    m_tabBar->setObjectName(QStringLiteral("titleTabBar"));
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setUsesScrollButtons(false);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
        closeSessionTab(index);
    });
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (m_tabs && m_tabs->currentIndex() != index)
            m_tabs->setCurrentIndex(index);
    });
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int fromIndex, int toIndex) {
        if (!m_tabs)
            return;
        QWidget *page = m_tabs->widget(fromIndex);
        const QString label = m_tabBar->tabText(toIndex);
        m_tabs->removeTab(fromIndex);
        m_tabs->insertTab(toIndex, page, label);
    });
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, [this](const QPoint &point) {
        showTabContextMenu(point);
    });

    m_updateButton = new QToolButton(m_titleBar);
    m_infoButton = new QToolButton(m_titleBar);
    m_infoButton->setObjectName(QStringLiteral("captionButton"));
    m_infoButton->setIcon(captionIcon(CaptionGlyph::Settings, captionInk()));
    m_infoButton->setIconSize(QSize(16, 16));
    m_infoButton->setFixedSize(34, kTitleBarHeight - 1);
    m_infoButton->setAutoRaise(true);
    m_infoButton->setFocusPolicy(Qt::NoFocus);
    m_infoButton->setToolTip(tr("Settings"));
    connect(m_infoButton, &QToolButton::clicked, this, [this]() {
        showSettingsDialog();
    });

    m_minimizeButton = new QToolButton(m_titleBar);
    m_maximizeButton = new QToolButton(m_titleBar);
    m_closeButton = new QToolButton(m_titleBar);

    m_updateButton->setObjectName(QStringLiteral("captionButton"));
    m_minimizeButton->setObjectName(QStringLiteral("captionButton"));
    m_maximizeButton->setObjectName(QStringLiteral("captionButton"));
    m_closeButton->setObjectName(QStringLiteral("closeCaptionButton"));
    m_updateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_minimizeButton->setIcon(captionIcon(CaptionGlyph::Minimize, captionInk()));
    m_maximizeButton->setIcon(captionIcon(CaptionGlyph::Maximize, captionInk()));
    m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, captionInk()));
    m_closeButton->installEventFilter(this);
    m_updateButton->setToolTip(tr("Check for updates"));
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_maximizeButton->setToolTip(tr("Maximize"));
    m_closeButton->setToolTip(tr("Close"));
    m_updateButton->setAutoRaise(true);
    m_updateButton->setFixedSize(64, kTitleBarHeight - 1);
    m_updateButton->setFocusPolicy(Qt::NoFocus);
    m_updateButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const std::array<QToolButton *, 3> captionButtons = {
        m_minimizeButton,
        m_maximizeButton,
        m_closeButton,
    };
    for (QToolButton *button : captionButtons) {
        button->setAutoRaise(true);
        button->setFixedSize(46, kTitleBarHeight - 1);
        button->setIconSize(QSize(16, 16));
        button->setFocusPolicy(Qt::NoFocus);
    }

    layout->addWidget(m_logoButton);
    layout->addWidget(m_connectionsHeader);
    layout->addWidget(m_tabBar);
    layout->addStretch(1);
    layout->addWidget(m_updateButton);
    layout->addWidget(m_infoButton);
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
        if (m_logoButton)
            m_logoButton->setChecked(!m_logoButton->isChecked());
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
    // Mirror MFC: Esc only exits full screen; otherwise it must reach the RDP
    // session instead of being eaten by an always-on application shortcut.
    m_exitFullScreenShortcut->setEnabled(m_isFullScreen);
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
        item->setData(Qt::UserRole, QString::fromStdWString(profile.name));
        item->setSizeHint(QSize(0, 54));

        auto *row = new QWidget;
        row->setObjectName(QStringLiteral("connectionRow"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 4, 10, 4);
        auto *textLabel = new QLabel(profileTitle(profile)
                                     + QStringLiteral("\n")
                                     + profileListSubtitle(profile, connectedNames), row);
        auto *dot = new QLabel(row);
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(
            connectionDotStyleSheet(
                sessionStateForProfile(QString::fromStdWString(profile.name))));
        rowLayout->addWidget(textLabel, 1);
        rowLayout->addStretch();
        rowLayout->addWidget(dot, 0, Qt::AlignVCenter);
        m_profileList->setItemWidget(item, row);
    }

    if (!retainedRows.empty()) {
        m_profileList->setCurrentRow(retainedRows.front());
        for (int row : retainedRows) {
            if (QListWidgetItem *item = m_profileList->item(row))
                item->setSelected(true);
        }
    }

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
        isMaximized() ? CaptionGlyph::Restore : CaptionGlyph::Maximize, captionInk()));
    m_maximizeButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
}

void QtMainWindow::saveWindowState() const
{
    if (m_isFullScreen)
        return;

#ifdef _WIN32
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
#else
    QScreen *screen = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!screen)
        return;
    const QRect geometry = isMaximized() ? normalGeometry() : this->geometry();
    RECT monitorRect = {};
    RECT workArea = {};
    std::wstring deviceName;
    if (!monitorInfoForScreen(screen, monitorRect, workArea, deviceName))
        return;
    const RECT workspaceRect = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, workArea);
    WindowState state;
    if (!WindowStateScaling::saveToMonitorWorkArea(rectFromQRect(geometry),
                                                   workspaceRect,
                                                   isMaximized() ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL,
                                                   state)) {
        return;
    }
    state.monitorDeviceName = deviceName;
    m_repository.saveWindowState(state);
#endif
}

bool QtMainWindow::restoreWindowState()
{
    const WindowState state = m_repository.loadWindowState();
    if (!state.valid)
        return false;

#ifdef _WIN32
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

    // Apply through Qt (not SetWindowPlacement): Qt re-applies its own
    // geometry record on show(), which silently reverted the size.
    const QRect geometry(restoredRect.left,
                         restoredRect.top,
                         restoredRect.right - restoredRect.left,
                         restoredRect.bottom - restoredRect.top);
    setGeometry(geometry);
#else
    QScreen *screen = nullptr;
    for (QScreen *candidate : QGuiApplication::screens()) {
        if (candidate->name().toStdWString() == state.monitorDeviceName) {
            screen = candidate;
            break;
        }
    }
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    RECT monitorRect = {};
    RECT workArea = {};
    if (!monitorInfoForScreen(screen, monitorRect, workArea))
        return false;
    const RECT workspaceRect = WindowStateScaling::workspaceRectForMonitorWorkArea(monitorRect, workArea);
    RECT restoredRect = {};
    if (!WindowStateScaling::restoreFromMonitorWorkArea(state, workspaceRect, restoredRect))
        return false;
    const QRect geometry(restoredRect.left,
                         restoredRect.top,
                         restoredRect.right - restoredRect.left,
                         restoredRect.bottom - restoredRect.top);
    setGeometry(geometry);
#endif
    if (state.showCmd == SW_SHOWMAXIMIZED)
        setWindowState(windowState() | Qt::WindowMaximized);
    refreshWindowControls();
    return true;
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
    if (!m_tabs || index < 0 || index >= m_tabs->count())
        return;

    const QString profileName = m_tabBar ? m_tabBar->tabData(index).toString() : QString();
    m_sessionStates.erase(profileName.toStdWString());

    QWidget *page = m_tabs->widget(index);
    m_tabs->removeTab(index);
    if (m_tabBar)
        m_tabBar->removeTab(index);
    if (page)
        page->deleteLater();
    refreshProfileList();

    // No sessions left: bring the connections panel back.
    if (m_tabBar && m_tabBar->count() == 0 && m_logoButton && !m_logoButton->isChecked())
        m_logoButton->setChecked(true);
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
    if (m_tabBar)
        m_tabBar->setVisible(!enabled);

    if (enabled)
        showFullScreen();
    else if (m_wasMaximizedBeforeFullScreen)
        showMaximized();
    else
        showNormal();

    if (m_exitFullScreenShortcut)
        m_exitFullScreenShortcut->setEnabled(enabled);

    refreshWindowControls();
}

void QtMainWindow::updateTabBarOffset()
{
    if (!m_titleBar || !m_sidebar || !m_connectionsHeader || !m_logoButton)
        return;

    int width = 0;
    if (m_sidebar->isVisible() && m_connectionsHeader->isVisible()) {
        const int spacing = m_titleBar->layout()->spacing();
        // Header spans from the logo to the panel's right edge so the + button
        // sits at the panel edge and the tab bar aligns with the session pane.
        // +1: the separator is the LAST pixel INSIDE the header, while the
        // splitter handle starts at the panel edge - widen by one so both
        // lines land on the same x.
        width = m_sidebar->width() - m_logoButton->width() - spacing + 1;
        width = qMax(width, m_connectionsHeader->sizeHint().width());
    }
    m_connectionsHeader->setFixedWidth(width);
}


void QtMainWindow::rethemeCaptionIcons()
{
    if (!m_minimizeButton)
        return;
    m_minimizeButton->setIcon(captionIcon(CaptionGlyph::Minimize, captionInk()));
    m_maximizeButton->setIcon(captionIcon(
        isMaximized() ? CaptionGlyph::Restore : CaptionGlyph::Maximize, captionInk()));
    m_closeButton->setIcon(captionIcon(CaptionGlyph::Close, captionInk()));
    m_infoButton->setIcon(captionIcon(CaptionGlyph::Settings, captionInk()));
    m_addButton->setIcon(captionIcon(CaptionGlyph::Plus, captionInk()));
    m_logoButton->setIcon(captionIcon(
        CaptionGlyph::Logo, m_logoButton->isChecked() ? Qt::white : kLogoOrange, 22));
}

void QtMainWindow::showSettingsDialog()
{
    FramelessDialogShell dialog(this, tr("Settings"));
    dialog.setWindowIcon(windowIcon());
    dialog.setModal(true);

    // --- General tab: theme + language -------------------------------------
    QWidget generalPage;
    QSettings settingsStore;
    const QString themeBefore = settingsStore.value(QStringLiteral("theme"), QStringLiteral("system")).toString();
    const QString languageBefore = settingsStore.value(QStringLiteral("language"), QString()).toString();
    auto *themeCombo = new QComboBox(&generalPage);
    themeCombo->addItem(tr("System"), QStringLiteral("system"));
    themeCombo->addItem(tr("Light"), QStringLiteral("light"));
    themeCombo->addItem(tr("Dark"), QStringLiteral("dark"));
    themeCombo->setCurrentIndex(std::max(0, themeCombo->findData(themeBefore)));
    auto *languageCombo = new QComboBox(&generalPage);
    languageCombo->addItem(tr("English"), QString());
    languageCombo->addItem(QStringLiteral("\u7b80\u4f53\u4e2d\u6587"), QStringLiteral("zh"));
    languageCombo->setCurrentIndex(std::max(0, languageCombo->findData(languageBefore)));
    languageCombo->setToolTip(tr("Takes effect after restart"));
    auto *generalForm = new QFormLayout;
    generalForm->addRow(tr("Theme"), themeCombo);
    generalForm->addRow(tr("Language"), languageCombo);
    auto *generalLayout = new QVBoxLayout(&generalPage);
    generalLayout->setContentsMargins(16, 14, 16, 14);
    generalLayout->addLayout(generalForm);
    generalLayout->addStretch(1);
    generalPage.setObjectName(QStringLiteral("settingsTab"));

    // --- Shortcuts tab ----------------------------------------------------
    QWidget shortcutsPage;
    shortcutsPage.setObjectName(QStringLiteral("settingsTab"));
    shortcutsPage.setObjectName(QStringLiteral("settingsTab"));
    const rdpbox::ShortcutSettings current = rdpbox::currentShortcuts();
    auto *newConnectionEdit = new QKeySequenceEdit(current.newConnection, &shortcutsPage);
    auto *openConnectionsEdit = new QKeySequenceEdit(current.openConnections, &shortcutsPage);
    auto *toggleFullScreenEdit = new QKeySequenceEdit(current.toggleFullScreen, &shortcutsPage);
    auto *form = new QFormLayout;
    form->addRow(tr("New connection"), newConnectionEdit);
    form->addRow(tr("Open connections"), openConnectionsEdit);
    form->addRow(tr("Toggle full screen"), toggleFullScreenEdit);
    auto *shortcutsLayout = new QVBoxLayout(&shortcutsPage);
    shortcutsLayout->setContentsMargins(16, 14, 16, 14);
    shortcutsLayout->addLayout(form);
    shortcutsLayout->addStretch(1);

    // --- About tab ---------------------------------------------------------
    QWidget aboutPage;
    aboutPage.setObjectName(QStringLiteral("settingsTab"));
    auto *aboutLayout = new QGridLayout(&aboutPage);
    aboutLayout->setContentsMargins(16, 18, 16, 14);
    aboutLayout->setHorizontalSpacing(14);
    aboutLayout->setVerticalSpacing(8);
    auto *icon = new QLabel(&aboutPage);
    icon->setFixedSize(56, 56);
    icon->setPixmap(windowIcon().pixmap(48, 48));
    icon->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    auto *title = new QLabel(aboutVersionText(), &aboutPage);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *buildLabel = new QLabel(tr("Build"), &aboutPage);
    auto *buildValue = new QLabel(aboutBuildDateText(), &aboutPage);
    auto *repoLabel = new QLabel(tr("Repository"), &aboutPage);
    auto *repoValue = new QLabel(&aboutPage);
    const QString repoUrl = repositoryUrlText();
    repoValue->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(repoUrl.toHtmlEscaped()));
    repoValue->setTextFormat(Qt::RichText);
    repoValue->setTextInteractionFlags(Qt::TextBrowserInteraction);
    repoValue->setOpenExternalLinks(true);
    aboutLayout->addWidget(icon, 0, 0, 3, 1);
    aboutLayout->addWidget(title, 0, 1, 1, 2);
    aboutLayout->addWidget(buildLabel, 1, 1);
    aboutLayout->addWidget(buildValue, 1, 2);
    aboutLayout->addWidget(repoLabel, 2, 1);
    aboutLayout->addWidget(repoValue, 2, 2);
    aboutLayout->setRowStretch(3, 1);
    aboutLayout->setColumnStretch(2, 1);

    auto *tabs = new QTabWidget(&dialog);
    // Themed separator only: pane/page/tab colors come from the global
    // theme QSS (%WINDOW%), the seam line is translucent theme ink so it
    // reads on both light and dark palettes.
    const QColor seamInk = QApplication::palette().color(QPalette::WindowText);
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane { border: none; border-top: 1px solid rgba(%1,%2,%3,66); }")
        .arg(seamInk.red())
        .arg(seamInk.green())
        .arg(seamInk.blue()));
    tabs->addTab(&generalPage, tr("General"));
    tabs->addTab(&shortcutsPage, tr("Shortcuts"));
    tabs->addTab(&aboutPage, tr("About"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QKeySequence keys[] = {
            newConnectionEdit->keySequence(),
            openConnectionsEdit->keySequence(),
            toggleFullScreenEdit->keySequence(),
        };
        for (int i = 0; i < 3; ++i) {
            for (int j = i + 1; j < 3; ++j) {
                if (!keys[i].isEmpty() && keys[i] == keys[j]) {
                    QMessageBox::warning(&dialog, tr("Shortcuts"),
                                         tr("Two actions use the same shortcut."));
                    return;
                }
            }
        }
        dialog.accept();
    });

    auto *body = new QWidget(&dialog);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 12, 12, 12);
    bodyLayout->addWidget(tabs);
    bodyLayout->addWidget(buttons);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(dialog.titleBar());
    layout->addWidget(body);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString themeAfter = themeCombo->currentData().toString();
    const QString languageAfter = languageCombo->currentData().toString();
    settingsStore.setValue(QStringLiteral("theme"), themeAfter);
    settingsStore.setValue(QStringLiteral("language"), languageAfter);
    if (themeAfter != themeBefore) {
        applyApplicationTheme(*qApp);
        rethemeCaptionIcons();
    }

    rdpbox::ShortcutSettings settings = current;
    settings.newConnection = newConnectionEdit->keySequence();
    settings.openConnections = openConnectionsEdit->keySequence();
    settings.toggleFullScreen = toggleFullScreenEdit->keySequence();
    rdpbox::setCurrentShortcuts(settings);
    applyShortcutSettings();
    QSettings store;
    rdpbox::saveShortcutSettings(rdpbox::currentShortcuts(), store);
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
        return;
    }

    m_updateState = ui::UpdateUiState::Downloaded;
    m_updateDownloadProgress = 100;
    refreshUpdateButton();

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
    if (!m_tabBar)
        return names;

    for (int index = 0; index < m_tabBar->count(); ++index) {
        const std::wstring name = m_tabBar->tabData(index).toString().toStdWString();
        if (!name.empty())
            names.push_back(name);
    }
    return names;
}

std::vector<std::wstring> QtMainWindow::connectedProfileNames() const
{
    std::vector<std::wstring> names;
    if (!m_tabBar)
        return names;

    for (int index = 0; index < m_tabs->count(); ++index) {
        const QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index);
        if (!sessionWidget || !sessionWidget->isConnected())
            continue;

        const QString name = m_tabBar->tabData(index).toString();
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

void QtMainWindow::showTabContextMenu(const QPoint &tabBarPoint)
{
    if (!m_tabBar)
        return;

    const int index = m_tabBar->tabAt(tabBarPoint);
    if (index < 0)
        return;

    const bool hasSession = index >= 0 && sessionWidgetForTab(index);
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

    QAction *selected = menu.exec(m_tabBar->mapToGlobal(tabBarPoint));
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
    if (!m_tabBar)
        return;

    for (int index = 0; index < m_tabs->count(); ++index) {
        QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index);
        if (!sessionWidget)
            continue;

        updateSessionTabState(
            m_tabBar->tabData(index).toString().toStdWString(),
            sessionWidget->state());
    }
}

void QtMainWindow::handleHostResume()
{
    if (!m_tabs)
        return;

    const int activeSessionIndex = m_tabs->currentIndex();
    for (int tabIndex = 0; tabIndex < m_tabs->count(); ++tabIndex) {
        QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(tabIndex);
        if (!sessionWidget)
            continue;

        const bool autoReconnect =
            sessionResumeActionForTab(tabIndex, activeSessionIndex)
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
    const QString title = profileTitle(profile);
    const int index = m_tabs->addTab(page, title);
    m_tabBar->addTab(title);
    m_tabBar->setTabData(index, QString::fromStdWString(profile.name));
    m_tabBar->setCurrentIndex(index);
    m_tabs->tabBar()->setVisible(false);
    updateSessionTabState(profile.name, FreeRdpProcess::State::Idle);
    m_tabs->setCurrentIndex(index);
    refreshProfileList();
}

void QtMainWindow::updateSessionTabState(const std::wstring &profileName, FreeRdpProcess::State state)
{
    m_sessionStates[profileName] = state;
    const int index = sessionTabIndexForProfileName(profileName);
    if (index < 0 || !m_tabs)
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

    m_tabBar->setTabText(index, sessionTabTitle(profile, state));
    m_tabBar->setTabToolTip(index, tooltip);
    m_tabBar->setTabIcon(index, sessionStatusIcon(ui::tabStatusForConnection(connected, uiInfo)));
    refreshProfileList();
}

FreeRdpProcess::State QtMainWindow::sessionStateForProfile(const QString &profileName) const
{
    const auto it = m_sessionStates.find(profileName.toStdWString());
    return it != m_sessionStates.end() ? it->second : FreeRdpProcess::State::Idle;
}

int QtMainWindow::sessionTabIndexForProfileName(const std::wstring &profileName) const
{
    if (!m_tabs || profileName.empty())
        return -1;

    const QString name = QString::fromStdWString(profileName);
    if (!m_tabBar)
        return -1;

    for (int index = 0; index < m_tabBar->count(); ++index) {
        if (QString::compare(m_tabBar->tabData(index).toString(), name, Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

QtRdpSessionWidget *QtMainWindow::sessionWidgetForTab(int index) const
{
    if (!m_tabs || index < 0 || index >= m_tabs->count())
        return nullptr;

    QWidget *page = m_tabs->widget(index);
    return page ? page->findChild<QtRdpSessionWidget *>() : nullptr;
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
    for (QWidget *widget : {static_cast<QWidget *>(m_logoButton),
                            static_cast<QWidget *>(m_updateButton),
                            static_cast<QWidget *>(m_infoButton),
                            static_cast<QWidget *>(m_minimizeButton),
                            static_cast<QWidget *>(m_maximizeButton),
                            static_cast<QWidget *>(m_closeButton),
                            static_cast<QWidget *>(m_addButton)}) {
        if (!widget)
            continue;

        const QPoint topLeft = widget->mapTo(this, QPoint(0, 0));
        rects.push_back(QRect(topLeft, widget->size()));
    }
    // Tabs themselves are interactive (switch/close/reorder); the empty
    // stretches of the tab bar stay draggable caption.
    if (m_tabBar) {
        const QPoint origin = m_tabBar->mapTo(this, QPoint(0, 0));
        for (int i = 0; i < m_tabBar->count(); ++i) {
            const QRect tab = m_tabBar->tabRect(i);
            rects.push_back(QRect(origin + tab.topLeft(), tab.size()));
        }
    }
    return rects;
}
