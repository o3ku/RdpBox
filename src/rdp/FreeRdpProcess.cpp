#include "FreeRdpProcess.h"

FreeRdpProcess::FreeRdpProcess(QObject *parent)
    : QObject(parent)
{
}

FreeRdpProcess::~FreeRdpProcess()
{
    stop();
}

void FreeRdpProcess::start(const QString &exePath,
                            const QString &host,
                            int port,
                            const QString &username,
                            const QString &password,
                            WId parentHwnd,
                            int width,
                            int height,
                            bool clipboardEnabled,
                            bool ignoreCertificate)
{
    if (m_state == State::Running || m_state == State::Starting)
        return;

    m_process = new QProcess(this);

    connect(m_process, &QProcess::started, this, [this]() {
        setState(State::Running);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        Q_UNUSED(exitCode);
        setState(State::Finished);
        m_process->deleteLater();
        m_process = nullptr;
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setState(State::Finished);
        m_process->deleteLater();
        m_process = nullptr;
    });

    QStringList args;
    args << QStringLiteral("/v:%1:%2").arg(host).arg(port);
    args << QStringLiteral("/u:%1").arg(username);
    args << QStringLiteral("/p:%1").arg(password);
    args << QStringLiteral("/parent-window:%1").arg(static_cast<qlonglong>(parentHwnd));
    if (width > 0 && height > 0) {
        args << QStringLiteral("/w:%1").arg(width);
        args << QStringLiteral("/h:%1").arg(height);
    }
    if (clipboardEnabled)
        args << QStringLiteral("+clipboard");
    if (ignoreCertificate)
        args << QStringLiteral("/cert:ignore");

    setState(State::Starting);
    m_process->start(exePath, args);
}

void FreeRdpProcess::stop()
{
    if (m_process) {
        m_process->disconnect();
        m_process->kill();
        m_process->waitForFinished(3000);
        delete m_process;
        m_process = nullptr;
        setState(State::Finished);
    }
}

void FreeRdpProcess::setState(State newState)
{
    if (m_state == newState)
        return;
    m_state = newState;
    emit stateChanged(newState);
}
