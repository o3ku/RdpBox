#include "RdpBoxApp.h"

#include "common/AppPaths.h"
#include "common/PasswordProtection.h"
#include "MainWindow.h"

#include <objbase.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
namespace Gdiplus
{
using std::min;
using std::max;
}
#include <gdiplus.h>

BEGIN_MESSAGE_MAP(CRdpBoxApp, CWinApp)
END_MESSAGE_MAP()

CRdpBoxApp theApp;

bool CRdpBoxApp::ensurePasswordProtectionReady()
{
    PasswordProtection::setMode(
        AppPaths::isPortableMode()
            ? PasswordProtection::Mode::Portable
            : PasswordProtection::Mode::Dpapi);
    return PasswordProtection::isReady();
}

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

    WSADATA wsaData = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cleanupSubsystems();
        return FALSE;
    }
    m_wsaInitialized = true;

    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &gdiInput, nullptr) != Gdiplus::Ok) {
        cleanupSubsystems();
        return FALSE;
    }
    m_gdiplusToken = static_cast<std::uintptr_t>(token);

    if (!CWinApp::InitInstance()) {
        cleanupSubsystems();
        return FALSE;
    }

    if (!ensurePasswordProtectionReady()) {
        cleanupSubsystems();
        return FALSE;
    }

    auto *frame = new MainWindow();
    if (!frame->createShell()) {
        delete frame;
        cleanupSubsystems();
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
    cleanupSubsystems();
    return CWinApp::ExitInstance();
}

void CRdpBoxApp::cleanupSubsystems()
{
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(static_cast<ULONG_PTR>(m_gdiplusToken));
        m_gdiplusToken = 0;
    }
    if (m_wsaInitialized) {
        WSACleanup();
        m_wsaInitialized = false;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}
