#pragma once

#include <afxwin.h>

class CRdpBoxApp : public CWinApp
{
    DECLARE_MESSAGE_MAP()

public:
    BOOL InitInstance() override;
    int ExitInstance() override;

private:
    bool m_comInitialized = false;
};
