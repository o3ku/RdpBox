#pragma once

#include <afxwin.h>

#include <memory>
#include <string>
#include <vector>

class QGuiApplication;

class CRdpBoxApp : public CWinApp
{
    DECLARE_MESSAGE_MAP()

public:
    BOOL InitInstance() override;
    int ExitInstance() override;
    BOOL OnIdle(LONG lCount) override;

private:
    bool initializeQt();

    int m_qtArgc = 0;
    std::vector<std::string> m_qtArgsUtf8;
    std::vector<char*> m_qtArgv;
    std::unique_ptr<QGuiApplication> m_qtApp;
};
