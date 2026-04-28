#pragma once

#include <afxwin.h>
#include <afxdialogex.h>

#include "profiles/Profile.h"
#include "ui/FlatButton.h"

class ProfileEditDialog : public CDialogEx
{
    DECLARE_DYNAMIC(ProfileEditDialog)

public:
    explicit ProfileEditDialog(CWnd *parent = nullptr);
    virtual ~ProfileEditDialog();

    void setProfile(const Profile &profile);
    Profile profile() const;

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void DoDataExchange(CDataExchange *dx);

    DECLARE_MESSAGE_MAP()

private:
    Profile m_profile;
    CString m_name;
    CString m_host;
    int m_port = 3389;
    CString m_domain;
    CString m_username;
    CString m_password;
    BOOL m_clipboardEnabled = TRUE;
    BOOL m_ignoreCertificate = TRUE;
    BOOL m_fullScreenOnConnect = FALSE;

    FlatButton m_btnOK;
    FlatButton m_btnCancel;
};
