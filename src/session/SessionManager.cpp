#include <afxwin.h>

#include "SessionManager.h"

#include "common/Win32String.h"
#include "profiles/ProfileRepository.h"
#include "rdp/RdpSessionView.h"
#include "ui/BrowserTabBar.h"

#include <algorithm>
#include <string>

SessionManager::SessionManager(BrowserTabBar *tabBar, CWnd *sessionHost, ProfileRepository *repository)
    : m_tabBar(tabBar)
    , m_sessionHost(sessionHost)
    , m_repository(repository)
{
}

SessionManager::~SessionManager()
{
    closeAllSessions();
}

std::string SessionManager::openSession(const Profile &profile)
{
    if (!m_tabBar || !m_sessionHost)
        return {};

    auto session = std::make_unique<Session>();
    session->id = createGuidString();
    session->profile = profile;
    session->view = std::make_unique<CRdpSessionView>();

    CRect hostRect;
    m_sessionHost->GetClientRect(&hostRect);
    if (!session->view->create(m_sessionHost, hostRect))
        return {};

    const std::string sessionId = session->id;
    session->view->setReconnectRequestedCallback([this, sessionId]() {
        reconnectSession(sessionId);
    });
    session->view->setConnectedCallback([this, sessionId]() {
        touchLastConnectedAt(sessionId);
        if (m_sessionConnectedCallback) {
            const int index = indexOfSession(sessionId);
            if (index >= 0)
                m_sessionConnectedCallback(sessionId, m_sessions[static_cast<size_t>(index)]->profile);
        }
    });
    session->view->connectToHost(profile);

    const std::wstring title = profile.name.empty() ? L"(unnamed)" : profile.name;
    const int index = m_tabBar->insertTab(title);
    if (index < 0) {
        session->view->DestroyWindow();
        return {};
    }

    m_sessions.push_back(std::move(session));
    m_tabBar->setSelectedIndex(index);
    showSessionAtIndex(index);
    return m_sessions[static_cast<size_t>(index)]->id;
}

void SessionManager::closeSession(const std::string &sessionId)
{
    const int index = indexOfSession(sessionId);
    if (index < 0 || !m_tabBar)
        return;

    m_sessions[static_cast<size_t>(index)]->view->DestroyWindow();
    m_sessions.erase(m_sessions.begin() + index);
    m_tabBar->removeTab(index);

    if (m_sessions.empty())
        return;

    const int nextIndex = std::min(index, static_cast<int>(m_sessions.size()) - 1);
    m_tabBar->setSelectedIndex(nextIndex);
    showSessionAtIndex(nextIndex);
}

void SessionManager::reconnectSession(const std::string &sessionId)
{
    const int index = indexOfSession(sessionId);
    if (index < 0)
        return;

    m_sessions[static_cast<size_t>(index)]->view->reconnect();
}

void SessionManager::closeAllSessions()
{
    for (auto &session : m_sessions) {
        if (session->view)
            session->view->DestroyWindow();
    }
    m_sessions.clear();

    if (m_tabBar)
        m_tabBar->clearTabs();
}

void SessionManager::activateTab(int index)
{
    showSessionAtIndex(index);
}

void SessionManager::layoutSessions()
{
    if (!m_sessionHost)
        return;

    CRect rect;
    m_sessionHost->GetClientRect(&rect);
    for (auto &session : m_sessions) {
        if (session->view && session->view->GetSafeHwnd())
            session->view->MoveWindow(rect);
    }
}

void SessionManager::setResizeSuppressed(bool suppressed)
{
    for (auto &session : m_sessions) {
        if (session->view && session->view->GetSafeHwnd())
            session->view->setResizeSuppressed(suppressed);
    }
}

void SessionManager::flushPendingResize()
{
    for (auto &session : m_sessions) {
        if (session->view && session->view->GetSafeHwnd())
            session->view->flushPendingResize();
    }
}

std::string SessionManager::sessionIdByTabIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_sessions.size()))
        return {};

    return m_sessions[static_cast<size_t>(index)]->id;
}

bool SessionManager::hasOpenSessions() const
{
    return !m_sessions.empty();
}

int SessionManager::indexOfSession(const std::string &sessionId) const
{
    for (size_t index = 0; index < m_sessions.size(); ++index) {
        if (m_sessions[index]->id == sessionId)
            return static_cast<int>(index);
    }
    return -1;
}

void SessionManager::showSessionAtIndex(int index)
{
    for (size_t currentIndex = 0; currentIndex < m_sessions.size(); ++currentIndex) {
        if (!m_sessions[currentIndex]->view || !m_sessions[currentIndex]->view->GetSafeHwnd())
            continue;

        m_sessions[currentIndex]->view->ShowWindow(static_cast<int>(currentIndex) == index ? SW_SHOW : SW_HIDE);
    }
}

void SessionManager::touchLastConnectedAt(const std::string &sessionId)
{
    if (!m_repository)
        return;

    const int index = indexOfSession(sessionId);
    if (index < 0)
        return;

    Profile &profile = m_sessions[static_cast<size_t>(index)]->profile;
    profile.lastConnectedAt = currentUtcIso8601();
    m_repository->updateProfile(profile);
}

void SessionManager::setSessionConnectedCallback(SessionConnectedCallback callback)
{
    m_sessionConnectedCallback = std::move(callback);
}
