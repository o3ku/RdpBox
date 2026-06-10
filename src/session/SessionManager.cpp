#include "SessionManager.h"

#include "common/Win32String.h"
#include "profiles/ProfileRepository.h"
#include "session/SessionCollectionBehavior.h"
#include "session/SessionResumePolicy.h"
#include "session/SessionTabBehavior.h"

#include <string>
#include <utility>

SessionManager::SessionManager(ISessionTabs *tabs,
                               ISessionHost *host,
                               ISessionViewFactory *viewFactory,
                               ProfileRepository *repository)
    : m_tabs(tabs)
    , m_host(host)
    , m_viewFactory(viewFactory)
    , m_repository(repository)
{
}

SessionManager::~SessionManager()
{
    closeAllSessions();
}

std::string SessionManager::openSession(const Profile &profile)
{
    if (!m_tabs || !m_host || !m_viewFactory)
        return {};

    Session session;
    session.id = createGuidString();
    session.profileName = profile.name;
    session.view = m_viewFactory->createView(m_host->clientBounds());
    if (!session.view)
        return {};

    const std::string sessionId = session.id;
    session.view->setReconnectRequestedCallback([this, sessionId]() {
        reconnectSession(sessionId);
    });
    session.view->setConnectedCallback([this, sessionId, profile]() {
        touchLastConnectedAt(sessionId);
        if (m_sessionConnectedCallback) {
            const int index = indexOfSession(sessionId);
            if (index >= 0)
                m_sessionConnectedCallback(sessionId, profile);
        }
    });

    const std::wstring title = profile.name.empty() ? L"(unnamed)" : profile.name;
    const int index = m_tabs->insertTab(title);
    if (index < 0) {
        session.view->destroy();
        return {};
    }

    m_sessions.push_back(std::move(session));
    m_tabs->setSelectedIndex(index);
    showSessionAtIndex(index);
    m_sessions[static_cast<size_t>(index)].view->connectToHost(profile);
    return m_sessions[static_cast<size_t>(index)].id;
}

void SessionManager::closeSession(const std::string &sessionId)
{
    const int index = indexOfSession(sessionId);
    if (index < 0 || !m_tabs)
        return;

    m_sessions[static_cast<size_t>(index)].view->destroy();
    m_sessions.erase(m_sessions.begin() + index);
    m_tabs->removeTab(index);

    if (m_sessions.empty())
        return;

    const std::optional<int> nextIndex =
        selectedTabAfterClose(static_cast<int>(m_sessions.size()) + 1, index);
    if (!nextIndex.has_value())
        return;

    m_tabs->setSelectedIndex(*nextIndex);
    showSessionAtIndex(*nextIndex);
}

void SessionManager::reconnectSession(const std::string &sessionId)
{
    const int index = indexOfSession(sessionId);
    if (index < 0)
        return;

    m_sessions[static_cast<size_t>(index)].view->reconnect();
}

void SessionManager::closeAllSessions()
{
    for (auto &session : m_sessions) {
        if (session.view)
            session.view->destroy();
    }
    m_sessions.clear();

    if (m_tabs)
        m_tabs->clearTabs();
}

void SessionManager::activateTab(int index)
{
    showSessionAtIndex(index);
}

void SessionManager::focusActiveSession()
{
    if (!m_tabs)
        return;

    showSessionAtIndex(m_tabs->selectedIndex());
}

void SessionManager::layoutSessions()
{
    if (!m_host)
        return;

    const SessionViewBounds bounds = m_host->clientBounds();
    for (auto &session : m_sessions) {
        if (session.view && session.view->isCreated()) {
            session.view->setBounds(bounds);
            session.view->redraw();
        }
    }
}

void SessionManager::setResizeSuppressed(bool suppressed)
{
    for (auto &session : m_sessions) {
        if (session.view && session.view->isCreated())
            session.view->setResizeSuppressed(suppressed);
    }
}

