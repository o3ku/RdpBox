#pragma once

#include <QMainWindow>
#include "rdp/FreeRdpProcess.h"

class RdpSessionWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateTitle(FreeRdpProcess::State state);

    RdpSessionWidget *m_sessionWidget;
};
