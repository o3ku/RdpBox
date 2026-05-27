#include "common/ConnectionLaunchArgs.h"

#include <algorithm>
#include <cwctype>

namespace
{
std::wstring trimWhitespace(std::wstring value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    }).base();

    if (first >= last)
        return {};

    return std::wstring(first, last);
}
}

namespace launch
{
bool tryParseConnectionsArgument(const std::wstring &argument,
                                 std::vector<std::wstring> &connectionNames)
{
    static constexpr wchar_t kPrefix[] = L"--connections=";
    if (argument.rfind(kPrefix, 0) != 0)
        return false;

    connectionNames = parseConnectionsArgumentValue(argument.substr(14));
    return true;
}

std::vector<std::wstring> parseConnectionsArgumentValue(const std::wstring &value)
{
    std::vector<std::wstring> connectionNames;
    std::wstring current;
    for (wchar_t ch : value) {
        if (ch == L',') {
            const std::wstring trimmed = trimWhitespace(current);
            if (!trimmed.empty())
                connectionNames.push_back(trimmed);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    const std::wstring trimmed = trimWhitespace(current);
    if (!trimmed.empty())
        connectionNames.push_back(trimmed);

    return connectionNames;
}

std::wstring buildConnectionsArgumentValue(const std::vector<std::wstring> &connectionNames)
{
    std::wstring value;
    for (const std::wstring &name : connectionNames) {
        const std::wstring trimmed = trimWhitespace(name);
        if (trimmed.empty())
            continue;

        if (!value.empty())
            value += L",";
        value += trimmed;
    }
    return value;
}
}
