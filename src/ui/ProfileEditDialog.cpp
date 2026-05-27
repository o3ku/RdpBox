#include "ProfileEditDialog.h"

#include "resources/resource.h"

#include <uxtheme.h>

IMPLEMENT_DYNAMIC(ProfileEditDialog, CDialogEx)

BEGIN_MESSAGE_MAP(ProfileEditDialog, CDialogEx)
END_MESSAGE_MAP()

ProfileEditDialog::ProfileEditDialog(CWnd *parent)
    : CDialogEx(IDD_PROFILE_DIALOG, parent)
{
}

ProfileEditDialog::~ProfileEditDialog() = default;

void ProfileEditDialog::DoDataExchange(CDataExchange *dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Text(dx, IDC_PROFILE_NAME, m_name);
    DDX_Text(dx, IDC_PROFILE_HOST, m_host);
    DDX_Text(dx, IDC_PROFILE_PORT, m_port);
    DDX_Text(dx, IDC_PROFILE_DOMAIN, m_domain);
    DDX_Text(dx, IDC_PROFILE_USERNAME, m_username);
    DDX_Text(dx, IDC_PROFILE_PASSWORD, m_password);
    DDX_Check(dx, IDC_PROFILE_CLIPBOARD, m_clipboardEnabled);
    DDX_Check(dx, IDC_PROFILE_IGNORE_CERT, m_ignoreCertificate);
    DDX_Check(dx, IDC_PROFILE_FULLSCREEN, m_fullScreenOnConnect);
    DDX_Control(dx, IDOK, m_btnOK);
    DDX_Control(dx, IDCANCEL, m_btnCancel);
}

BOOL ProfileEditDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    EnableThemeDialogTexture(GetSafeHwnd(), ETDT_ENABLETAB);

    if (m_btnOK.GetSafeHwnd())
        m_btnOK.ModifyStyle(BS_DEFPUSHBUTTON, BS_OWNERDRAW);
    if (m_btnCancel.GetSafeHwnd())
        m_btnCancel.ModifyStyle(BS_DEFPUSHBUTTON, BS_OWNERDRAW);
    m_btnOK.setDefault(true);
    return TRUE;
}

void ProfileEditDialog::OnOK()
{
    UpdateData(TRUE);
    if (m_name.IsEmpty() || m_host.IsEmpty())
        return;

    if (m_name.Find(L',') >= 0 || m_name.Find(L'"') >= 0) {
        MessageBox(L"Connection name cannot contain ',' or '\"'.",
                   L"Invalid Connection Name",
                   MB_OK | MB_ICONWARNING);
        return;
    }

    if (m_port < 1 || m_port > 65535)
        m_port = 3389;

    CDialogEx::OnOK();
}

void ProfileEditDialog::setProfile(const Profile &profile)
{
    m_profile = profile;
    m_name = profile.name.c_str();
    m_host = profile.host.c_str();
    m_port = profile.port;
    m_domain = profile.domain.c_str();
    m_username = profile.username.c_str();
    m_password = profile.password.c_str();
    m_clipboardEnabled = profile.clipboardEnabled ? TRUE : FALSE;
    m_ignoreCertificate = profile.ignoreCertificate ? TRUE : FALSE;
    m_fullScreenOnConnect = profile.fullScreenOnConnect ? TRUE : FALSE;
}

Profile ProfileEditDialog::profile() const
{
    Profile p = m_profile;
    p.name = static_cast<LPCWSTR>(m_name);
    p.host = static_cast<LPCWSTR>(m_host);
    p.port = m_port;
    p.domain = static_cast<LPCWSTR>(m_domain);
    p.username = static_cast<LPCWSTR>(m_username);
    p.password = static_cast<LPCWSTR>(m_password);
    p.clipboardEnabled = m_clipboardEnabled != FALSE;
    p.ignoreCertificate = m_ignoreCertificate != FALSE;
    p.fullScreenOnConnect = m_fullScreenOnConnect != FALSE;
    return p;
}
