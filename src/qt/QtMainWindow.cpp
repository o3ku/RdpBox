#include "qt/QtMainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/Win32String.h"
#include "qt/QtProfileDialog.h"
#include "qt/QtRdpSessionWidget.h"
#include "qt/QtWindowChromeBehavior.h"
#include "session/SessionResumePolicy.h"
#include "ui/ConnectionListBehavior.h"
#include "ui/MainWindowActivation.h"
#include "ui/MainWindowLayoutBehavior.h"
#include "ui/MainWindowSessionBehavior.h"
#include "ui/MainWindowShortcuts.h"
#include "ui/MainWindowTabBehavior.h"
#include "ui/MainWindowUpdateBehavior.h"
#include "ui/WindowStateScaling.h"

#include <QApplication>
#include <QAction>
#include <QAbstractItemView>
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
#include <QPointer>
#include <QPushButton>
#include <QPixmap>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QToolButton>

#ifdef RDPBOX_USE_QWINDOWKIT
#include <QWKWidgets/widgetwindowagent.h>
#endif

#include <windows.h>

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
constexpr int kUpdateCheckIntervalMs = 24 * 60 * 60 * 1000;

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

std::wstring powerShellSingleQuotedLiteral(const std::wstring &value)
{
    std::wstring quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back(L'\'');
    for (wchar_t ch : value) {
        if (ch == L'\'')
            quoted += L"''";
        else
            quoted.push_back(ch);
    }
    quoted.push_back(L'\'');
    return quoted;
}

bool writeScriptFile(const std::wstring &path, const std::wstring &contents)
{
    return AppPaths::writeFileContent(path, utf8FromWide(contents));
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

class QtConnectionListDialog final : public QDialog
{
public:
    QtConnectionListDialog(ProfileRepository *repository,
                           std::vector<std::wstring> connectedProfileNames,
                           QWidget *parent = nullptr)
        : QDialog(parent),
          m_repository(repository),
          m_connectedProfileNames(std::move(connectedProfileNames))
    {
        setWindowTitle(QObject::tr("Connections"));
        setModal(true);
        resize(620, 420);

        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText(QObject::tr("Search profiles..."));

        m_table = new QTableWidget(this);
        m_table->setColumnCount(4);
        m_table->setHorizontalHeaderLabels({
            QObject::tr("Name"),
            QObject::tr("Host"),
            QObject::tr("Port"),
            QObject::tr("Status"),
        });
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setShowGrid(false);
        m_table->setAlternatingRowColors(false);
        m_table->verticalHeader()->setVisible(false);
        m_table->horizontalHeader()->setStretchLastSection(false);
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_table->setColumnWidth(0, 150);
        m_table->setColumnWidth(1, 180);

        m_newButton = new QPushButton(QObject::tr("New"), this);
        m_editButton = new QPushButton(QObject::tr("Edit"), this);
        m_duplicateButton = new QPushButton(QObject::tr("Duplicate"), this);
        m_deleteButton = new QPushButton(QObject::tr("Delete"), this);
        m_connectButton = new QPushButton(QObject::tr("Connect"), this);
        m_connectButton->setDefault(true);

        auto *buttonLayout = new QHBoxLayout;
        buttonLayout->addWidget(m_newButton);
        buttonLayout->addWidget(m_editButton);
        buttonLayout->addWidget(m_duplicateButton);
        buttonLayout->addWidget(m_deleteButton);
        buttonLayout->addStretch(1);
        buttonLayout->addWidget(m_connectButton);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);
        layout->addWidget(m_searchEdit);
        layout->addWidget(m_table, 1);
        layout->addLayout(buttonLayout);

        connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
            refreshList();
        });
        connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
            updateButtonStates();
        });
        connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) {
            connectSelectedProfiles();
        });
        connect(m_newButton, &QPushButton::clicked, this, [this]() {
            addProfile();
        });
        connect(m_editButton, &QPushButton::clicked, this, [this]() {
            editSelectedProfile();
        });
        connect(m_duplicateButton, &QPushButton::clicked, this, [this]() {
            duplicateSelectedProfiles();
        });
        connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
            deleteSelectedProfiles();
        });
        connect(m_connectButton, &QPushButton::clicked, this, [this]() {
            connectSelectedProfiles();
        });

        refreshList();
    }

    const std::vector<std::wstring> &selectedProfileNames() const
    {
        return m_selectedProfileNames;
    }

