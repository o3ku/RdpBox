#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "session/SessionManager.h"

namespace
{
struct FakeViewState
{
    SessionViewBounds initialBounds;
    SessionViewBounds lastBounds;
    std::function<void()> reconnectRequestedCallback;
    std::function<void()> connectedCallback;
    Profile connectedProfile;
    FreeRdpProcess::ConnectionInfo info;
    bool created = true;
    bool connected = false;
    bool destroyed = false;
    bool visible = false;
    int connectCount = 0;
    int reconnectCount = 0;
    int setBoundsCount = 0;
    int redrawCount = 0;
    int flushPendingResizeCount = 0;
    int handleBecameVisibleCount = 0;
    int focusCount = 0;
    std::vector<bool> resizeSuppressedCalls;
    std::vector<bool> resumeAutoReconnectCalls;
};

class FakeSessionView final : public ISessionView
{
public:
    explicit FakeSessionView(std::shared_ptr<FakeViewState> state)
        : m_state(std::move(state))
    {
    }

    void setReconnectRequestedCallback(std::function<void()> callback) override
    {
        m_state->reconnectRequestedCallback = std::move(callback);
    }

    void setConnectedCallback(std::function<void()> callback) override
    {
        m_state->connectedCallback = std::move(callback);
    }

    void connectToHost(const Profile &profile) override
    {
        m_state->connectedProfile = profile;
        ++m_state->connectCount;
    }

    void reconnect() override
    {
        ++m_state->reconnectCount;
    }

    void destroy() override
    {
        m_state->destroyed = true;
        m_state->created = false;
    }

    bool isCreated() const override
    {
        return m_state->created;
    }

    void setBounds(const SessionViewBounds &bounds) override
    {
        m_state->lastBounds = bounds;
        ++m_state->setBoundsCount;
    }

    void redraw() override
    {
        ++m_state->redrawCount;
    }

    void setResizeSuppressed(bool suppressed) override
    {
        m_state->resizeSuppressedCalls.push_back(suppressed);
    }

    void flushPendingResize() override
    {
        ++m_state->flushPendingResizeCount;
    }

    void handleHostResume(bool autoReconnect) override
    {
        m_state->resumeAutoReconnectCalls.push_back(autoReconnect);
    }

    void show(bool visible) override
    {
        m_state->visible = visible;
    }

    void handleBecameVisible() override
    {
        ++m_state->handleBecameVisibleCount;
    }

    void focus() override
    {
        ++m_state->focusCount;
    }

    FreeRdpProcess::ConnectionInfo connectionInfo() const override
    {
        return m_state->info;
    }

    bool isConnected() const override
    {
        return m_state->connected;
    }

private:
    std::shared_ptr<FakeViewState> m_state;
};

class FakeViewFactory final : public ISessionViewFactory
{
public:
    std::unique_ptr<ISessionView> createView(const SessionViewBounds &bounds) override
    {
        if (failNextCreate) {
            failNextCreate = false;
            return {};
        }

        auto state = std::make_shared<FakeViewState>();
        state->initialBounds = bounds;
        views.push_back(state);
        return std::make_unique<FakeSessionView>(state);
    }

    bool failNextCreate = false;
    std::vector<std::shared_ptr<FakeViewState>> views;
};

class FakeTabs final : public ISessionTabs
{
public:
    int insertTab(const std::wstring &title) override
    {
        if (failNextInsert) {
            failNextInsert = false;
            return -1;
        }

        titles.push_back(title);
        return static_cast<int>(titles.size()) - 1;
    }

    void removeTab(int index) override
    {
        removedIndexes.push_back(index);
        if (index >= 0 && index < static_cast<int>(titles.size()))
            titles.erase(titles.begin() + index);
    }

    void clearTabs() override
    {
        ++clearCount;
        titles.clear();
        selected = -1;
    }

    void setSelectedIndex(int index) override
    {
        selected = index;
        selectedHistory.push_back(index);
    }

    int selectedIndex() const override
    {
        return selected;
    }

    bool failNextInsert = false;
    int selected = -1;
    int clearCount = 0;
    std::vector<std::wstring> titles;
    std::vector<int> removedIndexes;
    std::vector<int> selectedHistory;
};

class FakeHost final : public ISessionHost
{
public:
    SessionViewBounds clientBounds() const override
    {
        return bounds;
    }

    SessionViewBounds bounds{ 10, 20, 300, 200 };
};

Profile profile(std::wstring name)
{
    Profile result;
    result.name = std::move(name);
    result.host = L"server";
    result.username = L"user";
    result.port = 3389;
    return result;
}

void assertBounds(const SessionViewBounds &bounds, int left, int top, int width, int height)
{
    assert(bounds.left == left);
    assert(bounds.top == top);
    assert(bounds.width == width);
    assert(bounds.height == height);
}
}

