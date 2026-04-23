#pragma once

#include <QProcess>
#include <QtGlobal>
#include <QtGui/qwindowdefs.h>

class FreeRdpProcess : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Starting, Running, Finished };

    explicit FreeRdpProcess(QObject *parent = nullptr);
    ~FreeRdpProcess();

    void start(const QString &exePath,
               const QString &host,
               int port,
               const QString &username,
               const QString &password,
               WId parentHwnd,
               bool clipboardEnabled = true,
               bool ignoreCertificate = true);
    void stop();

    State state() const { return m_state; }

signals:
    void stateChanged(FreeRdpProcess::State newState);

private:
    void setState(State newState);

    QProcess *m_process = nullptr;
    State m_state = State::Idle;
};
