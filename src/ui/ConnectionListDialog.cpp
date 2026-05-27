#include "ConnectionListDialog.h"

#include "ProfileEditDialog.h"

#include "profiles/ProfileRepository.h"

#include "resources/resource.h"

#include <algorithm>

#include <uxtheme.h>

IMPLEMENT_DYNAMIC(ConnectionListDialog, CDialogEx)

BEGIN_MESSAGE_MAP(ConnectionListDialog, CDialogEx)
    ON_EN_CHANGE(IDC_CONNECTION_SEARCH, &ConnectionListDialog::OnSearchChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_CONNECTION_LIST, &ConnectionListDialog::OnItemDoubleClicked)
    ON_NOTIFY(LVN_BEGINDRAG, IDC_CONNECTION_LIST, &ConnectionListDialog::OnBeginDragList)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_CONNECTION_LIST, &ConnectionListDialog::OnCustomDrawList)
    ON_BN_CLICKED(IDC_CONNECTION_NEW, &ConnectionListDialog::OnNewClicked)
    ON_BN_CLICKED(IDC_CONNECTION_EDIT, &ConnectionListDialog::OnEditClicked)
    ON_BN_CLICKED(IDC_CONNECTION_DELETE, &ConnectionListDialog::OnDeleteClicked)
    ON_BN_CLICKED(IDC_CONNECTION_CONNECT, &ConnectionListDialog::OnConnectClicked)
    ON_BN_CLICKED(IDC_CONNECTION_DUPLICATE, &ConnectionListDialog::OnDuplicateClicked)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_CONNECTION_LIST, &ConnectionListDialog::OnItemChanged)
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

ConnectionListDialog::ConnectionListDialog(ProfileRepository *repo,
                                             const std::vector<std::wstring> &connectedProfileNames,
                                             CWnd *parent)
    : CDialogEx(IDD_CONNECTION_DIALOG, parent)
    , m_repo(repo)
    , m_connectedProfileNames(connectedProfileNames)
{
}

ConnectionListDialog::~ConnectionListDialog() = default;

BOOL ConnectionListDialog::PreTranslateMessage(MSG *msg)
{
    if (msg && msg->message == WM_KEYDOWN && !m_draggingList) {
        if (msg->wParam == VK_UP && moveCurrentSelectionBy(-1))
            return TRUE;
        if (msg->wParam == VK_DOWN && moveCurrentSelectionBy(1))
            return TRUE;
    }

    return CDialogEx::PreTranslateMessage(msg);
}

void ConnectionListDialog::DoDataExchange(CDataExchange *dx)
{
    CDialogEx::DoDataExchange(dx);
    DDX_Text(dx, IDC_CONNECTION_SEARCH, m_searchText);
    DDX_Control(dx, IDC_CONNECTION_NEW, m_btnNew);
    DDX_Control(dx, IDC_CONNECTION_EDIT, m_btnEdit);
    DDX_Control(dx, IDC_CONNECTION_DUPLICATE, m_btnDuplicate);
    DDX_Control(dx, IDC_CONNECTION_DELETE, m_btnDelete);
    DDX_Control(dx, IDC_CONNECTION_CONNECT, m_btnConnect);
}

namespace
{
void makeFlatOwnerDraw(FlatButton &button)
{
    if (button.GetSafeHwnd())
        button.ModifyStyle(BS_DEFPUSHBUTTON, BS_OWNERDRAW);
}

bool isConnected(const std::wstring &profileName, const std::vector<std::wstring> &connectedNames)
{
    return std::find(connectedNames.begin(), connectedNames.end(), profileName) != connectedNames.end();
}
}

