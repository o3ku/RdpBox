#include "RdpBoxApp.h"

#include "MainWindow.h"

#include <objbase.h>

BEGIN_MESSAGE_MAP(CRdpBoxApp, CWinApp)
END_MESSAGE_MAP()

CRdpBoxApp theApp;

BOOL CRdpBoxApp::InitInstance()
{
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&commonControls);

    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initResult))
        return FALSE;

    m_comInitialized = true;

    if (!CWinApp::InitInstance()) {
        CoUninitialize();
        m_comInitialized = false;
        return FALSE;
    }

    auto *frame = new MainWindow();
    if (!frame->createShell()) {
        delete frame;
        CoUninitialize();
        m_comInitialized = false;
        return FALSE;
    }

    m_pMainWnd = frame;
    frame->ShowWindow(SW_SHOW);
    frame->UpdateWindow();
    frame->PostMessage(MainWindow::WM_APP_OPEN_CONNECTIONS, FALSE, 0);
    return TRUE;
}

int CRdpBoxApp::ExitInstance()
{
    if (m_comInitialized)
        CoUninitialize();

    return CWinApp::ExitInstance();
}
