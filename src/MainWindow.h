#pragma once

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QToolButton;
class QTabWidget;
class SessionManager;
class ProfileRepository;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onNewConnection();
    void onOpenConnection();
    void onTabCloseRequested(int index);
    void onTabContextMenuRequested(const QPoint &pos);

private:
    void closeEvent(QCloseEvent *event) override;

    void setupActions();
    void setupTabWidget();
    void setupTabActions();
    void restoreWindowState();
    void saveWindowState() const;
    QString sessionIdByTabIndex(int index) const;

    QTabWidget *m_tabWidget;
    SessionManager *m_sessionManager;
    ProfileRepository *m_profileRepo;
    QAction *m_newAction;
    QAction *m_connectionsAction;
    QAction *m_reconnectAction;
    QToolButton *m_newButton;
    QToolButton *m_connectionsButton;
};
