#include <cassert>

#include "common/ConnectionLaunchArgs.h"

int main()
{
    {
        const std::vector<std::wstring> names =
            launch::parseConnectionsArgumentValue(L"alpha,beta,gamma");
        assert(names.size() == 3);
        assert(names[0] == L"alpha");
        assert(names[1] == L"beta");
        assert(names[2] == L"gamma");
    }

    {
        const std::vector<std::wstring> names =
            launch::parseConnectionsArgumentValue(L" alpha ,  beta  ,, gamma ");
        assert(names.size() == 3);
        assert(names[0] == L"alpha");
        assert(names[1] == L"beta");
        assert(names[2] == L"gamma");
    }

    {
        std::vector<std::wstring> names;
        assert(launch::tryParseConnectionsArgument(L"--connections=left,right", names));
        assert(names.size() == 2);
        assert(names[0] == L"left");
        assert(names[1] == L"right");
        assert(!launch::tryParseConnectionsArgument(L"--portable", names));
    }

    {
        const std::wstring value = launch::buildConnectionsArgumentValue({
            L" alpha ",
            L"",
            L"beta",
            L" gamma"
        });
        assert(value == L"alpha,beta,gamma");
    }

    {
        assert(launch::powerShellSingleQuotedLiteral(L"plain") == L"'plain'");
        assert(launch::powerShellSingleQuotedLiteral(L"it's") == L"'it''s'");
        assert(launch::powerShellSingleQuotedLiteral(L"--connections=\"a b,c\"")
               == L"'--connections=\"a b,c\"'");
        assert(launch::powerShellSingleQuotedLiteral(L"\u6d4b\u8bd5\u673a204")
               == L"'\u6d4b\u8bd5\u673a204'");
    }

    {
        const std::string script = launch::updateScriptUtf8(L"$s = '\u6d4b\u8bd5\u673a204'\n");
        assert(script.size() >= 3);
        assert(static_cast<unsigned char>(script[0]) == 0xEF);
        assert(static_cast<unsigned char>(script[1]) == 0xBB);
        assert(static_cast<unsigned char>(script[2]) == 0xBF);
        // UTF-8 bytes of U+6D4B survive after the BOM
        assert(script.find("\xE6\xB5\x8B") == 3 + 6);
    }

    {
        const std::wstring script = launch::buildUpdateApplyScript(
            L"C:\\upd\\RdpBox-v2.exe", L"C:\\App\\RdpBox.exe",
            L"C:\\App\\RdpBox.exe.bak",
            L"--portable --connections=\"\u6d4b\u8bd5\u673a204,dev box\"",
            L"C:\\upd\\apply-update-42.ps1", L"C:\\upd\\update-apply.log");
        assert(script.find(L"-LiteralPath $bak") != std::wstring::npos);
        assert(script.find(L"Move-Item -LiteralPath $dst") != std::wstring::npos);
        assert(script.find(L"Wait-Process") != std::wstring::npos);
        assert(script.find(L"$argsLine = '--portable --connections=\"\u6d4b\u8bd5\u673a204,dev box\"'") != std::wstring::npos);
        assert(script.find(L"Remove-Item -LiteralPath '$scriptPath'") != std::wstring::npos);
    }

    return 0;
}
