#pragma once

#include <QObject>
#include <QMap>
#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"

class RdpSessionWidget;
class QTabWidget;

class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(QTabWidget *tabWidget, QObject *parent = nullptr);

    void setExePath(const QString &path);

    QString openSession(const Profile &profile);
    void closeSession(const QString &sessionId);
    void reconnectSession(const QString &sessionId);
    void closeAllSessions();

    QString sessionIdByWidget(QWidget *widget) const;

signals:
    void sessionTitleChanged(const QString &sessionId, const QString &title);

private:
    void onSessionStateChanged(const QString &sessionId, FreeRdpProcess::State state);

    struct Session {
        QString id;
        Profile profile;
        RdpSessionWidget *widget = nullptr;
    };

    QTabWidget *m_tabWidget;
    QString m_exePath;
    QMap<QString, Session> m_sessions;
};
