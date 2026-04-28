#pragma once

#include "profiles/Profile.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class BrowserTabBar;
class CWnd;
class CRdpSessionView;
class ProfileRepository;

class SessionManager
{
public:
    using SessionConnectedCallback = std::function<void(const std::string &, const Profile &)>;

    SessionManager(BrowserTabBar *tabBar, CWnd *sessionHost, ProfileRepository *repository);
    ~SessionManager();

    std::string openSession(const Profile &profile);
    void closeSession(const std::string &sessionId);
    void reconnectSession(const std::string &sessionId);
    void closeAllSessions();
    void activateTab(int index);
    void layoutSessions();
    void setResizeSuppressed(bool suppressed);
    void flushPendingResize();
    void setSessionConnectedCallback(SessionConnectedCallback callback);

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
    void touchLastConnectedAt(const std::string &sessionId);

    BrowserTabBar *m_tabBar = nullptr;
    CWnd *m_sessionHost = nullptr;
    ProfileRepository *m_repository = nullptr;
    std::vector<std::unique_ptr<Session>> m_sessions;
    SessionConnectedCallback m_sessionConnectedCallback;
};