int main()
{
    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        const std::string id = manager.openSession(profile(L"alpha"));

        assert(!id.empty());
        assert(manager.hasOpenSessions());
        assert(tabs.titles.size() == 1);
        assert(tabs.titles[0] == L"alpha");
        assert(tabs.selected == 0);
        assert(manager.sessionIdByTabIndex(0) == id);
        assert(factory.views.size() == 1);
        assertBounds(factory.views[0]->initialBounds, 10, 20, 300, 200);
        assert(factory.views[0]->connectCount == 1);
        assert(factory.views[0]->connectedProfile.name == L"alpha");
        assert(factory.views[0]->visible);
        assert(factory.views[0]->handleBecameVisibleCount == 1);
        assert(factory.views[0]->focusCount == 1);
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        const std::string id = manager.openSession(profile({}));

        assert(!id.empty());
        assert(tabs.titles.size() == 1);
        assert(tabs.titles[0] == L"(unnamed)");
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        factory.failNextCreate = true;
        SessionManager manager(&tabs, &host, &factory);

        assert(manager.openSession(profile(L"alpha")).empty());
        assert(!manager.hasOpenSessions());
        assert(tabs.titles.empty());
        assert(factory.views.empty());
    }

    {
        FakeTabs tabs;
        tabs.failNextInsert = true;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        assert(manager.openSession(profile(L"alpha")).empty());
        assert(!manager.hasOpenSessions());
        assert(tabs.titles.empty());
        assert(factory.views.size() == 1);
        assert(factory.views[0]->destroyed);
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        const std::string first = manager.openSession(profile(L"alpha"));
        const std::string second = manager.openSession(profile(L"beta"));

        assert(!first.empty());
        assert(!second.empty());
        assert(!factory.views[0]->visible);
        assert(factory.views[1]->visible);

        manager.activateTab(0);
        assert(factory.views[0]->visible);
        assert(!factory.views[1]->visible);
        assert(factory.views[0]->handleBecameVisibleCount == 2);
        assert(factory.views[0]->focusCount == 2);

        tabs.selected = 1;
        manager.focusActiveSession();
        assert(!factory.views[0]->visible);
        assert(factory.views[1]->visible);
        assert(factory.views[1]->handleBecameVisibleCount == 2);
        assert(factory.views[1]->focusCount == 2);
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        const std::string first = manager.openSession(profile(L"alpha"));
        const std::string second = manager.openSession(profile(L"beta"));
        const std::string third = manager.openSession(profile(L"gamma"));

        manager.closeSession(second);
        assert(factory.views[1]->destroyed);
        assert(tabs.removedIndexes.size() == 1);
        assert(tabs.removedIndexes[0] == 1);
        assert(tabs.selected == 1);
        assert(manager.sessionIdByTabIndex(0) == first);
        assert(manager.sessionIdByTabIndex(1) == third);
        assert(factory.views[2]->visible);

        manager.closeSession(first);
        assert(tabs.selected == 0);
        assert(manager.sessionIdByTabIndex(0) == third);

        manager.closeSession(third);
        assert(!manager.hasOpenSessions());
        assert(tabs.titles.empty());
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        const std::string first = manager.openSession(profile(L"alpha"));
        const std::string second = manager.openSession(profile(L"beta"));
        const std::string third = manager.openSession(profile(L"gamma"));

        tabs.selected = 2;
        assert(manager.moveSession(0, 2));
        assert(manager.sessionIdByTabIndex(0) == second);
        assert(manager.sessionIdByTabIndex(1) == third);
        assert(manager.sessionIdByTabIndex(2) == first);
        assert(factory.views[0]->visible);
        assert(!manager.moveSession(1, 1));
        assert(!manager.moveSession(-1, 0));
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        manager.openSession(profile(L"alpha"));
        manager.openSession(profile(L"beta"));

        host.bounds = { 5, 6, 700, 500 };
        manager.layoutSessions();
        assert(factory.views[0]->setBoundsCount == 1);
        assert(factory.views[1]->setBoundsCount == 1);
        assertBounds(factory.views[0]->lastBounds, 5, 6, 700, 500);
        assert(factory.views[0]->redrawCount == 1);
        assert(factory.views[1]->redrawCount == 1);

        manager.setResizeSuppressed(true);
        manager.flushPendingResize();
        assert(factory.views[0]->resizeSuppressedCalls.size() == 1);
        assert(factory.views[0]->resizeSuppressedCalls[0]);
        assert(factory.views[1]->flushPendingResizeCount == 1);

        tabs.selected = 1;
        manager.handleHostResume();
        assert(factory.views[0]->resumeAutoReconnectCalls.size() == 1);
        assert(!factory.views[0]->resumeAutoReconnectCalls[0]);
        assert(factory.views[1]->resumeAutoReconnectCalls.size() == 1);
        assert(factory.views[1]->resumeAutoReconnectCalls[0]);
    }

    {
        FakeTabs tabs;
        FakeHost host;
        FakeViewFactory factory;
        SessionManager manager(&tabs, &host, &factory);

        const std::string id = manager.openSession(profile(L"alpha"));
        factory.views[0]->connected = true;
        factory.views[0]->info.codecName = "GFX";
        factory.views[0]->info.rtt = 42;
        factory.views[0]->info.rttAvailable = true;

        const auto info = manager.connectionInfoForTab(0);
        assert(info.codecName == "GFX");
        assert(info.rtt == 42);
        assert(info.rttAvailable);
        assert(manager.isTabConnected(0));

        const auto connectedNames = manager.connectedProfileNames();
        assert(connectedNames.size() == 1);
        assert(connectedNames[0] == L"alpha");

        factory.views[0]->reconnectRequestedCallback();
        assert(factory.views[0]->reconnectCount == 1);

        manager.closeSession(id);
        factory.views[0]->connectedCallback();
        assert(factory.views[0]->reconnectCount == 1);
    }

    return 0;
}
