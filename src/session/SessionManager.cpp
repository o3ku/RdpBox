#include "SessionManager.h"

#include "rdp/RdpSessionView.h"

#include <afxcmn.h>
#include <afxwin.h>

#include <QUuid>
#include <string>

SessionManager::SessionManager(CTabCtrl *tabCtrl, CWnd *sessionHost)
    : m_tabCtrl(tabCtrl)
    , m_sessionHost(sessionHost)
{
}

SessionManager::~SessionManager()
{
    closeAllSessions();
}

QString SessionManager::openSession(const Profile &profile)
{
    if (!m_tabCtrl || !m_sessionHost)
        return {};

    auto session = std::make_unique<Session>();
    session->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session->profile = profile;
    session->view = std::make_unique<CRdpSessionView>();

    CRect hostRect;
    m_sessionHost->GetClientRect(&hostRect);
    if (!session->view->create(m_sessionHost, hostRect))
        return {};

    session->view->setReconnectRequestedCallback([this, sessionId = session->id]() {
        reconnectSession(sessionId);
    });
    session->view->connectToHost(profile);

    TCITEM item = {};
    item.mask = TCIF_TEXT;
    std::wstring title = profile.name.toStdWString();
    item.pszText = title.empty() ? const_cast<wchar_t*>(L"(unnamed)") : title.data();
    const int index = m_tabCtrl->InsertItem(static_cast<int>(m_sessions.size()), &item);

    if (index < 0)
        return {};

    m_sessions.push_back(std::move(session));
    m_tabCtrl->SetCurSel(index);
    showSessionAtIndex(index);
    return m_sessions[static_cast<size_t>(index)]->id;
}

void SessionManager::closeSession(const QString &sessionId)
{
    const int index = indexOfSession(sessionId);
    if (index < 0 || !m_tabCtrl)
        return;

    m_sessions[static_cast<size_t>(index)]->view->DestroyWindow();
    m_sessions.erase(m_sessions.begin() + index);
    m_tabCtrl->DeleteItem(index);

    if (m_sessions.empty())
        return;

    const int nextIndex = std::min(index, static_cast<int>(m_sessions.size()) - 1);
    m_tabCtrl->SetCurSel(nextIndex);
    showSessionAtIndex(nextIndex);
}

void SessionManager::reconnectSession(const QString &sessionId)
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

    if (m_tabCtrl) {
        while (m_tabCtrl->GetItemCount() > 0)
            m_tabCtrl->DeleteItem(0);
    }
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

QString SessionManager::sessionIdByTabIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_sessions.size()))
        return {};

    return m_sessions[static_cast<size_t>(index)]->id;
}

bool SessionManager::hasOpenSessions() const
{
    return !m_sessions.empty();
}

int SessionManager::indexOfSession(const QString &sessionId) const
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

