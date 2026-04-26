#pragma once

#include "profiles/Profile.h"

#include <memory>
#include <string>
#include <vector>

class CTabCtrl;
class CWnd;
class CRdpSessionView;

class SessionManager
{
public:
    SessionManager(CTabCtrl *tabCtrl, CWnd *sessionHost);
    ~SessionManager();

    std::string openSession(const Profile &profile);
    void closeSession(const std::string &sessionId);
    void reconnectSession(const std::string &sessionId);
    void closeAllSessions();
    void activateTab(int index);
    void layoutSessions();
    void setResizeSuppressed(bool suppressed);
    void flushPendingResize();

    std::string sessionIdByTabIndex(int index) const;
    bool hasOpenSessions() const;

private:
    struct Session
    {
        std::string id;
        Profile profile;
        std::unique_ptr<CRdpSessionView> view;
    };

    int indexOfSession(const std::string &sessionId) const;
    void showSessionAtIndex(int index);

    CTabCtrl *m_tabCtrl = nullptr;
    CWnd *m_sessionHost = nullptr;
    std::vector<std::unique_ptr<Session>> m_sessions;
};
