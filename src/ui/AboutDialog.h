#pragma once

#include <afxdialogex.h>

#include "ui/FlatButton.h"

class AboutDialog : public CDialogEx
{
    DECLARE_DYNAMIC(AboutDialog)

public:
    explicit AboutDialog(CWnd *parent = nullptr);
    ~AboutDialog() override;

protected:
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange *dx) override;

    DECLARE_MESSAGE_MAP()

    afx_msg HBRUSH OnCtlColor(CDC *dc, CWnd *wnd, UINT ctrlType);
    afx_msg void OnRepoClicked();

private:
    CString m_titleText;
    CString m_releaseDateText;
    CString m_repoUrlText;
    FlatButton m_btnOK;
    CFont m_linkFont;
    HCURSOR m_handCursor = nullptr;
};
