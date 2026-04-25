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
    m_name = CString(profile.name.toStdWString().c_str());
    m_host = CString(profile.host.toStdWString().c_str());
    m_port = profile.port;
    m_username = CString(profile.username.toStdWString().c_str());
    m_password = CString(profile.password.toStdWString().c_str());
    m_clipboardEnabled = profile.clipboardEnabled ? TRUE : FALSE;
    m_ignoreCertificate = profile.ignoreCertificate ? TRUE : FALSE;
}

Profile ProfileEditDialog::profile() const
{
    Profile p = m_profile;
    p.name = QString::fromWCharArray(m_name.GetString());
    p.host = QString::fromWCharArray(m_host.GetString());
    p.port = m_port;
    p.username = QString::fromWCharArray(m_username.GetString());
    p.password = QString::fromWCharArray(m_password.GetString());
    p.clipboardEnabled = m_clipboardEnabled != FALSE;
    p.ignoreCertificate = m_ignoreCertificate != FALSE;
    return p;
}
