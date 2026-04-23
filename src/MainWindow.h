#pragma once

#include <QMainWindow>

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

private:
    void setupToolbar();

    QTabWidget *m_tabWidget;
    SessionManager *m_sessionManager;
    ProfileRepository *m_profileRepo;
};
