#pragma once

#include <string>
#include <vector>

struct SessionSnapshot
{
    std::string id;
    std::wstring profileName;
    bool connected = false;
};

std::string sessionIdAtTabIndex(const std::vector<SessionSnapshot> &sessions, int index);

std::vector<std::wstring> openProfileNamesForSessions(const std::vector<SessionSnapshot> &sessions);

std::vector<std::wstring> connectedProfileNamesForSessions(const std::vector<SessionSnapshot> &sessions);

bool moveSessionSnapshot(std::vector<SessionSnapshot> &sessions, int fromIndex, int toIndex);
