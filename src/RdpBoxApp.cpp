#include "RdpBoxApp.h"

#include "MainWindow.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>

#include <shellapi.h>

namespace
{
std::string utf8FromWide(const wchar_t *text)
{
    if (!text)
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}
}

BEGIN_MESSAGE_MAP(CRdpBoxApp, CWinApp)
END_MESSAGE_MAP()

CRdpBoxApp theApp;

bool CRdpBoxApp::initializeQt()
{
    int argc = 0;
    LPWSTR *argvWide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvWide || argc <= 0)
        return false;

    m_qtArgsUtf8.clear();
    m_qtArgv.clear();
    m_qtArgsUtf8.reserve(static_cast<size_t>(argc));
    m_qtArgv.reserve(static_cast<size_t>(argc));

    for (int index = 0; index < argc; ++index)
        m_qtArgsUtf8.push_back(utf8FromWide(argvWide[index]));

    LocalFree(argvWide);

    for (auto &arg : m_qtArgsUtf8)
        m_qtArgv.push_back(arg.data());

    m_qtArgc = static_cast<int>(m_qtArgv.size());
    m_qtApp = std::make_unique<QGuiApplication>(m_qtArgc, m_qtArgv.data());
    return true;
}

BOOL CRdpBoxApp::InitInstance()
{
    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&commonControls);

    CWinApp::InitInstance();

    if (!initializeQt())
        return FALSE;

    auto *frame = new MainWindow();
    if (!frame->createShell()) {
        delete frame;
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
    m_qtApp.reset();
    return CWinApp::ExitInstance();
}

BOOL CRdpBoxApp::OnIdle(LONG lCount)
{
    if (m_qtApp)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

    const BOOL baseResult = CWinApp::OnIdle(lCount);
    return baseResult || (m_qtApp && QCoreApplication::hasPendingEvents());
}
