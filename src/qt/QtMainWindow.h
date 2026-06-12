#pragma once

#include "profiles/Profile.h"
#include "profiles/ProfileRepository.h"

#include <QMainWindow>

#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabWidget;

class QtMainWindow : public QMainWindow
{
public:
    explicit QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                          QWidget *parent = nullptr);

private:
    void buildUi();
    void refreshProfileList();
    void refreshActions();
    void addProfile();
    void editSelectedProfile();
    void deleteSelectedProfile();
    void connectSelectedProfile();
    void openConnectionsByName(const std::vector<std::wstring> &connectionNames);
    Profile selectedProfile() const;
    std::wstring selectedProfileName() const;
    void addSessionTab(const Profile &profile);
    QWidget *createHomePage() const;
    QWidget *createSessionPage(const Profile &profile) const;

    ProfileRepository m_repository;
    std::vector<std::wstring> m_startupConnectionNames;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_profileList = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_statusLabel = nullptr;
};
