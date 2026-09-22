#include "MainWindowTabBehavior.h"

#include "resources/resource.h"

namespace ui
{
namespace
{
std::wstring wideFromAscii(const std::string &value)
{
    return std::wstring(value.begin(), value.end());
}
}

MainWindowTabStatus tabStatusForConnection(bool connected,
                                           const MainWindowConnectionInfo &connectionInfo)
{
    if (!connected)
        return MainWindowTabStatus::Inactive;

    if (!connectionInfo.rttAvailable)
        return MainWindowTabStatus::ConnectedGood;

    if (connectionInfo.rtt <= 100)
        return MainWindowTabStatus::ConnectedGood;
    if (connectionInfo.rtt <= 300)
        return MainWindowTabStatus::ConnectedWarn;
    return MainWindowTabStatus::ConnectedBad;
}

std::wstring tabTooltipText(const MainWindowConnectionInfo &connectionInfo)
{
    if (connectionInfo.codecName.empty())
        return {};

    std::wstring text = L"Codec: ";
    text += wideFromAscii(connectionInfo.codecName);
    if (connectionInfo.rttAvailable) {
        text += L", Delay: ";
        text += std::to_wstring(connectionInfo.rtt);
        text += L" ms";
    }
    return text;
}

TabContextMenuState tabContextMenuState(int tabIndex,
                                        int selectedIndex,
                                        bool hasTab,
                                        bool fullScreen)
{
    TabContextMenuState state;
    state.fullScreenEnabled = tabIndex == selectedIndex;
    state.reconnectEnabled = hasTab;
    state.closeEnabled = hasTab;
    state.fullScreenText = fullScreen ? L"Exit Full Screen" : L"Full Screen";
    return state;
}

TabContextCommand tabContextCommandForMenuId(unsigned int commandId, bool hasTab)
{
    if (!hasTab)
        return TabContextCommand::None;

    switch (commandId) {
    case ID_TAB_FULLSCREEN:
        return TabContextCommand::ToggleFullScreen;
    case ID_TAB_RECONNECT:
        return TabContextCommand::Reconnect;
    case ID_TAB_CLOSE:
        return TabContextCommand::Close;
    default:
        return TabContextCommand::None;
    }
}
}