protected:
    void accept() override
    {
        connectSelectedProfiles();
    }

private:
    std::vector<Profile> currentVisibleProfiles() const
    {
        if (!m_repository)
            return {};

        const std::wstring query = m_searchEdit ? m_searchEdit->text().toStdWString() : std::wstring();
        return query.empty() ? m_repository->profiles() : m_repository->search(query);
    }

    std::vector<int> selectedRows() const
    {
        std::vector<int> rows;
        if (!m_table)
            return rows;

        const QModelIndexList indexes = m_table->selectionModel()->selectedRows();
        rows.reserve(static_cast<std::size_t>(indexes.size()));
        for (const QModelIndex &index : indexes) {
            if (index.row() >= 0)
                rows.push_back(index.row());
        }
        std::sort(rows.begin(), rows.end());
        return rows;
    }

    void refreshList()
    {
        if (!m_table)
            return;

        const std::vector<std::wstring> previousSelection = selectedProfileNamesForRows(selectedRows());
        m_currentProfiles = currentVisibleProfiles();
        const std::vector<int> retainedRows =
            retainedSelectionRowsForProfiles(m_currentProfiles, previousSelection);

        m_table->setRowCount(static_cast<int>(m_currentProfiles.size()));
        for (int row = 0; row < static_cast<int>(m_currentProfiles.size()); ++row) {
            const Profile &profile = m_currentProfiles[static_cast<std::size_t>(row)];
            setCell(row, 0, QString::fromStdWString(profile.name));
            setCell(row, 1, QString::fromStdWString(profile.host));
            setCell(row, 2, QString::number(profile.port));
            setCell(row, 3, QString::fromStdWString(
                            connectionListStatusText(profile.name, m_connectedProfileNames)));
        }

        if (!retainedRows.empty()) {
            m_table->clearSelection();
            for (int row : retainedRows)
                m_table->selectRow(row);
            m_table->setCurrentCell(retainedRows.front(), 0);
        } else if (!m_currentProfiles.empty() && selectedRows().empty()) {
            m_table->selectRow(0);
            m_table->setCurrentCell(0, 0);
        }
        updateButtonStates();
    }

    void setCell(int row, int column, const QString &text)
    {
        auto *item = new QTableWidgetItem(text);
        item->setData(Qt::UserRole, row);
        m_table->setItem(row, column, item);
    }

    std::vector<std::wstring> selectedProfileNamesForRows(const std::vector<int> &rows) const
    {
        std::vector<std::wstring> names;
        for (int row : rows) {
            if (row >= 0 && row < static_cast<int>(m_currentProfiles.size()))
                names.push_back(m_currentProfiles[static_cast<std::size_t>(row)].name);
        }
        return names;
    }

    Profile currentProfile() const
    {
        const std::vector<int> rows = selectedRows();
        if (rows.size() != 1)
            return {};

        const int row = rows.front();
        if (row < 0 || row >= static_cast<int>(m_currentProfiles.size()))
            return {};

        return m_currentProfiles[static_cast<std::size_t>(row)];
    }

    void updateButtonStates()
    {
        const ConnectionListButtonState state =
            connectionListButtonState(m_currentProfiles, selectedRows(), m_connectedProfileNames);

        if (m_editButton)
            m_editButton->setEnabled(state.editEnabled);
        if (m_duplicateButton)
            m_duplicateButton->setEnabled(state.duplicateEnabled);
        if (m_deleteButton)
            m_deleteButton->setEnabled(state.deleteEnabled);
        if (m_connectButton)
            m_connectButton->setEnabled(state.connectEnabled);
    }

    void addProfile()
    {
        if (!m_repository)
            return;

        QtProfileDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
            return;

        const Profile profile = dialog.profile();
        if (!m_repository->addProfile(profile)) {
            showNameConflictMessage(profile.name);
            return;
        }
        refreshList();
        selectProfileByName(profile.name);
    }

    void editSelectedProfile()
    {
        if (!m_repository)
            return;

        const Profile current = currentProfile();
        if (!current.isValid())
            return;

        QtProfileDialog dialog(this);
        dialog.setProfile(current);
        if (dialog.exec() != QDialog::Accepted)
            return;

        const Profile profile = dialog.profile();
        if (!m_repository->updateProfile(current.name, profile)) {
            showNameConflictMessage(profile.name);
            return;
        }
        refreshList();
        selectProfileByName(profile.name);
    }

    void duplicateSelectedProfiles()
    {
        if (!m_repository)
            return;

        const std::vector<int> rows = selectedRows();
        if (rows.empty())
            return;

        std::vector<std::wstring> duplicatedNames;
        for (int row : rows) {
            if (row < 0 || row >= static_cast<int>(m_currentProfiles.size()))
                continue;

            const Profile duplicate =
                duplicateProfileDraft(m_currentProfiles[static_cast<std::size_t>(row)]);
            if (!m_repository->addProfile(duplicate)) {
                showNameConflictMessage(duplicate.name);
                continue;
            }
            duplicatedNames.push_back(duplicate.name);
        }
        refreshList();
        if (!duplicatedNames.empty())
            selectProfileByName(duplicatedNames.front());
    }

    void deleteSelectedProfiles()
    {
        if (!m_repository)
            return;

        const std::vector<int> rows = selectedRows();
        if (rows.empty())
            return;

        const QString message = rows.size() == 1
            ? QObject::tr("Delete this connection?")
            : QObject::tr("Delete %n connections?", nullptr, static_cast<int>(rows.size()));
        const QMessageBox::StandardButton result = QMessageBox::question(
            this,
            QObject::tr("Delete Connection"),
            message,
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (result != QMessageBox::Yes)
            return;

        for (const std::wstring &name : selectedProfileNamesForRows(rows))
            m_repository->removeProfile(name);
        refreshList();
    }

    void connectSelectedProfiles()
    {
        m_selectedProfileNames =
            connectableProfileNamesForSelection(m_currentProfiles, selectedRows(), m_connectedProfileNames);
        if (m_selectedProfileNames.empty())
            return;

        QDialog::accept();
    }

    void selectProfileByName(const std::wstring &profileName)
    {
        if (profileName.empty())
            return;

        for (int row = 0; row < static_cast<int>(m_currentProfiles.size()); ++row) {
            if (_wcsicmp(m_currentProfiles[static_cast<std::size_t>(row)].name.c_str(),
                         profileName.c_str()) != 0) {
                continue;
            }
            m_table->clearSelection();
            m_table->selectRow(row);
            m_table->scrollToItem(m_table->item(row, 0));
            return;
        }
    }

    void showNameConflictMessage(const std::wstring &name)
    {
        QMessageBox::warning(
            this,
            QObject::tr("Connection"),
            QObject::tr("A connection named \"%1\" already exists.")
                .arg(QString::fromStdWString(name)));
    }

    ProfileRepository *m_repository = nullptr;
    std::vector<std::wstring> m_connectedProfileNames;
    std::vector<std::wstring> m_selectedProfileNames;
    std::vector<Profile> m_currentProfiles;
    QLineEdit *m_searchEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_newButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_connectButton = nullptr;
};
}