void SessionManager::flushPendingResize()
{
    for (auto &session : m_sessions) {
        if (session.view && session.view->isCreated())
            session.view->flushPendingResize();
    }
}

void SessionManager::handleHostResume()
{
    const int activeTabIndex = m_tabs ? m_tabs->selectedIndex() : -1;
    for (size_t index = 0; index < m_sessions.size(); ++index) {
        auto &session = m_sessions[index];
        if (session.view && session.view->isCreated())
            session.view->handleHostResume(
                sessionResumeActionForTab(static_cast<int>(index), activeTabIndex)
                == SessionResumeAction::AutoReconnect);
    }
}

std::string SessionManager::sessionIdByTabIndex(int index) const
{
    return sessionIdAtTabIndex(snapshots(), index);
}

bool SessionManager::hasOpenSessions() const
{
    return !m_sessions.empty();
}

FreeRdpProcess::ConnectionInfo SessionManager::connectionInfoForTab(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_sessions.size()))
        return {};

    auto &session = m_sessions[static_cast<size_t>(index)];
    if (session.view)
        return session.view->connectionInfo();
    return {};
}

bool SessionManager::isTabConnected(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_sessions.size()))
        return false;
    return m_sessions[static_cast<size_t>(index)].view
        && m_sessions[static_cast<size_t>(index)].view->isConnected();
}

std::vector<std::wstring> SessionManager::connectedProfileNames() const
{
    return connectedProfileNamesForSessions(snapshots());
}

std::vector<std::wstring> SessionManager::openProfileNames() const
{
    return openProfileNamesForSessions(snapshots());
}

int SessionManager::indexOfSession(const std::string &sessionId) const
{
    for (size_t index = 0; index < m_sessions.size(); ++index) {
        if (m_sessions[index].id == sessionId)
            return static_cast<int>(index);
    }
    return -1;
}

void SessionManager::showSessionAtIndex(int index)
{
    ISessionView *activeView = nullptr;
    for (size_t currentIndex = 0; currentIndex < m_sessions.size(); ++currentIndex) {
        if (!m_sessions[currentIndex].view || !m_sessions[currentIndex].view->isCreated())
            continue;

        const bool active = static_cast<int>(currentIndex) == index;
        m_sessions[currentIndex].view->show(active);
        if (active)
            activeView = m_sessions[currentIndex].view.get();
    }

    if (activeView && activeView->isCreated()) {
        activeView->handleBecameVisible();
        activeView->focus();
    }
}

void SessionManager::touchLastConnectedAt(const std::string &sessionId)
{
    if (!m_repository)
        return;

    const int index = indexOfSession(sessionId);
    if (index < 0)
        return;

    Profile profile = m_repository->profileByName(m_sessions[static_cast<size_t>(index)].profileName);
    if (!profile.isValid())
        return;

    profile.lastConnectedAt = currentUtcIso8601();
    m_repository->updateProfile(profile.name, profile);
}

void SessionManager::setSessionConnectedCallback(SessionConnectedCallback callback)
{
    m_sessionConnectedCallback = std::move(callback);
}

bool SessionManager::moveSession(int fromIndex, int toIndex)
{
    if (!m_tabs)
        return false;
    if (!canMoveSessionTab(static_cast<int>(m_sessions.size()), fromIndex, toIndex))
        return false;

    Session moved = std::move(m_sessions[static_cast<size_t>(fromIndex)]);
    m_sessions.erase(m_sessions.begin() + fromIndex);
    m_sessions.insert(m_sessions.begin() + toIndex, std::move(moved));
    showSessionAtIndex(m_tabs->selectedIndex());
    return true;
}

std::vector<SessionSnapshot> SessionManager::snapshots() const
{
    std::vector<SessionSnapshot> result;
    result.reserve(m_sessions.size());
    for (const auto &session : m_sessions) {
        result.push_back(SessionSnapshot{
            session.id,
            session.profileName,
            session.view && session.view->isConnected(),
        });
    }
    return result;
}
