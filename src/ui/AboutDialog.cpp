#include "AboutDialog.h"

#include "resources/resource.h"

#include <algorithm>

#include <shellapi.h>
#include <uxtheme.h>

IMPLEMENT_DYNAMIC(AboutDialog, CDialogEx)

namespace
{
CString wideFromBuildDate(const char *buildDate)
{
    if (!buildDate || !*buildDate)
        return {};

    const int length = ::MultiByteToWideChar(CP_ACP, 0, buildDate, -1, nullptr, 0);
    if (length <= 1)
        return {};

    CString result;
    wchar_t *buffer = result.GetBuffer(length - 1);
    ::MultiByteToWideChar(CP_ACP, 0, buildDate, -1, buffer, length);
    result.ReleaseBuffer();
    return result;
}
}

BEGIN_MESSAGE_MAP(AboutDialog, CDialogEx)
    ON_WM_CTLCOLOR()
    ON_STN_CLICKED(IDC_ABOUT_REPO_VALUE, &AboutDialog::OnRepoClicked)
END_MESSAGE_MAP()

AboutDialog::AboutDialog(CWnd *parent)
    : CDialogEx(IDD_ABOUT_DIALOG, parent)
{
}

AboutDialog::~AboutDialog() = default;

void AboutDialog::DoDataExchange(CDataExchange *dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Text(dx, IDC_ABOUT_TITLE, m_titleText);
    DDX_Text(dx, IDC_ABOUT_RELEASED_VALUE, m_releaseDateText);
    DDX_Text(dx, IDC_ABOUT_REPO_VALUE, m_repoUrlText);
    DDX_Control(dx, IDOK, m_btnOK);
}

BOOL AboutDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    EnableThemeDialogTexture(GetSafeHwnd(), ETDT_ENABLETAB);

    if (m_btnOK.GetSafeHwnd())
        m_btnOK.ModifyStyle(BS_DEFPUSHBUTTON, BS_OWNERDRAW);
    m_btnOK.setDefault(true);

    if (CStatic *icon = static_cast<CStatic *>(GetDlgItem(IDC_ABOUT_ICON)))
    {
        HICON hIcon = static_cast<HICON>(
            LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(IDI_APP_ICON),
                      IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
        if (hIcon)
            icon->SetIcon(hIcon);
    }

    if (CStatic *link = static_cast<CStatic *>(GetDlgItem(IDC_ABOUT_REPO_VALUE)))
    {
        link->SetCursor(::LoadCursor(nullptr, IDC_HAND));

        LOGFONTW lf = {};
        if (HFONT hFont = reinterpret_cast<HFONT>(link->GetFont()->GetSafeHandle()))
            GetObjectW(hFont, sizeof(lf), &lf);
        lf.lfUnderline = TRUE;
        m_linkFont.CreateFontIndirectW(&lf);
        link->SetFont(&m_linkFont);
    }

    m_titleText.Format(L"RdpBox %s", RDPBOX_VERSION);
    m_releaseDateText = wideFromBuildDate(__DATE__);
    m_repoUrlText = RDPBOX_GITHUB_URL;
    UpdateData(FALSE);
    return TRUE;
}

HBRUSH AboutDialog::OnCtlColor(CDC *dc, CWnd *wnd, UINT ctrlType)
{
    HBRUSH brush = CDialogEx::OnCtlColor(dc, wnd, ctrlType);
    if (ctrlType == CTLCOLOR_STATIC && wnd->GetDlgCtrlID() == IDC_ABOUT_REPO_VALUE)
    {
        dc->SetTextColor(RGB(0, 90, 158));
        dc->SetBkMode(TRANSPARENT);
    }
    return brush;
}

void AboutDialog::OnRepoClicked()
{
    CString url = RDPBOX_GITHUB_URL;
    ::ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}
