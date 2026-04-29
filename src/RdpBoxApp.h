#pragma once

#include <afxwin.h>

#include <cstdint>

class CRdpBoxApp : public CWinApp
{
    DECLARE_MESSAGE_MAP()

public:
    BOOL InitInstance() override;
    int ExitInstance() override;

private:
    bool ensurePasswordProtectionReady();

    bool m_comInitialized = false;
    bool m_wsaInitialized = false;
    std::uintptr_t m_gdiplusToken = 0;
};
