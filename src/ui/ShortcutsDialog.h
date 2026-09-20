#pragma once

#include <afxdialogex.h>

#include "ui/MainWindowShortcuts.h"

// Registry-backed persistence for the configurable main-window shortcuts.
// Falls back to defaults for missing or invalid entries.
ui::MainWindowShortcutSettings loadMainWindowShortcutSettings();
void saveMainWindowShortcutSettings(const ui::MainWindowShortcutSettings &settings);

class ShortcutsDialog : public CDialogEx
{
    DECLARE_DYNAMIC(ShortcutsDialog)

public:
    explicit ShortcutsDialog(const ui::MainWindowShortcutSettings &settings, CWnd *parent = nullptr);

    ui::MainWindowShortcutSettings settings() const { return m_settings; }

protected:
    BOOL PreTranslateMessage(MSG *msg) override;
    BOOL OnInitDialog() override;
    void DoDataExchange(CDataExchange *dx) override;
    void OnOK() override;

    DECLARE_MESSAGE_MAP()

private:
    struct CaptureEdit
    {
        UINT controlId = 0;
        ui::ShortcutChord *chord = nullptr;
    };

    CaptureEdit *captureEditForFocus();
    void captureKey(UINT virtualKey);
    void refreshEditTexts();
    bool validateAndReport();

    ui::MainWindowShortcutSettings m_settings;
    CaptureEdit m_captures[3];
};