QtMainWindow::QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                           QWidget *parent)
    : QMainWindow(parent),
      m_repository(AppPaths::profilesFilePath()),
      m_startupConnectionNames(std::move(startupConnectionNames))
{
#ifdef RDPBOX_USE_QWINDOWKIT
    m_windowAgent = new QWK::WidgetWindowAgent(this);
    m_windowAgent->setup(this);
#else
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_NativeWindow);
#endif
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
    if (object == m_iconLabel && event) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                showLogoMenu();
                return true;
            }
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
            if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(m_tabBar ? m_tabBar->currentIndex() : -1))
                sessionWidget->handleBecameVisible();
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }

    if (msg->message == WM_POWERBROADCAST) {
        const ui::MainWindowPowerBroadcastPlan plan =
            ui::powerBroadcastPlan(static_cast<unsigned int>(msg->wParam),
                                   m_tabBar && m_tabBar->count() > 0);
        if (plan.handleHostResume)
            handleHostResume();
        if (result)
            *result = TRUE;
        return true;
    }

#ifdef RDPBOX_USE_QWINDOWKIT
    return QMainWindow::nativeEvent(eventType, message, result);
#else
    if (msg->message != WM_NCHITTEST)
        return QMainWindow::nativeEvent(eventType, message, result);

    const POINTS screenPoint = MAKEPOINTS(msg->lParam);
    const QPoint windowPoint = mapFromGlobal(QPoint(screenPoint.x, screenPoint.y));
    *result = nativeHitTestForPoint(windowPoint);
    return true;
