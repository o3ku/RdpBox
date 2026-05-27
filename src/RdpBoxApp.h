#pragma once

#include <afxwin.h>

#include <cstdint>
#include <string>
#include <vector>

class CRdpBoxApp : public CWinApp
{
    DECLARE_MESSAGE_MAP()

public:
    void setStartupConnectionNames(std::vector<std::wstring> connectionNames);
    const std::vector<std::wstring> &startupConnectionNames() const;
    BOOL InitInstance() override;
    int ExitInstance() override;

private:
    bool ensurePasswordProtectionReady();
    void cleanupSubsystems();

    bool m_comInitialized = false;
    bool m_wsaInitialized = false;
    std::uintptr_t m_gdiplusToken = 0;
    std::vector<std::wstring> m_startupConnectionNames;
};

extern CRdpBoxApp theApp;
