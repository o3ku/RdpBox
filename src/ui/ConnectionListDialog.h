#pragma once

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>

#include "profiles/Profile.h"
#include "ui/FlatButton.h"

#include <string>
#include <vector>

class ProfileRepository;
class CImageList;

class ConnectionListDialog : public CDialogEx
{
    DECLARE_DYNAMIC(ConnectionListDialog)

public:
    ConnectionListDialog(ProfileRepository *repo,
                         const std::vector<std::wstring> &connectedProfileNames,
                         CWnd *parent = nullptr);
    virtual ~ConnectionListDialog();

    std::vector<std::wstring> selectedProfileNames() const;
    BOOL PreTranslateMessage(MSG *msg) override;

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();
    virtual void OnClose();
    virtual void DoDataExchange(CDataExchange *dx);

    afx_msg void OnSearchChanged();
    afx_msg void OnItemDoubleClicked(NMHDR *notify, LRESULT *result);
    afx_msg void OnNewClicked();
    afx_msg void OnEditClicked();
    afx_msg void OnDeleteClicked();
    afx_msg void OnConnectClicked();
    afx_msg void OnDuplicateClicked();
    afx_msg void OnItemChanged(NMHDR *notify, LRESULT *result);
    afx_msg void OnBeginDragList(NMHDR *notify, LRESULT *result);
    afx_msg void OnCustomDrawList(NMHDR *notify, LRESULT *result);
    afx_msg void OnMouseMove(UINT flags, CPoint point);
    afx_msg void OnLButtonUp(UINT flags, CPoint point);

    DECLARE_MESSAGE_MAP()

private:
    void refreshList(const std::vector<Profile> &profiles);
    void updateButtonStates();
    Profile currentProfile() const;
    std::vector<int> selectedIndices() const;
    void showNameConflictMessage(const CString &name);
    void beginListDrag(int itemIndex, CPoint point);
    void updateListDrag(CPoint screenPoint);
    void endListDrag(bool commit);
    int insertIndexForScreenPoint(CPoint screenPoint) const;
    std::size_t repositoryTargetIndexForVisibleInsertIndex(int insertIndex) const;
    void selectProfileByName(const std::wstring &name);
    void drawDragInsertMarker(CDC &dc, CListCtrl &list) const;
    bool moveCurrentSelectionBy(int delta);

    ProfileRepository *m_repo = nullptr;
    std::vector<std::wstring> m_connectedProfileNames;
    std::vector<Profile> m_currentProfiles;
    std::vector<std::wstring> m_selectedProfileNames;
    CString m_searchText;

    FlatButton m_btnNew;
    FlatButton m_btnEdit;
    FlatButton m_btnDuplicate;
    FlatButton m_btnDelete;
    FlatButton m_btnConnect;
    bool m_draggingList = false;
    int m_dragSourceIndex = -1;
    int m_dragInsertIndex = -1;
    CImageList *m_dragImage = nullptr;
};
