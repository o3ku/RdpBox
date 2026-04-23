#include "MainWindow.h"
#include "rdp/RdpSessionWidget.h"

#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_sessionWidget(new RdpSessionWidget(this))
{
    setWindowTitle("RdpBox - POC");
    setCentralWidget(m_sessionWidget);
    resize(1280, 800);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
}