#endif
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
    configureWindowChrome();

    m_sessionStack = new QStackedWidget(shell);
    m_sessionStack->setObjectName(QStringLiteral("sessionHost"));
    shellLayout->addWidget(m_sessionStack, 1);
    setCentralWidget(shell);

    connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
        closeSessionTab(index);
    });
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (m_sessionStack && index >= 0 && index < m_sessionStack->count())
            m_sessionStack->setCurrentIndex(index);
        if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(index))
            sessionWidget->handleBecameVisible();
    });
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int fromIndex, int toIndex) {
        handleTabMoved(fromIndex, toIndex);
    });
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, [this](const QPoint &point) {
        showTabContextMenu(point);
    });

    refreshActions();
}

void QtMainWindow::buildTitleBar(QVBoxLayout *rootLayout)
{
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("titleBar"));
    m_titleBar->setFixedHeight(ui::kMainWindowCaptionHeight);

    auto *layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_iconLabel = new QLabel(m_titleBar);
    m_iconLabel->setPixmap(windowIcon().pixmap(ui::kMainWindowLogoSize, ui::kMainWindowLogoSize));
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setFixedSize(ui::kMainWindowLogoLeftPadding
                                  + ui::kMainWindowLogoSize
                                  + ui::kMainWindowLogoRightPadding,
                              ui::kMainWindowCaptionHeight);
    m_iconLabel->setCursor(Qt::PointingHandCursor);
    m_iconLabel->setToolTip(tr("RdpBox"));
    m_iconLabel->installEventFilter(this);

    m_tabBar = new QTabBar(m_titleBar);
    m_tabBar->setObjectName(QStringLiteral("captionTabBar"));
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setMovable(true);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setFixedHeight(ui::kMainWindowCaptionHeight);

    m_updateButton = new QToolButton(m_titleBar);
    m_minimizeButton = new QToolButton(m_titleBar);
    m_maximizeButton = new QToolButton(m_titleBar);
    m_closeButton = new QToolButton(m_titleBar);

    m_updateButton->setObjectName(QStringLiteral("captionButton"));
    m_minimizeButton->setObjectName(QStringLiteral("captionButton"));
    m_maximizeButton->setObjectName(QStringLiteral("captionButton"));
    m_closeButton->setObjectName(QStringLiteral("closeCaptionButton"));
    m_updateButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_minimizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    m_maximizeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_updateButton->setToolTip(tr("Check for updates"));
    m_minimizeButton->setToolTip(tr("Minimize"));
    m_maximizeButton->setToolTip(tr("Maximize"));
    m_closeButton->setToolTip(tr("Close"));
    m_updateButton->setAutoRaise(true);
    m_updateButton->setFixedSize(ui::kMainWindowUpdateButtonWidth, ui::kMainWindowCaptionHeight);
    m_updateButton->setFocusPolicy(Qt::NoFocus);
    m_updateButton->setToolButtonStyle(Qt::ToolButtonIconOnly);

    const std::array<QToolButton *, 3> captionButtons = {
        m_minimizeButton,
        m_maximizeButton,
        m_closeButton,
    };
    for (QToolButton *button : captionButtons) {
        button->setAutoRaise(true);
        button->setFixedSize(ui::kMainWindowCaptionButtonWidth, ui::kMainWindowCaptionHeight);
        button->setFocusPolicy(Qt::NoFocus);
    }

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_tabBar, 1);
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

