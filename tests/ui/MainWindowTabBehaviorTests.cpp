#include <cassert>

#include "resources/resource.h"
#include "ui/MainWindowTabBehavior.h"

int main()
{
    using ui::MainWindowConnectionInfo;
    using ui::MainWindowTabStatus;
    using ui::TabContextCommand;

    assert(ui::tabStatusForConnection(false, MainWindowConnectionInfo{"GFX", 10, true})
           == MainWindowTabStatus::Inactive);
    assert(ui::tabStatusForConnection(true, MainWindowConnectionInfo{"GFX", 0, false})
           == MainWindowTabStatus::ConnectedGood);
    assert(ui::tabStatusForConnection(true, MainWindowConnectionInfo{"GFX", 100, true})
           == MainWindowTabStatus::ConnectedGood);
    assert(ui::tabStatusForConnection(true, MainWindowConnectionInfo{"GFX", 101, true})
           == MainWindowTabStatus::ConnectedWarn);
    assert(ui::tabStatusForConnection(true, MainWindowConnectionInfo{"GFX", 300, true})
           == MainWindowTabStatus::ConnectedWarn);
    assert(ui::tabStatusForConnection(true, MainWindowConnectionInfo{"GFX", 301, true})
           == MainWindowTabStatus::ConnectedBad);

    assert(ui::tabTooltipText(MainWindowConnectionInfo{"", 42, true}).empty());
    assert(ui::tabTooltipText(MainWindowConnectionInfo{"GFX", 0, false}) == L"Codec: GFX");
    assert(ui::tabTooltipText(MainWindowConnectionInfo{"NSCodec", 42, true})
           == L"Codec: NSCodec, Delay: 42 ms");

    {
        const auto state = ui::tabContextMenuState(1, 1, true, false);
        assert(state.fullScreenEnabled);
        assert(state.reconnectEnabled);
        assert(state.closeEnabled);
        assert(state.fullScreenText == L"Full Screen");
    }

    {
        const auto state = ui::tabContextMenuState(0, 1, true, true);
        assert(!state.fullScreenEnabled);
        assert(state.reconnectEnabled);
        assert(state.closeEnabled);
        assert(state.fullScreenText == L"Exit Full Screen");
    }

    {
        const auto state = ui::tabContextMenuState(1, 1, false, false);
        assert(state.fullScreenEnabled);
        assert(!state.reconnectEnabled);
        assert(!state.closeEnabled);
    }

    assert(ui::tabContextCommandForMenuId(ID_TAB_FULLSCREEN, true)
           == TabContextCommand::ToggleFullScreen);
    assert(ui::tabContextCommandForMenuId(ID_TAB_RECONNECT, true)
           == TabContextCommand::Reconnect);
    assert(ui::tabContextCommandForMenuId(ID_TAB_CLOSE, true)
           == TabContextCommand::Close);
    assert(ui::tabContextCommandForMenuId(ID_TAB_FULLSCREEN, false)
           == TabContextCommand::None);
    assert(ui::tabContextCommandForMenuId(0, true) == TabContextCommand::None);

    return 0;
}
