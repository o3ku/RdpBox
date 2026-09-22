#pragma once

#include <string>
#include <vector>

namespace launch
{
bool tryParseConnectionsArgument(const std::wstring &argument,
                                 std::vector<std::wstring> &connectionNames);
std::vector<std::wstring> parseConnectionsArgumentValue(const std::wstring &value);
std::wstring buildConnectionsArgumentValue(const std::vector<std::wstring> &connectionNames);
std::wstring powerShellSingleQuotedLiteral(const std::wstring &value);
std::string updateScriptUtf8(const std::wstring &script);
std::wstring buildUpdateApplyScript(const std::wstring &downloadedPath,
                                    const std::wstring &currentExePath,
                                    const std::wstring &backupExePath,
                                    const std::wstring &launchParams,
                                    const std::wstring &scriptPath,
                                    const std::wstring &logPath);
}
