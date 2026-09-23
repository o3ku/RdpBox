#include "common/ConnectionLaunchArgs.h"

#include "common/ConnectionLaunchArgs.h"

#include "Win32String.h"

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

std::wstring powerShellSingleQuotedLiteral(const std::wstring &value)
{
    std::wstring quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back(L'\'');
    for (wchar_t ch : value) {
        if (ch == L'\'')
            quoted += L"''";
        else
            quoted.push_back(ch);
    }
    quoted.push_back(L'\'');
    return quoted;
}

std::string updateScriptUtf8(const std::wstring &script)
{
    // UTF-8 BOM: powershell.exe 5.1 reads BOM-less .ps1 as ANSI and corrupts
    // non-ASCII connection names, which breaks session restore after an update.
    std::string contents = "\xEF\xBB\xBF";
    contents += utf8FromWide(script);
    return contents;
}

std::wstring buildUpdateApplyScript(const std::wstring &downloadedPath,
                                    const std::wstring &currentExePath,
                                    const std::wstring &backupExePath,
                                    const std::wstring &launchParams,
                                    const std::wstring &scriptPath,
                                    const std::wstring &logPath)
{
    std::wstring script;
    script += L"$ErrorActionPreference = 'Stop'\n";
#ifdef _WIN32
    script += L"$pidToWait = " + std::to_wstring(::GetCurrentProcessId()) + L"\n";
#else
    script += L"$pidToWait = 0\n";
#endif
    script += L"$src = " + powerShellSingleQuotedLiteral(downloadedPath) + L"\n";
    script += L"$dst = " + powerShellSingleQuotedLiteral(currentExePath) + L"\n";
    script += L"$bak = " + powerShellSingleQuotedLiteral(backupExePath) + L"\n";
    script += L"$argsLine = " + powerShellSingleQuotedLiteral(launchParams) + L"\n";
    script += L"$log = " + powerShellSingleQuotedLiteral(logPath) + L"\n";
    script += L"function Log($message) { Add-Content -Path $log -Value $message }\n";
    script += L"Log \"==== $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') ====\"\n";
    script += L"Log \"PID=$pidToWait\"\n";
    script += L"Log \"SRC=$src\"\n";
    script += L"Log \"DST=$dst\"\n";
    script += L"try {\n";
    script += L"  try {\n";
    script += L"    Wait-Process -Id $pidToWait -ErrorAction Stop\n";
    script += L"    Log 'old process exited'\n";
    script += L"  } catch {\n";
    script += L"    Log 'old process already exited'\n";
    script += L"  }\n";
    script += L"  for ($i = 0; $i -lt 20; $i++) {\n";
    script += L"    try {\n";
    script += L"      if (Test-Path $bak) { Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue }\n";
    script += L"      if (Test-Path $dst) { Move-Item -LiteralPath $dst -Destination $bak -Force }\n";
    script += L"      Copy-Item -LiteralPath $src -Destination $dst -Force\n";
    script += L"      if (Test-Path $dst) {\n";
    script += L"        Log 'replacement complete'\n";
    script += L"        break\n";
    script += L"      }\n";
    script += L"    } catch {\n";
    script += L"      Log \"replace retry $i : $($_.Exception.Message)\"\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"      continue\n";
    script += L"    }\n";
    script += L"  }\n";
    script += L"  if (-not (Test-Path $dst)) { throw 'replacement failed' }\n";
    script += L"  Remove-Item -LiteralPath $src -Force -ErrorAction SilentlyContinue\n";
    script += L"  for ($i = 0; $i -lt 5; $i++) {\n";
    script += L"    try {\n";
    script += L"      if ([string]::IsNullOrWhiteSpace($argsLine)) {\n";
    script += L"        Log \"launching without args try $i\"\n";
    script += L"        Start-Process -FilePath $dst\n";
    script += L"      } else {\n";
    script += L"        Log \"launching with args try $i : $argsLine\"\n";
    script += L"        Start-Process -FilePath $dst -ArgumentList $argsLine\n";
    script += L"      }\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"      $p = Get-Process -Name 'RdpBox' -ErrorAction SilentlyContinue\n";
    script += L"      if ($p) {\n";
    script += L"        Log 'launch success'\n";
    script += L"        break\n";
    script += L"      }\n";
    script += L"      Log 'launch retry'\n";
    script += L"    } catch {\n";
    script += L"      Log \"launch error $i : $($_.Exception.Message)\"\n";
    script += L"      Start-Sleep -Seconds 1\n";
    script += L"    }\n";
    script += L"  }\n";
    script += L"} catch {\n";
    script += L"  Log \"script error: $($_.Exception.Message)\"\n";
    script += L"} finally {\n";
    script += L"  Log 'script end'\n";
    script += L"  try {\n";
    script += L"    $scriptPath = $PSCommandPath\n";
    script += L"    Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', \"Start-Sleep -Seconds 2; Remove-Item -LiteralPath '$scriptPath' -Force -ErrorAction SilentlyContinue\")\n";
    script += L"  } catch {\n";
    script += L"  }\n";
    script += L"}\n";
    return script;
}
}
