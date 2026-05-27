#include "RdpBoxApp.h"
#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"

#include <afxwin.h>
#include <shellapi.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

extern "C" int WINAPI WinMain(HINSTANCE hInstance,
                               HINSTANCE hPrevInstance,
                               LPSTR,
                               int nCmdShow)
{
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, L"RdpBox_SingleInstance");
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(mutex);
        HWND existing = ::FindWindowW(nullptr, L"RdpBox");
        if (existing) {
            ::SetForegroundWindow(existing);
            if (::IsIconic(existing))
                ::ShowWindow(existing, SW_RESTORE);
        }
        return 0;
    }

    int argc = 0;
    LPWSTR *argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        std::vector<std::wstring> startupConnectionNames;
        for (int i = 1; i < argc; ++i) {
            if (argv[i] && wcscmp(argv[i], L"--portable") == 0) {
                AppPaths::enablePortableMode();
                continue;
            }

            if (!argv[i])
                continue;

            std::vector<std::wstring> parsedNames;
            if (launch::tryParseConnectionsArgument(argv[i], parsedNames))
                startupConnectionNames = std::move(parsedNames);
        }
        theApp.setStartupConnectionNames(std::move(startupConnectionNames));
        ::LocalFree(argv);
    }

    if (!AfxWinInit(hInstance, hPrevInstance, GetCommandLineW(), nCmdShow))
        return FALSE;

    CWinThread *thread = AfxGetThread();
    if (!thread)
        return FALSE;

    if (!thread->InitInstance()) {
        const int exitCode = thread->ExitInstance();
        AfxWinTerm();
        return exitCode;
    }

    const int exitCode = thread->Run();
    AfxWinTerm();
    return exitCode;
}
