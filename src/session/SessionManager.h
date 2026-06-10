#pragma once

#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"
#include "session/SessionCollectionBehavior.h"
#include "session/SessionManagerInterfaces.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class BrowserTabBar;
class CWnd;
class ProfileRepository;

class SessionManager
{
public:
    using SessionConnectedCallback = std::function<void(const std::string &, const Profile &)>;

    SessionManager(BrowserTabBar *tabBar, CWnd *sessionHost, ProfileRepository *repository);
    SessionManager(ISessionTabs *tabs,
                   ISessionHost *host,
                   ISessionViewFactory *viewFactory,
                   ProfileRepository *repository = nullptr);
    ~SessionManager();

    std::string openSession(const Profile &profile);
    void closeSession(const std::string &sessionId);
    void reconnectSession(const std::string &sessionId);
    void closeAllSessions();
    void activateTab(int index);
    void focusActiveSession();
    void layoutSessions();
    void setResizeSuppressed(bool suppressed);
    void flushPendingResize();
    void handleHostResume();
    void setSessionConnectedCallback(SessionConnectedCallback callback);
    bool moveSession(int fromIndex, int toIndex);

    std::string sessionIdByTabIndex(int index) const;
    bool hasOpenSessions() const;

    FreeRdpProcess::ConnectionInfo connectionInfoForTab(int index) const;
    bool isTabConnected(int index) const;
    std::vector<std::wstring> connectedProfileNames() const;
    std::vector<std::wstring> openProfileNames() const;

private:
    struct Session
    {
        std::string id;
        std::wstring profileName;
        std::unique_ptr<ISessionView> view;
    };

    int indexOfSession(const std::string &sessionId) const;
    std::vector<SessionSnapshot> snapshots() const;
    void showSessionAtIndex(int index);
    void touchLastConnectedAt(const std::string &sessionId);

    std::unique_ptr<ISessionTabs> m_ownedTabs;
    std::unique_ptr<ISessionHost> m_ownedHost;
    std::unique_ptr<ISessionViewFactory> m_ownedViewFactory;
    ISessionTabs *m_tabs = nullptr;
    ISessionHost *m_host = nullptr;
    ISessionViewFactory *m_viewFactory = nullptr;
    ProfileRepository *m_repository = nullptr;
    std::vector<Session> m_sessions;
    SessionConnectedCallback m_sessionConnectedCallback;
};