BOOL ConnectionListDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    EnableThemeDialogTexture(GetSafeHwnd(), ETDT_ENABLETAB);

    makeFlatOwnerDraw(m_btnNew);
    makeFlatOwnerDraw(m_btnEdit);
    makeFlatOwnerDraw(m_btnDuplicate);
    makeFlatOwnerDraw(m_btnDelete);
    makeFlatOwnerDraw(m_btnConnect);
    m_btnConnect.setDefault(true);

    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    if (list) {
        list->ModifyStyle(LVS_SINGLESEL, 0);
        list->SetExtendedStyle(list->GetExtendedStyle()
            | LVS_EX_FULLROWSELECT
            | LVS_EX_DOUBLEBUFFER
            | LVS_EX_HEADERINALLVIEWS
            | LVS_EX_INFOTIP);
        ::SetWindowTheme(list->GetSafeHwnd(), L"Explorer", nullptr);
        list->InsertColumn(0, L"Name", LVCFMT_LEFT, 150);
        list->InsertColumn(1, L"Host", LVCFMT_LEFT, 180);
        list->InsertColumn(2, L"Port", LVCFMT_LEFT, 60);
        list->InsertColumn(3, L"Status", LVCFMT_LEFT, 80);
    }

    if (CEdit *search = static_cast<CEdit *>(GetDlgItem(IDC_CONNECTION_SEARCH)))
        search->SetCueBanner(L"Search profiles...", TRUE);

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
    if (m_draggingList)
        endListDrag(false);
    CDialogEx::OnCancel();
}

void ConnectionListDialog::OnClose()
{
    if (m_draggingList)
        endListDrag(false);
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

    if (m_repo) {
        if (!m_repo->addProfile(dialog.profile())) {
            showNameConflictMessage(dialog.profile().name.c_str());
            return;
        }
        refreshList(m_repo->search(m_searchText.GetString()));
    }
}

void ConnectionListDialog::OnEditClicked()
{
    const Profile cur = currentProfile();
    if (!cur.isValid())
        return;

    ProfileEditDialog dialog(this);
    dialog.setProfile(cur);
    if (dialog.DoModal() != IDOK)
        return;

    if (m_repo) {
        if (!m_repo->updateProfile(cur.name, dialog.profile())) {
            showNameConflictMessage(dialog.profile().name.c_str());
            return;
        }
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

    std::vector<std::wstring> names;
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(m_currentProfiles.size()))
            names.push_back(m_currentProfiles[idx].name);
    }
    for (const std::wstring &name : names)
        m_repo->removeProfile(name);

    refreshList(m_repo->search(m_searchText.GetString()));
}

void ConnectionListDialog::OnConnectClicked()
{
    const auto indices = selectedIndices();
    if (indices.empty())
        return;

    if (!m_repo)
        return;

    m_selectedProfileNames.clear();
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(m_currentProfiles.size())) {
            const auto &profileName = m_currentProfiles[idx].name;
            if (!isConnected(profileName, m_connectedProfileNames))
                m_selectedProfileNames.push_back(profileName);
        }
    }

    if (m_selectedProfileNames.empty())
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
        if (!dup.name.empty())
            dup.name += L"(n)";
        else
            dup.name = L"(unnamed)";

        if (!m_repo->addProfile(dup))
            showNameConflictMessage(dup.name.c_str());
    }

    refreshList(m_repo->search(m_searchText.GetString()));
}

void ConnectionListDialog::OnItemChanged(NMHDR *notify, LRESULT *result)
{
    UNREFERENCED_PARAMETER(notify);
    UNREFERENCED_PARAMETER(result);
    updateButtonStates();
}

void ConnectionListDialog::OnBeginDragList(NMHDR *notify, LRESULT *result)
{
    auto *drag = reinterpret_cast<NMLISTVIEW *>(notify);
    if (result)
        *result = 0;
    if (!drag || drag->iItem < 0)
        return;

    CPoint point;
    ::GetCursorPos(&point);
    beginListDrag(drag->iItem, point);
}

std::vector<std::wstring> ConnectionListDialog::selectedProfileNames() const
{
    return m_selectedProfileNames;
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
        list->SetItemText(idx, 3, isConnected(p.name, m_connectedProfileNames) ? L"Connected" : L"");
        list->SetItemData(idx, static_cast<DWORD_PTR>(idx));
    }

    if (list->GetItemCount() > 0 && list->GetSelectedCount() == 0) {
        list->SetItemState(0,
                           LVIS_SELECTED | LVIS_FOCUSED,
                           LVIS_SELECTED | LVIS_FOCUSED);
        list->SetSelectionMark(0);
    }

    list->SetRedraw(TRUE);
    updateButtonStates();
}

