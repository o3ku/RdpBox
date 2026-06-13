#include <cassert>
#include <utility>

#include "ui/ConnectionListBehavior.h"

namespace
{
Profile profile(std::wstring name, std::wstring host = L"host")
{
    Profile result;
    result.name = std::move(name);
    result.host = std::move(host);
    result.port = 3389;
    return result;
}
}

int main()
{
    const std::vector<Profile> visibleProfiles = {
        profile(L"alpha"),
        profile(L"beta"),
        profile(L"gamma"),
    };
    const std::vector<std::wstring> connectedNames = {L"beta"};

    assert(isProfileConnected(L"beta", connectedNames));
    assert(!isProfileConnected(L"Beta", connectedNames));
    assert(!isProfileConnected(L"gamma", connectedNames));
    assert(connectionListStatusText(L"beta", connectedNames) == L"Connected");
    assert(connectionListStatusText(L"gamma", connectedNames).empty());

    {
        const ConnectionListButtonState state =
            connectionListButtonState(visibleProfiles, {}, connectedNames);
        assert(!state.editEnabled);
        assert(!state.deleteEnabled);
        assert(!state.duplicateEnabled);
        assert(!state.connectEnabled);
    }

    {
        const ConnectionListButtonState state =
            connectionListButtonState(visibleProfiles, {0}, connectedNames);
        assert(state.editEnabled);
        assert(state.deleteEnabled);
        assert(state.duplicateEnabled);
        assert(state.connectEnabled);
    }

    {
        const ConnectionListButtonState state =
            connectionListButtonState(visibleProfiles, {1}, connectedNames);
        assert(state.editEnabled);
        assert(state.deleteEnabled);
        assert(state.duplicateEnabled);
        assert(!state.connectEnabled);
    }

    {
        const ConnectionListButtonState state =
            connectionListButtonState(visibleProfiles, {0, 1}, connectedNames);
        assert(!state.editEnabled);
        assert(state.deleteEnabled);
        assert(state.duplicateEnabled);
        assert(state.connectEnabled);
    }

    {
        const auto names =
            connectableProfileNamesForSelection(visibleProfiles, {0, 1, 2, 99}, connectedNames);
        assert(names.size() == 2);
        assert(names[0] == L"alpha");
        assert(names[1] == L"gamma");
    }

    {
        const auto rows = retainedSelectionRowsForProfiles(visibleProfiles, {L"gamma", L"alpha"});
        assert(rows.size() == 2);
        assert(rows[0] == 0);
        assert(rows[1] == 2);

        const auto fallbackRows = retainedSelectionRowsForProfiles(visibleProfiles, {L"missing"});
        assert(fallbackRows.size() == 1);
        assert(fallbackRows[0] == 0);

        assert(retainedSelectionRowsForProfiles({}, {L"alpha"}).empty());
    }

    {
        Profile named = profile(L"alpha");
        assert(duplicateProfileDraft(named).name == L"alpha(n)");
        named.name.clear();
        assert(duplicateProfileDraft(named).name == L"(unnamed)");
    }

    {
        const std::vector<Profile> repositoryProfiles = {
            profile(L"alpha"),
            profile(L"beta"),
            profile(L"gamma"),
            profile(L"delta"),
            profile(L"epsilon"),
        };
        const std::vector<Profile> filteredProfiles = {
            profile(L"beta"),
            profile(L"delta"),
        };

        assert(repositoryTargetIndexForVisibleInsertIndex(repositoryProfiles, filteredProfiles, -1) == 1);
        assert(repositoryTargetIndexForVisibleInsertIndex(repositoryProfiles, filteredProfiles, 0) == 1);
        assert(repositoryTargetIndexForVisibleInsertIndex(repositoryProfiles, filteredProfiles, 1) == 3);
        assert(repositoryTargetIndexForVisibleInsertIndex(repositoryProfiles, filteredProfiles, 2) == 4);
        assert(repositoryTargetIndexForVisibleInsertIndex(repositoryProfiles, filteredProfiles, 99) == 4);
        assert(repositoryTargetIndexForVisibleInsertIndex(repositoryProfiles, {}, 0) == 0);
    }

    {
        assert(targetSelectionIndex(1, 3, 1).value() == 2);
        assert(targetSelectionIndex(1, 3, -1).value() == 0);
        assert(!targetSelectionIndex(0, 3, -1).has_value());
        assert(!targetSelectionIndex(2, 3, 1).has_value());
        assert(!targetSelectionIndex(-1, 3, 1).has_value());
        assert(!targetSelectionIndex(1, 3, 0).has_value());
    }

    return 0;
}
