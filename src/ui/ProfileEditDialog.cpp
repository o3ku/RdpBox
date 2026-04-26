#include "ProfileEditDialog.h"

#include "resources/resource.h"

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
    DDX_Text(dx, IDC_PROFILE_USERNAME, m_username);
    DDX_Text(dx, IDC_PROFILE_PASSWORD, m_password);
    DDX_Check(dx, IDC_PROFILE_CLIPBOARD, m_clipboardEnabled);
    DDX_Check(dx, IDC_PROFILE_IGNORE_CERT, m_ignoreCertificate);
}

BOOL ProfileEditDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    return TRUE;
}

void ProfileEditDialog::OnOK()
{
    UpdateData(TRUE);
    if (m_name.IsEmpty() || m_host.IsEmpty())
        return;

    CDialogEx::OnOK();
}

void ProfileEditDialog::setProfile(const Profile &profile)
{
    m_profile = profile;
    m_name = profile.name.c_str();
    m_host = profile.host.c_str();
    m_port = profile.port;
    m_username = profile.username.c_str();
    m_password = profile.password.c_str();
    m_clipboardEnabled = profile.clipboardEnabled ? TRUE : FALSE;
    m_ignoreCertificate = profile.ignoreCertificate ? TRUE : FALSE;
}

Profile ProfileEditDialog::profile() const
{
    Profile p = m_profile;
    p.name = static_cast<LPCWSTR>(m_name);
    p.host = static_cast<LPCWSTR>(m_host);
    p.port = m_port;
    p.username = static_cast<LPCWSTR>(m_username);
    p.password = static_cast<LPCWSTR>(m_password);
    p.clipboardEnabled = m_clipboardEnabled != FALSE;
    p.ignoreCertificate = m_ignoreCertificate != FALSE;
    return p;
}
