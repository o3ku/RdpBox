#include "SessionCollectionBehavior.h"

#include "session/SessionTabBehavior.h"

#include <utility>

std::string sessionIdAtTabIndex(const std::vector<SessionSnapshot> &sessions, int index)
{
    if (index < 0 || index >= static_cast<int>(sessions.size()))
        return {};

    return sessions[static_cast<std::size_t>(index)].id;
}

std::vector<std::wstring> openProfileNamesForSessions(const std::vector<SessionSnapshot> &sessions)
{
    std::vector<std::wstring> names;
    names.reserve(sessions.size());
    for (const auto &session : sessions)
        names.push_back(session.profileName);
    return names;
}

std::vector<std::wstring> connectedProfileNamesForSessions(const std::vector<SessionSnapshot> &sessions)
{
    std::vector<std::wstring> names;
    names.reserve(sessions.size());
    for (const auto &session : sessions) {
        if (session.connected)
            names.push_back(session.profileName);
    }
    return names;
}

bool moveSessionSnapshot(std::vector<SessionSnapshot> &sessions, int fromIndex, int toIndex)
{
    if (!canMoveSessionTab(static_cast<int>(sessions.size()), fromIndex, toIndex))
        return false;

    SessionSnapshot moved = std::move(sessions[static_cast<std::size_t>(fromIndex)]);
    sessions.erase(sessions.begin() + fromIndex);
    sessions.insert(sessions.begin() + toIndex, std::move(moved));
    return true;
}
