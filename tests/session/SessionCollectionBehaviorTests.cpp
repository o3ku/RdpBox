#include <cassert>
#include <utility>

#include "mfcui/session/SessionCollectionBehavior.h"

namespace
{
SessionSnapshot session(std::string id, std::wstring profileName, bool connected)
{
    return SessionSnapshot{std::move(id), std::move(profileName), connected};
}
}

int main()
{
    std::vector<SessionSnapshot> sessions = {
        session("a", L"alpha", true),
        session("b", L"beta", false),
        session("c", L"gamma", true),
    };

    assert(sessionIdAtTabIndex(sessions, -1).empty());
    assert(sessionIdAtTabIndex(sessions, 3).empty());
    assert(sessionIdAtTabIndex(sessions, 0) == "a");
    assert(sessionIdAtTabIndex(sessions, 2) == "c");

    {
        const auto names = openProfileNamesForSessions(sessions);
        assert(names.size() == 3);
        assert(names[0] == L"alpha");
        assert(names[1] == L"beta");
        assert(names[2] == L"gamma");
    }

    {
        const auto names = connectedProfileNamesForSessions(sessions);
        assert(names.size() == 2);
        assert(names[0] == L"alpha");
        assert(names[1] == L"gamma");
    }

    return 0;
}
