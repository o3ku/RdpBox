#pragma once

#include "profiles/Profile.h"
#include "rdp/FreeRdpProcess.h"

#include <functional>
#include <memory>
#include <string>

struct SessionViewBounds
{
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
};

class ISessionTabs
{
public:
    virtual ~ISessionTabs() = default;

    virtual int insertTab(const std::wstring &title) = 0;
    virtual void removeTab(int index) = 0;
    virtual void clearTabs() = 0;
    virtual void setSelectedIndex(int index) = 0;
    virtual int selectedIndex() const = 0;
};

class ISessionHost
{
public:
    virtual ~ISessionHost() = default;

    virtual SessionViewBounds clientBounds() const = 0;
};

class ISessionView
{
public:
    virtual ~ISessionView() = default;

    virtual void setReconnectRequestedCallback(std::function<void()> callback) = 0;
    virtual void setConnectedCallback(std::function<void()> callback) = 0;
    virtual void connectToHost(const Profile &profile) = 0;
    virtual void reconnect() = 0;
    virtual void destroy() = 0;
    virtual bool isCreated() const = 0;
    virtual void setBounds(const SessionViewBounds &bounds) = 0;
    virtual void redraw() = 0;
    virtual void setResizeSuppressed(bool suppressed) = 0;
    virtual void flushPendingResize() = 0;
    virtual void handleHostResume(bool autoReconnect) = 0;
    virtual void show(bool visible) = 0;
    virtual void handleBecameVisible() = 0;
    virtual void focus() = 0;
    virtual FreeRdpProcess::ConnectionInfo connectionInfo() const = 0;
    virtual bool isConnected() const = 0;
};

class ISessionViewFactory
{
public:
    virtual ~ISessionViewFactory() = default;

    virtual std::unique_ptr<ISessionView> createView(const SessionViewBounds &bounds) = 0;
};
