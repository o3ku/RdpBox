#pragma once

#include <string>
#include <vector>

namespace launch
{
bool tryParseConnectionsArgument(const std::wstring &argument,
                                 std::vector<std::wstring> &connectionNames);
std::vector<std::wstring> parseConnectionsArgumentValue(const std::wstring &value);
std::wstring buildConnectionsArgumentValue(const std::vector<std::wstring> &connectionNames);
}
