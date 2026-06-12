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
class QToolButton;
class QTabWidget;
class QVBoxLayout;
class QWidget;

class QtMainWindow : public QMainWindow
{
public:
    explicit QtMainWindow(std::vector<std::wstring> startupConnectionNames,
                          QWidget *parent = nullptr);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void buildTitleBar(QVBoxLayout *rootLayout);
    void refreshProfileList();
    void refreshActions();
    void refreshWindowControls();
    void configureHomeTab();
    int nativeHitTestForPoint(const QPoint &windowPoint) const;
    void addProfile();
    void editSelectedProfile();
    void deleteSelectedProfile();
    void closeSessionTab(int index);
    void connectSelectedProfile();
    void openConnectionsByName(const std::vector<std::wstring> &connectionNames);
    Profile selectedProfile() const;
    std::wstring selectedProfileName() const;
    void addSessionTab(const Profile &profile);
    QWidget *createHomePage() const;
    QWidget *createSessionPage(const Profile &profile) const;
    std::vector<QRect> captionExclusionRects() const;

    ProfileRepository m_repository;
    std::vector<std::wstring> m_startupConnectionNames;
    QWidget *m_titleBar = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_profileList = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_statusLabel = nullptr;
};
