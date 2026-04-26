#include <afxwin.h>

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

extern "C" int WINAPI WinMain(HINSTANCE hInstance,
                               HINSTANCE hPrevInstance,
                               LPSTR,
                               int nCmdShow)
{
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
