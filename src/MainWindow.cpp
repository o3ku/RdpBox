#include "MainWindow.h"
#include "rdp/RdpSessionWidget.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_sessionWidget(new RdpSessionWidget(this))
{
    setWindowTitle("RdpBox - POC");
    setCentralWidget(m_sessionWidget);
    resize(1280, 800);

    // --- Hardcoded connection parameters (change these for your environment) ---
    const QString host = "127.0.0.1";
    const int port = 3389;
    const QString username = "administrator";
    const QString password = "";
    // ---------------------------------------------------------------------------

    const QString exePath = QCoreApplication::applicationDirPath()
                            + "/../../tools/wfreerdp.exe";
    m_sessionWidget->connectToHost(exePath, host, port, username, password);

    connect(m_sessionWidget, &RdpSessionWidget::titleStateChanged,
            this, &MainWindow::updateTitle);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}

void MainWindow::updateTitle(FreeRdpProcess::State state)
{
    QString suffix;
    switch (state) {
    case FreeRdpProcess::State::Idle:     suffix = "Idle"; break;
    case FreeRdpProcess::State::Starting: suffix = "Connecting..."; break;
    case FreeRdpProcess::State::Running:  suffix = "Connected"; break;
    case FreeRdpProcess::State::Finished: suffix = "Disconnected"; break;
    }
    setWindowTitle("RdpBox - " + suffix);
}
