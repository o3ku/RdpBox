#pragma once

#include <QMainWindow>

class RdpSessionWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    RdpSessionWidget *m_sessionWidget;
};
