#pragma once

#include <cstdint>
#include <string>

namespace ui
{
enum class MainWindowTabStatus
{
    Inactive,
    ConnectedGood,
    ConnectedWarn,
    ConnectedBad,
};

struct MainWindowConnectionInfo
{
    std::string codecName;
    std::uint32_t rtt = 0;
    bool rttAvailable = false;
};

struct TabContextMenuState
{
    bool fullScreenEnabled = false;
    bool reconnectEnabled = false;
    bool closeEnabled = false;
    std::wstring fullScreenText;
};

enum class TabContextCommand
{
    None,
    ToggleFullScreen,
    Reconnect,
    Close,
};

MainWindowTabStatus tabStatusForConnection(bool connected,
                                           const MainWindowConnectionInfo &connectionInfo);

std::wstring tabTooltipText(const MainWindowConnectionInfo &connectionInfo);

TabContextMenuState tabContextMenuState(int tabIndex,
                                        int selectedIndex,
                                        bool hasTab,
                                        bool fullScreen);

TabContextCommand tabContextCommandForMenuId(unsigned int commandId, bool hasTab);
}
