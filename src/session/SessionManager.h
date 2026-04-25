#pragma once

#include "profiles/Profile.h"

#include <memory>
#include <vector>

class CTabCtrl;
class CWnd;
class CRdpSessionView;

class SessionManager
{
public:
    SessionManager(CTabCtrl *tabCtrl, CWnd *sessionHost);
    ~SessionManager();

    QString openSession(const Profile &profile);
    void closeSession(const QString &sessionId);
    void reconnectSession(const QString &sessionId);
    void closeAllSessions();
    void activateTab(int index);
    void layoutSessions();

    QString sessionIdByTabIndex(int index) const;
    bool hasOpenSessions() const;

private:
    struct Session
    {
        QString id;
        Profile profile;
        std::unique_ptr<CRdpSessionView> view;
    };

    int indexOfSession(const QString &sessionId) const;
    void showSessionAtIndex(int index);

    CTabCtrl *m_tabCtrl = nullptr;
    CWnd *m_sessionHost = nullptr;
    std::vector<std::unique_ptr<Session>> m_sessions;
};