void QtMainWindow::configureWindowChrome()
{
#ifdef RDPBOX_USE_QWINDOWKIT
    if (!m_windowAgent || !m_titleBar)
        return;

    m_windowAgent->setTitleBar(m_titleBar);

    if (m_iconLabel) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::WindowIcon, m_iconLabel);
        m_windowAgent->setHitTestVisible(m_iconLabel, true);
    }
    if (m_tabBar)
        m_windowAgent->setHitTestVisible(m_tabBar, true);
    if (m_updateButton)
        m_windowAgent->setHitTestVisible(m_updateButton, true);
    if (m_minimizeButton) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, m_minimizeButton);
        m_windowAgent->setHitTestVisible(m_minimizeButton, true);
    }
    if (m_maximizeButton) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, m_maximizeButton);
        m_windowAgent->setHitTestVisible(m_maximizeButton, true);
    }
    if (m_closeButton) {
        m_windowAgent->setSystemButton(QWK::WindowAgentBase::Close, m_closeButton);
        m_windowAgent->setHitTestVisible(m_closeButton, true);
    }
#endif
}

void QtMainWindow::installShortcuts()
{
    auto bindShortcut = [this](const QKeySequence &sequence, auto handler) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, handler);
    };

    bindShortcut(QKeySequence(Qt::Key_F11), [this]() {
        toggleFullScreen();
    });
    bindShortcut(QKeySequence(Qt::Key_Escape), [this]() {
        if (m_isFullScreen)
            setFullScreen(false);
    });

    if (ui::shortcutActionForKey(WM_KEYDOWN, true, false, 'N') == ui::MainWindowShortcutAction::NewConnection) {
        bindShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), [this]() {
            if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(m_tabBar ? m_tabBar->currentIndex() : -1))
                sessionWidget->noteConsumedLocalShortcutKey('N');
            addProfile(true);
        });
    }

    if (ui::shortcutActionForKey(WM_KEYDOWN, true, false, 'P') == ui::MainWindowShortcutAction::OpenConnections) {
        bindShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), [this]() {
            if (QtRdpSessionWidget *sessionWidget = sessionWidgetForTab(m_tabBar ? m_tabBar->currentIndex() : -1))
                sessionWidget->noteConsumedLocalShortcutKey('P');
            showConnectionsDialog();
        });
    }
}

void QtMainWindow::refreshProfileList()
{
    if (!m_profileList) {
        refreshActions();
        return;
    }

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

    if (m_statusLabel)
        m_statusLabel->setText(tr("%n connection(s)", nullptr, static_cast<int>(m_repository.profiles().size())));
    refreshActions();
}

