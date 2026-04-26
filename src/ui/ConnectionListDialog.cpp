#include "ConnectionListDialog.h"

#include "ProfileEditDialog.h"

#include "profiles/ProfileRepository.h"

#include "resources/resource.h"

#include <algorithm>

IMPLEMENT_DYNAMIC(ConnectionListDialog, CDialogEx)

BEGIN_MESSAGE_MAP(ConnectionListDialog, CDialogEx)
    ON_EN_CHANGE(IDC_CONNECTION_SEARCH, &ConnectionListDialog::OnSearchChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_CONNECTION_LIST, &ConnectionListDialog::OnItemDoubleClicked)
    ON_BN_CLICKED(IDC_CONNECTION_NEW, &ConnectionListDialog::OnNewClicked)
    ON_BN_CLICKED(IDC_CONNECTION_EDIT, &ConnectionListDialog::OnEditClicked)
    ON_BN_CLICKED(IDC_CONNECTION_DELETE, &ConnectionListDialog::OnDeleteClicked)
    ON_BN_CLICKED(IDC_CONNECTION_CONNECT, &ConnectionListDialog::OnConnectClicked)
    ON_BN_CLICKED(IDC_CONNECTION_DUPLICATE, &ConnectionListDialog::OnDuplicateClicked)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_CONNECTION_LIST, &ConnectionListDialog::OnItemChanged)
END_MESSAGE_MAP()

ConnectionListDialog::ConnectionListDialog(ProfileRepository *repo, CWnd *parent)
    : CDialogEx(IDD_CONNECTION_DIALOG, parent)
    , m_repo(repo)
{
}

ConnectionListDialog::~ConnectionListDialog() = default;

void ConnectionListDialog::DoDataExchange(CDataExchange *dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Text(dx, IDC_CONNECTION_SEARCH, m_searchText);
}

BOOL ConnectionListDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    if (list) {
        list->SetExtendedStyle(list->GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        list->InsertColumn(0, L"Name", LVCFMT_LEFT, 150);
        list->InsertColumn(1, L"Host", LVCFMT_LEFT, 180);
        list->InsertColumn(2, L"Port", LVCFMT_LEFT, 60);
    }

    if (m_repo)
        refreshList(m_repo->profiles());

    updateButtonStates();
    return TRUE;
}

void ConnectionListDialog::OnOK()
{
    OnConnectClicked();
}

void ConnectionListDialog::OnCancel()
{
    if (m_selectionRequired)
        return;

    CDialogEx::OnCancel();
}

void ConnectionListDialog::OnClose()
{
    if (m_selectionRequired)
        return;

    CDialogEx::OnCancel();
}

void ConnectionListDialog::OnSearchChanged()
{
    UpdateData(TRUE);
    if (m_repo)
        refreshList(m_repo->search(m_searchText.GetString()));
}

void ConnectionListDialog::OnItemDoubleClicked(NMHDR *notify, LRESULT *result)
{
    UNREFERENCED_PARAMETER(result);
    auto *nmItem = reinterpret_cast<NMITEMACTIVATE*>(notify);
    if (nmItem && nmItem->iItem >= 0)
        OnConnectClicked();
}

void ConnectionListDialog::OnNewClicked()
{
    ProfileEditDialog dialog(this);
    if (dialog.DoModal() != IDOK)
        return;

    Profile p = dialog.profile();
    if (p.id.empty())
        p.id = createGuidString();

    if (m_repo) {
        m_repo->addProfile(p);
        refreshList(m_repo->search(m_searchText.GetString()));
    }
}

void ConnectionListDialog::OnEditClicked()
{
    const Profile cur = currentProfile();
    if (cur.id.empty())
        return;

    ProfileEditDialog dialog(this);
    dialog.setProfile(cur);
    if (dialog.DoModal() != IDOK)
        return;

    if (m_repo) {
        m_repo->updateProfile(dialog.profile());
        refreshList(m_repo->search(m_searchText.GetString()));
    }
}

void ConnectionListDialog::OnDeleteClicked()
{
    const auto indices = selectedIndices();
    if (indices.empty())
        return;

    const int count = static_cast<int>(indices.size());
    CString message;
    if (count == 1)
        message = L"Delete this connection?";
    else
        message.Format(L"Delete %d connections?", count);

    if (MessageBox(message, L"Delete Connection", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    if (!m_repo)
        return;

    std::vector<std::string> ids;
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(m_currentProfiles.size()))
            ids.push_back(m_currentProfiles[idx].id);
    }
    for (const std::string &id : ids)
        m_repo->removeProfile(id);

    refreshList(m_repo->search(m_searchText.GetString()));
}

