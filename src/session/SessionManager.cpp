#include "SessionManager.h"
#include "rdp/RdpSessionWidget.h"

#include <QTabWidget>
#include <QUuid>

SessionManager::SessionManager(QTabWidget *tabWidget, QObject *parent)
    : QObject(parent)
    , m_tabWidget(tabWidget)
{
}

QString SessionManager::openSession(const Profile &profile)
{
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    auto *widget = new RdpSessionWidget();
    Session session{sessionId, profile, widget};
    m_sessions.insert(sessionId, session);

    int index = m_tabWidget->addTab(widget, profile.name);
    m_tabWidget->setCurrentIndex(index);

    connect(widget, &RdpSessionWidget::titleStateChanged,
            this, [this, sessionId](FreeRdpProcess::State state) {
        onSessionStateChanged(sessionId, state);
    });
    connect(widget, &RdpSessionWidget::reconnectRequested,
            this, [this, sessionId]() {
        reconnectSession(sessionId);
    });

    widget->connectToHost(profile.host, profile.port,
                          profile.username, profile.password,
                          profile.clipboardEnabled, profile.ignoreCertificate);

    return sessionId;
}

void SessionManager::closeSession(const QString &sessionId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return;

    int index = m_tabWidget->indexOf(it->widget);
    if (index >= 0)
        m_tabWidget->removeTab(index);

    it->widget->deleteLater();
    m_sessions.erase(it);
}

void SessionManager::reconnectSession(const QString &sessionId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return;

    it->widget->connectToHost(it->profile.host, it->profile.port,
                              it->profile.username, it->profile.password,
                              it->profile.clipboardEnabled, it->profile.ignoreCertificate);
}

void SessionManager::closeAllSessions()
{
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        int index = m_tabWidget->indexOf(it->widget);
        if (index >= 0)
            m_tabWidget->removeTab(index);
        it->widget->deleteLater();
    }
    m_sessions.clear();
}

QString SessionManager::sessionIdByWidget(QWidget *widget) const
{
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (it->widget == widget)
            return it.key();
    }
    return {};
}

void SessionManager::onSessionStateChanged(const QString &sessionId,
                                             FreeRdpProcess::State state)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return;

    Q_UNUSED(state);

    const QString title = it->profile.name;

    int index = m_tabWidget->indexOf(it->widget);
    if (index >= 0)
        m_tabWidget->setTabText(index, title);

    emit sessionTitleChanged(sessionId, title);
}