void ConnectionListDialog::updateButtonStates()
{
    CListCtrl *list = static_cast<CListCtrl*>(GetDlgItem(IDC_CONNECTION_LIST));
    const int selectedCount = list ? list->GetSelectedCount() : 0;

    bool allConnected = true;
    if (list && selectedCount > 0) {
        POSITION pos = list->GetFirstSelectedItemPosition();
        while (pos) {
            const int idx = list->GetNextSelectedItem(pos);
            if (idx >= 0 && idx < static_cast<int>(m_currentProfiles.size())
                && !isConnected(m_currentProfiles[idx].name, m_connectedProfileNames)) {
                allConnected = false;
                break;
            }
        }
    } else {
        allConnected = false;
    }

    CWnd *editButton = GetDlgItem(IDC_CONNECTION_EDIT);
    CWnd *deleteButton = GetDlgItem(IDC_CONNECTION_DELETE);
    CWnd *connectButton = GetDlgItem(IDC_CONNECTION_CONNECT);
    CWnd *duplicateButton = GetDlgItem(IDC_CONNECTION_DUPLICATE);

    if (editButton)
        editButton->EnableWindow(selectedCount == 1);
    if (deleteButton)
        deleteButton->EnableWindow(selectedCount > 0);
    if (connectButton)
        connectButton->EnableWindow(selectedCount > 0 && !allConnected);
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

void ConnectionListDialog::showNameConflictMessage(const CString &name)
{
    CString message;
    message.Format(L"Connection name \"%s\" already exists.", static_cast<LPCWSTR>(name));
    MessageBox(message, L"Duplicate Connection Name", MB_OK | MB_ICONWARNING);
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

void ConnectionListDialog::OnMouseMove(UINT flags, CPoint point)
{
    if (m_draggingList) {
        CPoint screenPoint(point);
        ClientToScreen(&screenPoint);
        updateListDrag(screenPoint);
        return;
    }

    CDialogEx::OnMouseMove(flags, point);
}

void ConnectionListDialog::OnLButtonUp(UINT flags, CPoint point)
{
    if (m_draggingList) {
        endListDrag(true);
        return;
    }

    CDialogEx::OnLButtonUp(flags, point);
}

void ConnectionListDialog::beginListDrag(int itemIndex, CPoint point)
{
    if (!m_repo || itemIndex < 0 || itemIndex >= static_cast<int>(m_currentProfiles.size()))
        return;

    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list)
        return;

    m_draggingList = true;
    m_dragSourceIndex = itemIndex;
    m_dragInsertIndex = itemIndex;
    m_dragImage = list->CreateDragImage(itemIndex, nullptr);
    if (m_dragImage) {
        m_dragImage->BeginDrag(0, CPoint(12, 12));
        m_dragImage->DragEnter(GetDesktopWindow(), point);
    }
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
    SetCapture();
    updateListDrag(point);
}

void ConnectionListDialog::updateListDrag(CPoint screenPoint)
{
    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list || !m_draggingList)
        return;

    m_dragInsertIndex = insertIndexForScreenPoint(screenPoint);
    if (m_dragImage)
        m_dragImage->DragMove(screenPoint);
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
    list->Invalidate(FALSE);

    LVINSERTMARK mark = {};
    if (list->GetItemCount() <= 0) {
        ListView_SetInsertMark(list->GetSafeHwnd(), &mark);
        return;
    }

    if (m_dragInsertIndex <= 0) {
        mark.iItem = 0;
        mark.dwFlags = 0;
    } else if (m_dragInsertIndex >= list->GetItemCount()) {
        mark.iItem = list->GetItemCount() - 1;
        mark.dwFlags = LVIM_AFTER;
    } else {
        mark.iItem = m_dragInsertIndex;
        mark.dwFlags = 0;
    }

    ListView_SetInsertMark(list->GetSafeHwnd(), &mark);
}

