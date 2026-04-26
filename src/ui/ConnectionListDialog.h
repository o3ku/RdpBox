#pragma once

#include <afxcmn.h>
#include <afxdialogex.h>
#include <afxwin.h>

#include "profiles/Profile.h"

#include <string>
#include <vector>

class ProfileRepository;

class ConnectionListDialog : public CDialogEx
{
    DECLARE_DYNAMIC(ConnectionListDialog)

public:
    ConnectionListDialog(ProfileRepository *repo, CWnd *parent = nullptr);
    virtual ~ConnectionListDialog();

    std::vector<std::string> selectedProfileIds() const;
    void setSelectionRequired(bool required);

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

    DECLARE_MESSAGE_MAP()

private:
    void refreshList(const std::vector<Profile> &profiles);
    void updateButtonStates();
    Profile currentProfile() const;
    std::vector<int> selectedIndices() const;

    ProfileRepository *m_repo = nullptr;
    std::vector<Profile> m_currentProfiles;
    std::vector<std::string> m_selectedProfileIds;
    CString m_searchText;
    bool m_selectionRequired = false;
};