void QtMainWindow::refreshActions()
{
    if (!m_profileList || !m_editButton || !m_duplicateButton || !m_deleteButton
        || !m_connectButton || !m_moveUpButton || !m_moveDownButton) {
        return;
    }

    const std::vector<int> selectedRows = selectedProfileRows();
    const ConnectionListButtonState state =
        connectionListButtonState(currentVisibleProfiles(), selectedRows, openProfileNames());
    const int itemCount = m_profileList ? m_profileList->count() : 0;
    const bool canMoveSelection = selectedRows.size() == 1;
    const int selectedRow = canMoveSelection ? selectedRows.front() : -1;

    m_editButton->setEnabled(state.editEnabled);
    m_duplicateButton->setEnabled(state.duplicateEnabled);
    m_deleteButton->setEnabled(state.deleteEnabled);
    m_connectButton->setEnabled(state.connectEnabled);
    m_moveUpButton->setEnabled(canMoveSelection && targetSelectionIndex(selectedRow, itemCount, -1).has_value());
    m_moveDownButton->setEnabled(canMoveSelection && targetSelectionIndex(selectedRow, itemCount, 1).has_value());
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

    m_maximizeButton->setIcon(style()->standardIcon(
        isMaximized() ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
    m_maximizeButton->setToolTip(isMaximized() ? tr("Restore") : tr("Maximize"));
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
        ui::kMainWindowResizeBorderWidth,
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
    const std::vector<Profile> visibleProfiles = currentVisibleProfiles();
    for (int row : selectedRows) {
        if (row < 0 || row >= static_cast<int>(visibleProfiles.size()))
            continue;

        const Profile duplicate = duplicateProfileDraft(visibleProfiles[static_cast<std::size_t>(row)]);
        if (!m_repository.addProfile(duplicate)) {
            QMessageBox::warning(
                this,
                tr("Connection"),
                tr("A connection with this name already exists."));
            continue;
        }
        duplicateNames.push_back(duplicate.name);
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
    if (!m_tabBar || !m_sessionStack || index < 0 || index >= m_tabBar->count()
        || index >= m_sessionStack->count()) {
        return;
    }

    QWidget *page = m_sessionStack->widget(index);
    m_tabBar->removeTab(index);
    m_sessionStack->removeWidget(page);
    if (page)
        page->deleteLater();
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
    if (m_tabBar)
        m_tabBar->setVisible(!enabled);

    if (enabled)
        showFullScreen();
    else if (m_wasMaximizedBeforeFullScreen)
        showMaximized();
    else
        showNormal();

    refreshWindowControls();
}

void QtMainWindow::showConnectionsDialog()
{
    if (m_isFullScreen)
        setFullScreen(false);

    QtConnectionListDialog dialog(&m_repository, connectedProfileNames(), this);
    if (dialog.exec() != QDialog::Accepted) {
        refreshProfileList();
        return;
    }

    openConnectionsByName(dialog.selectedProfileNames());
    refreshProfileList();
}

void QtMainWindow::showLogoMenu()
{
    if (!m_iconLabel)
        return;

    QMenu menu(this);
    QAction *newAction = menu.addAction(tr("New"));
    newAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    QAction *connectionsAction = menu.addAction(tr("Connections"));
    connectionsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    menu.addSeparator();
    QAction *aboutAction = menu.addAction(tr("About"));

    const QPoint menuPoint = m_iconLabel->mapToGlobal(QPoint(0, m_iconLabel->height()));
    QAction *selected = menu.exec(menuPoint);
    if (!selected)
        return;

    if (selected == newAction) {
        addProfile(true);
    } else if (selected == connectionsAction) {
        showConnectionsDialog();
    } else if (selected == aboutAction) {
        showAboutDialog();
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

    for (int index = 0; index < m_tabBar->count(); ++index) {
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
    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        tr("Update Downloaded"),
        QString::fromStdWString(ui::downloadedUpdatePrompt(m_updateRelease.tagName)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (result != QMessageBox::Yes)
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

    const std::wstring connectionsArg = launch::buildConnectionsArgumentValue(openProfileNames());

    std::wstring params;
    if (AppPaths::isPortableMode())
        params = L"--portable";
    if (!connectionsArg.empty()) {
        if (!params.empty())
            params += L" ";
        params += L"--connections=\"";
        params += connectionsArg;
        params += L"\"";
    }

    const std::wstring updatesDir = AppPaths::updatesDirectoryPath();
    if (updatesDir.empty())
        return false;

    const std::wstring backupExePath = currentExePath + L".bak";
    const std::wstring scriptPath = updatesDir + L"\\apply-update-"
        + std::to_wstring(::GetCurrentProcessId()) + L".ps1";
    const std::wstring logPath = updatesDir + L"\\update-apply.log";

    std::wstring script;
    script += L"$ErrorActionPreference = 'Stop'\n";
    script += L"$pidToWait = " + std::to_wstring(::GetCurrentProcessId()) + L"\n";
    script += L"$src = " + powerShellSingleQuotedLiteral(downloadedPath) + L"\n";
    script += L"$dst = " + powerShellSingleQuotedLiteral(currentExePath) + L"\n";
    script += L"$bak = " + powerShellSingleQuotedLiteral(backupExePath) + L"\n";
    script += L"$argsLine = " + powerShellSingleQuotedLiteral(params) + L"\n";
    script += L"$log = " + powerShellSingleQuotedLiteral(logPath) + L"\n";
    script += L"function Log($message) { Add-Content -Path $log -Value $message }\n";
    script += L"Log \"==== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') ====\"\n";
    script += L"Log \"PID=$pidToWait\"\n";
    script += L"Log \"SRC=$src\"\n";
    script += L"Log \"DST=$dst\"\n";
    script += L"try {\n";
    script += L"  try {\n";
    script += L"    Wait-Process -Id $pidToWait -ErrorAction Stop\n";
    script += L"    Log 'old process exited'\n";
    script += L"  } catch {\n";
    script += L"    Log 'old process already exited'\n";
    script += L"  }\n";
    script += L"  for ($i = 0; $i -lt 20; $i++) {\n";
    script += L"    try {\n";
    script += L"      if (Test-Path $bak) { Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue }\n";
    script += L"      if (Test-Path $dst) { Move-Item -LiteralPath $dst -Destination $bak -Force }\n";
    script += L"      Copy-Item -LiteralPath $src -Destination $dst -Force\n";
    script += L"      if (Test-Path $dst) {\n";
    script += L"        Log 'replacement complete'\n";
    script += L"        break\n";
    script += L"      }\n";
    script += L"    } catch {\n";
    script += L"      Log \"replace retry $i : $($_.Exception.Message)\"\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"      continue\n";
    script += L"    }\n";
    script += L"  }\n";
    script += L"  if (-not (Test-Path $dst)) { throw 'replacement failed' }\n";
    script += L"  Remove-Item -LiteralPath $src -Force -ErrorAction SilentlyContinue\n";
    script += L"  for ($i = 0; $i -lt 5; $i++) {\n";
    script += L"    try {\n";
    script += L"      if ([string]::IsNullOrWhiteSpace($argsLine)) {\n";
    script += L"        Log \"launching without args try $i\"\n";
    script += L"        Start-Process -FilePath $dst\n";
    script += L"      } else {\n";
    script += L"        Log \"launching with args try $i : $argsLine\"\n";
    script += L"        Start-Process -FilePath $dst -ArgumentList $argsLine\n";
    script += L"      }\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"      $p = Get-Process -Name 'RdpBox' -ErrorAction SilentlyContinue\n";
    script += L"      if ($p) {\n";
    script += L"        Log 'launch success'\n";
    script += L"        break\n";
    script += L"      }\n";
    script += L"      Log 'launch retry'\n";
    script += L"    } catch {\n";
    script += L"      Log \"launch error $i : $($_.Exception.Message)\"\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"    }\n";
    script += L"  }\n";
    script += L"} catch {\n";
    script += L"  Log \"script error: $($_.Exception.Message)\"\n";
    script += L"} finally {\n";
    script += L"  Log 'script end'\n";
    script += L"  try {\n";
    script += L"    $scriptPath = $PSCommandPath\n";
    script += L"    Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', \"Start-Sleep -Seconds 2; Remove-Item -LiteralPath '$scriptPath' -Force -ErrorAction SilentlyContinue\")\n";
    script += L"  } catch {\n";
    script += L"  }\n";
    script += L"}\n";

    if (!writeScriptFile(scriptPath, script))
        return false;

    std::wstring commandLine =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "
        + powerShellSingleQuotedLiteral(scriptPath);
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    BOOL created = ::CreateProcessW(nullptr,
                                    commandLine.data(),
                                    nullptr,
                                    nullptr,
                                    FALSE,
                                    CREATE_NO_WINDOW,
                                    nullptr,
                                    nullptr,
                                    &startupInfo,
                                    &processInfo);
    if (!created)
        return false;

    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);
    return true;
}

void QtMainWindow::handleTabMoved(int fromIndex, int toIndex)
{
    if (m_adjustingTabMove || !m_sessionStack || fromIndex == toIndex)
        return;

    QWidget *page = m_sessionStack->widget(fromIndex);
    if (!page)
        return;

    m_adjustingTabMove = true;
    m_sessionStack->removeWidget(page);
    m_sessionStack->insertWidget(toIndex, page);
    m_sessionStack->setCurrentIndex(toIndex);
    m_adjustingTabMove = false;
}

void QtMainWindow::showTabContextMenu(const QPoint &tabBarPoint)
{
    if (!m_tabBar)
        return;

    const int index = m_tabBar->tabAt(tabBarPoint);
    if (index < 0)
        return;

    const bool hasSession = sessionWidgetForTab(index);
    const ui::TabContextMenuState state =
        ui::tabContextMenuState(index, m_tabBar->currentIndex(), hasSession, m_isFullScreen);

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

    for (int index = 0; index < m_tabBar->count(); ++index) {
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
    if (!m_tabBar)
        return;

    const int activeSessionIndex = m_tabBar->currentIndex();
    for (int tabIndex = 0; tabIndex < m_tabBar->count(); ++tabIndex) {
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
        if (m_tabBar)
            m_tabBar->setCurrentIndex(existingIndex);
        return;
    }

    QWidget *page = createSessionPage(profile);
    const int pageIndex = m_sessionStack ? m_sessionStack->addWidget(page) : -1;
    const int index = m_tabBar ? m_tabBar->addTab(profileTitle(profile)) : -1;
    if (index >= 0)
        m_tabBar->setTabData(index, QString::fromStdWString(profile.name));
    if (pageIndex != index && m_sessionStack && pageIndex >= 0) {
        m_sessionStack->removeWidget(page);
        m_sessionStack->insertWidget(index, page);
    }
    updateSessionTabState(profile.name, FreeRdpProcess::State::Idle);
    if (m_tabBar && index >= 0)
        m_tabBar->setCurrentIndex(index);
    refreshProfileList();
}

void QtMainWindow::updateSessionTabState(const std::wstring &profileName, FreeRdpProcess::State state)
{
    const int index = sessionTabIndexForProfileName(profileName);
    if (index < 0 || !m_tabBar)
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
}

int QtMainWindow::sessionTabIndexForProfileName(const std::wstring &profileName) const
{
    if (!m_tabBar || profileName.empty())
        return -1;

    const QString name = QString::fromStdWString(profileName);
    for (int index = 0; index < m_tabBar->count(); ++index) {
        if (QString::compare(m_tabBar->tabData(index).toString(), name, Qt::CaseInsensitive) == 0)
            return index;
    }
    return -1;
}

QtRdpSessionWidget *QtMainWindow::sessionWidgetForTab(int index) const
{
    if (!m_sessionStack || index < 0 || index >= m_sessionStack->count())
        return nullptr;

    QWidget *page = m_sessionStack->widget(index);
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
    for (QWidget *widget : {static_cast<QWidget *>(m_iconLabel),
                            static_cast<QWidget *>(m_tabBar),
                            static_cast<QWidget *>(m_updateButton),
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