void ConnectionListDialog::endListDrag(bool commit)
{
    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (list) {
        LVINSERTMARK clearMark = {};
        ListView_SetInsertMark(list->GetSafeHwnd(), &clearMark);
        list->Invalidate(FALSE);
    }

    if (m_dragImage) {
        m_dragImage->DragLeave(GetDesktopWindow());
        m_dragImage->EndDrag();
        delete m_dragImage;
        m_dragImage = nullptr;
    }

    if (GetCapture() == this)
        ReleaseCapture();

    const bool canCommit =
        commit && m_repo
        && m_dragSourceIndex >= 0
        && m_dragSourceIndex < static_cast<int>(m_currentProfiles.size())
        && m_dragInsertIndex >= 0;

    std::wstring movedName;
    if (canCommit)
        movedName = m_currentProfiles[static_cast<size_t>(m_dragSourceIndex)].name;

    if (canCommit) {
        const std::size_t targetIndex = repositoryTargetIndexForVisibleInsertIndex(m_dragInsertIndex);
        if (m_repo->moveProfile(movedName, targetIndex)) {
            refreshList(m_repo->search(m_searchText.GetString()));
            selectProfileByName(movedName);
        }
    }

    m_draggingList = false;
    m_dragSourceIndex = -1;
    m_dragInsertIndex = -1;
}

void ConnectionListDialog::OnCustomDrawList(NMHDR *notify, LRESULT *result)
{
    auto *customDraw = reinterpret_cast<NMLVCUSTOMDRAW *>(notify);
    if (!customDraw || !result)
        return;

    *result = CDRF_DODEFAULT;
    if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        *result = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
        return;
    }

    if (customDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        if (m_draggingList && static_cast<int>(customDraw->nmcd.dwItemSpec) == m_dragSourceIndex) {
            *result = CDRF_NOTIFYPOSTPAINT;
            return;
        }
        return;
    }

    if (customDraw->nmcd.dwDrawStage != CDDS_POSTPAINT || !m_draggingList)
        return;

    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list)
        return;

    CDC *dc = CDC::FromHandle(customDraw->nmcd.hdc);
    if (!dc)
        return;

    drawDragInsertMarker(*dc, *list);

    if (static_cast<int>(customDraw->nmcd.dwItemSpec) == m_dragSourceIndex) {
        CRect itemRect;
        if (list->GetItemRect(m_dragSourceIndex, &itemRect, LVIR_BOUNDS)) {
            dc->Draw3dRect(itemRect, RGB(0, 120, 215), RGB(0, 120, 215));
        }
    }
}

int ConnectionListDialog::insertIndexForScreenPoint(CPoint screenPoint) const
{
    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list || list->GetItemCount() <= 0)
        return 0;

    CPoint clientPoint(screenPoint);
    list->ScreenToClient(&clientPoint);

    LVHITTESTINFO hit = {};
    hit.pt = clientPoint;
    const int item = ListView_HitTest(list->GetSafeHwnd(), &hit);
    if (item < 0) {
        if (clientPoint.y < 0)
            return 0;
        return list->GetItemCount();
    }

    CRect itemRect;
    list->GetItemRect(item, &itemRect, LVIR_BOUNDS);
    const int topThreshold = itemRect.top + itemRect.Height() / 3;
    const int bottomThreshold = itemRect.bottom - itemRect.Height() / 3;

    if (m_dragSourceIndex >= 0) {
        if (item > m_dragSourceIndex)
            return clientPoint.y < topThreshold ? item : item + 1;
        if (item < m_dragSourceIndex)
            return clientPoint.y > bottomThreshold ? item + 1 : item;
    }

    return clientPoint.y < itemRect.CenterPoint().y ? item : item + 1;
}

