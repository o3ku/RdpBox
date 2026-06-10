#include <afxwin.h>

#include "SessionManager.h"

#include "rdp/RdpSessionView.h"
#include "ui/BrowserTabBar.h"

#include <memory>
#include <utility>

namespace
{
class BrowserTabSessionTabs final : public ISessionTabs
{
public:
    explicit BrowserTabSessionTabs(BrowserTabBar *tabBar)
        : m_tabBar(tabBar)
    {
    }

    int insertTab(const std::wstring &title) override
    {
        return m_tabBar ? m_tabBar->insertTab(title) : -1;
    }

    void removeTab(int index) override
    {
        if (m_tabBar)
            m_tabBar->removeTab(index);
    }

    void clearTabs() override
    {
        if (m_tabBar)
            m_tabBar->clearTabs();
    }

    void setSelectedIndex(int index) override
    {
        if (m_tabBar)
            m_tabBar->setSelectedIndex(index);
    }

    int selectedIndex() const override
    {
        return m_tabBar ? m_tabBar->selectedIndex() : -1;
    }

private:
    BrowserTabBar *m_tabBar = nullptr;
};

class MfcSessionHost final : public ISessionHost
{
public:
    explicit MfcSessionHost(CWnd *sessionHost)
        : m_sessionHost(sessionHost)
    {
    }

    SessionViewBounds clientBounds() const override
    {
        if (!m_sessionHost)
            return {};

        CRect rect;
        m_sessionHost->GetClientRect(&rect);
        return { rect.left, rect.top, rect.Width(), rect.Height() };
    }

private:
    CWnd *m_sessionHost = nullptr;
};

class RdpSessionViewAdapter final : public ISessionView
{
public:
    explicit RdpSessionViewAdapter(std::unique_ptr<CRdpSessionView> view)
        : m_view(std::move(view))
    {
    }

    void setReconnectRequestedCallback(std::function<void()> callback) override
    {
        if (m_view)
            m_view->setReconnectRequestedCallback(std::move(callback));
    }

    void setConnectedCallback(std::function<void()> callback) override
    {
        if (m_view)
            m_view->setConnectedCallback(std::move(callback));
    }

    void connectToHost(const Profile &profile) override
    {
        if (m_view)
            m_view->connectToHost(profile);
    }

    void reconnect() override
    {
        if (m_view)
            m_view->reconnect();
    }

    void destroy() override
    {
        if (m_view)
            m_view->DestroyWindow();
    }

    bool isCreated() const override
    {
        return m_view && m_view->GetSafeHwnd();
    }

    void setBounds(const SessionViewBounds &bounds) override
    {
        if (isCreated()) {
            m_view->SetWindowPos(nullptr,
                                 bounds.left,
                                 bounds.top,
                                 bounds.width,
                                 bounds.height,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void redraw() override
    {
        if (isCreated()) {
            m_view->RedrawWindow(nullptr, nullptr,
                                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
        }
    }

    void setResizeSuppressed(bool suppressed) override
    {
        if (isCreated())
            m_view->setResizeSuppressed(suppressed);
    }

    void flushPendingResize() override
    {
        if (isCreated())
            m_view->flushPendingResize();
    }

    void handleHostResume(bool autoReconnect) override
    {
        if (isCreated())
            m_view->handleHostResume(autoReconnect);
    }

    void show(bool visible) override
    {
        if (isCreated())
            m_view->ShowWindow(visible ? SW_SHOW : SW_HIDE);
    }

    void handleBecameVisible() override
    {
        if (isCreated())
            m_view->handleBecameVisible();
    }

    void focus() override
    {
        if (isCreated())
            m_view->SetFocus();
    }

    FreeRdpProcess::ConnectionInfo connectionInfo() const override
    {
        return m_view ? m_view->connectionInfo() : FreeRdpProcess::ConnectionInfo{};
    }

    bool isConnected() const override
    {
        return m_view && m_view->isConnected();
    }

private:
    std::unique_ptr<CRdpSessionView> m_view;
};

class RdpSessionViewFactory final : public ISessionViewFactory
{
public:
    explicit RdpSessionViewFactory(CWnd *parent)
        : m_parent(parent)
    {
    }

    std::unique_ptr<ISessionView> createView(const SessionViewBounds &bounds) override
    {
        if (!m_parent)
            return {};

        auto view = std::make_unique<CRdpSessionView>();
        const CRect rect(bounds.left,
                         bounds.top,
                         bounds.left + bounds.width,
                         bounds.top + bounds.height);
        if (!view->create(m_parent, rect))
            return {};

        return std::make_unique<RdpSessionViewAdapter>(std::move(view));
    }

private:
    CWnd *m_parent = nullptr;
};
}

SessionManager::SessionManager(BrowserTabBar *tabBar, CWnd *sessionHost, ProfileRepository *repository)
    : m_ownedTabs(std::make_unique<BrowserTabSessionTabs>(tabBar))
    , m_ownedHost(std::make_unique<MfcSessionHost>(sessionHost))
    , m_ownedViewFactory(std::make_unique<RdpSessionViewFactory>(sessionHost))
    , m_tabs(m_ownedTabs.get())
    , m_host(m_ownedHost.get())
    , m_viewFactory(m_ownedViewFactory.get())
    , m_repository(repository)
{
}