void ConnectionListDialog::OnConnectClicked()
{
    const auto indices = selectedIndices();
    if (indices.empty())
        return;

    if (!m_repo)
        return;

    m_selectedProfileIds.clear();
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(m_currentProfiles.size()))
            m_selectedProfileIds.push_back(m_currentProfiles[idx].id);
    }

    if (m_selectedProfileIds.empty())
        return;

    CDialogEx::OnOK();
}

void ConnectionListDialog::OnDuplicateClicked()
{
    const auto indices = selectedIndices();
    if (indices.empty() || !m_repo)
        return;

    m_currentProfiles = m_repo->search(m_searchText.GetString());
    for (int idx : indices) {
        if (idx < 0 || idx >= static_cast<int>(m_currentProfiles.size()))
            continue;

        Profile dup = m_currentProfiles[idx];
        dup.id = createGuidString();
        if (!dup.name.empty())
            dup.name += L"(n)";
        else
            dup.name = L"(unnamed)";

        m_repo->addProfile(dup);
    }

    refreshList(m_repo->search(m_searchText.GetString()));
}

void ConnectionListDialog::OnItemChanged(NMHDR *notify, LRESULT *result)
{
    UNREFERENCED_PARAMETER(notify);
    UNREFERENCED_PARAMETER(result);
    updateButtonStates();
}

std::vector<std::string> ConnectionListDialog::selectedProfileIds() const
{
    return m_selectedProfileIds;
}

void ConnectionListDialog::setSelectionRequired(bool required)
{
    m_selectionRequired = required;
}

void ConnectionListDialog::refreshList(const std::vector<Profile> &profiles)
{
    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list)
        return;

    m_currentProfiles = profiles;
    list->SetRedraw(FALSE);
    list->DeleteAllItems();

    for (int idx = 0; idx < static_cast<int>(profiles.size()); ++idx) {
        const auto &p = profiles[idx];
        const CString name(p.name.c_str());
        const CString host(p.host.c_str());
        CString portStr;
        portStr.Format(L"%d", p.port);

        list->InsertItem(idx, name);
        list->SetItemText(idx, 1, host);
        list->SetItemText(idx, 2, portStr);
        list->SetItemData(idx, static_cast<DWORD_PTR>(idx));
    }

    if (list->GetItemCount() > 0 && list->GetSelectedCount() == 0)
        list->SetItemState(0, LVIS_SELECTED, LVIS_SELECTED);

    list->SetRedraw(TRUE);
    updateButtonStates();
}

void ConnectionListDialog::updateButtonStates()
{
    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    const int selectedCount = list ? list->GetSelectedCount() : 0;

    CWnd *editButton = GetDlgItem(IDC_CONNECTION_EDIT);
    CWnd *deleteButton = GetDlgItem(IDC_CONNECTION_DELETE);
    CWnd *connectButton = GetDlgItem(IDC_CONNECTION_CONNECT);
    CWnd *duplicateButton = GetDlgItem(IDC_CONNECTION_DUPLICATE);

    if (editButton)
        editButton->EnableWindow(selectedCount == 1);
    if (deleteButton)
        deleteButton->EnableWindow(selectedCount > 0);
    if (connectButton)
        connectButton->EnableWindow(selectedCount > 0);
    if (duplicateButton)
        duplicateButton->EnableWindow(selectedCount > 0);
}

Profile ConnectionListDialog::currentProfile() const
{
    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list)
        return {};

    POSITION pos = list->GetFirstSelectedItemPosition();
    if (!pos)
        return {};

    const int idx = list->GetNextSelectedItem(pos);
    if (idx < 0 || idx >= static_cast<int>(m_currentProfiles.size()))
        return {};

    return m_currentProfiles[idx];
}

std::vector<int> ConnectionListDialog::selectedIndices() const
{
    std::vector<int> indices;
    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list)
        return indices;

    POSITION pos = list->GetFirstSelectedItemPosition();
    while (pos) {
        const int idx = list->GetNextSelectedItem(pos);
        indices.push_back(idx);
    }
    return indices;
}