std::size_t ConnectionListDialog::repositoryTargetIndexForVisibleInsertIndex(int insertIndex) const
{
    if (!m_repo || m_currentProfiles.empty())
        return 0;

    const auto &profiles = m_repo->profiles();
    auto findFullIndex = [&](const std::wstring &name) {
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            if (profiles[i].name == name)
                return i;
        }
        return profiles.size();
    };

    if (insertIndex <= 0)
        return findFullIndex(m_currentProfiles.front().name);
    if (insertIndex >= static_cast<int>(m_currentProfiles.size())) {
        const std::size_t lastIndex = findFullIndex(m_currentProfiles.back().name);
        return lastIndex >= profiles.size() ? profiles.size() : lastIndex + 1;
    }

    return findFullIndex(m_currentProfiles[static_cast<std::size_t>(insertIndex)].name);
}

void ConnectionListDialog::selectProfileByName(const std::wstring &name)
{
    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list)
        return;

    for (int i = 0; i < list->GetItemCount(); ++i) {
        list->SetItemState(i, 0, LVIS_SELECTED | LVIS_FOCUSED);
    }

    for (int i = 0; i < static_cast<int>(m_currentProfiles.size()); ++i) {
        if (m_currentProfiles[static_cast<std::size_t>(i)].name != name)
            continue;

        list->SetItemState(i,
                           LVIS_SELECTED | LVIS_FOCUSED,
                           LVIS_SELECTED | LVIS_FOCUSED);
        list->SetSelectionMark(i);
        list->EnsureVisible(i, FALSE);
        return;
    }
}

void ConnectionListDialog::drawDragInsertMarker(CDC &dc, CListCtrl &list) const
{
    if (m_dragInsertIndex < 0)
        return;

    CRect clientRect;
    list.GetClientRect(&clientRect);
    if (clientRect.IsRectEmpty())
        return;

    int y = clientRect.top + 2;
    const int itemCount = list.GetItemCount();
    if (itemCount > 0) {
        if (m_dragInsertIndex <= 0) {
            CRect itemRect;
            list.GetItemRect(0, &itemRect, LVIR_BOUNDS);
            y = itemRect.top + 1;
        } else if (m_dragInsertIndex >= itemCount) {
            CRect itemRect;
            list.GetItemRect(itemCount - 1, &itemRect, LVIR_BOUNDS);
            y = itemRect.bottom - 1;
        } else {
            CRect itemRect;
            list.GetItemRect(m_dragInsertIndex, &itemRect, LVIR_BOUNDS);
            y = itemRect.top;
        }
    }

    const COLORREF markerColor = RGB(0, 120, 215);
    CBrush brush(markerColor);
    CRect lineRect(clientRect.left, y - 1, clientRect.right, y + 2);
    dc.FillRect(&lineRect, &brush);
}

bool ConnectionListDialog::moveCurrentSelectionBy(int delta)
{
    if (delta == 0)
        return false;

    CListCtrl *list = static_cast<CListCtrl *>(GetDlgItem(IDC_CONNECTION_LIST));
    if (!list || list->GetItemCount() <= 0)
        return false;

    int currentIndex = list->GetSelectionMark();
    if (currentIndex < 0) {
        POSITION pos = list->GetFirstSelectedItemPosition();
        if (pos)
            currentIndex = list->GetNextSelectedItem(pos);
    }
    if (currentIndex < 0 || currentIndex >= static_cast<int>(m_currentProfiles.size()))
        return false;

    const int targetIndex = currentIndex + delta;
    if (targetIndex < 0 || targetIndex >= static_cast<int>(m_currentProfiles.size()))
        return false;

    for (int i = 0; i < list->GetItemCount(); ++i)
        list->SetItemState(i, 0, LVIS_SELECTED | LVIS_FOCUSED);

    list->SetItemState(targetIndex,
                       LVIS_SELECTED | LVIS_FOCUSED,
                       LVIS_SELECTED | LVIS_FOCUSED);
    list->SetSelectionMark(targetIndex);
    list->EnsureVisible(targetIndex, FALSE);
    updateButtonStates();
    return true;
}
