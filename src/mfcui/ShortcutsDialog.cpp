#include "mfcui/ShortcutsDialog.h"

#include "resources/resource.h"

IMPLEMENT_DYNAMIC(ShortcutsDialog, CDialogEx)

BEGIN_MESSAGE_MAP(ShortcutsDialog, CDialogEx)
END_MESSAGE_MAP()

namespace
{
constexpr wchar_t kRegistrySection[] = L"Shortcuts";
constexpr int kModCtrl = 1;
constexpr int kModShift = 2;
constexpr int kModAlt = 4;

bool isModifierKey(UINT virtualKey)
{
    return virtualKey == VK_CONTROL || virtualKey == VK_SHIFT || virtualKey == VK_MENU;
}

int chordMods(const ui::ShortcutChord &chord)
{
    int mods = 0;
    if (chord.ctrl)
        mods |= kModCtrl;
    if (chord.shift)
        mods |= kModShift;
    if (chord.alt)
        mods |= kModAlt;
    return mods;
}

ui::ShortcutChord chordFromStorage(int virtualKey, int mods)
{
    ui::ShortcutChord chord;
    chord.virtualKey = static_cast<unsigned int>(virtualKey);
    chord.ctrl = (mods & kModCtrl) != 0;
    chord.shift = (mods & kModShift) != 0;
    chord.alt = (mods & kModAlt) != 0;
    return chord;
}
}

ui::MainWindowShortcutSettings loadMainWindowShortcutSettings()
{
    ui::MainWindowShortcutSettings settings;
    CWinApp *app = AfxGetApp();
    if (!app)
        return settings;

    const ui::ShortcutChord fallbacks[] = {
        settings.newConnection, settings.openConnections, settings.toggleFullScreen,
    };
    ui::ShortcutChord *targets[] = {
        &settings.newConnection, &settings.openConnections, &settings.toggleFullScreen,
    };
    const wchar_t *const names[] = {L"New", L"Open", L"FullScreen"};

    for (int i = 0; i < 3; ++i) {
        const int virtualKey = app->GetProfileInt(kRegistrySection,
                                                  (std::wstring(names[i]) + L"Vk").c_str(),
                                                  static_cast<int>(fallbacks[i].virtualKey));
        const int mods = app->GetProfileInt(kRegistrySection,
                                            (std::wstring(names[i]) + L"Mods").c_str(),
                                            chordMods(fallbacks[i]));
        const ui::ShortcutChord chord = chordFromStorage(virtualKey, mods);
        if (virtualKey == 0)
            *targets[i] = ui::ShortcutChord();  // explicitly disabled
        else if (ui::isValidShortcutChord(chord))
            *targets[i] = chord;
    }
    return settings;
}

void saveMainWindowShortcutSettings(const ui::MainWindowShortcutSettings &settings)
{
    CWinApp *app = AfxGetApp();
    if (!app)
        return;

    const ui::ShortcutChord chords[] = {
        settings.newConnection, settings.openConnections, settings.toggleFullScreen,
    };
    const wchar_t *const names[] = {L"New", L"Open", L"FullScreen"};
    for (int i = 0; i < 3; ++i) {
        app->WriteProfileInt(kRegistrySection,
                             (std::wstring(names[i]) + L"Vk").c_str(),
                             static_cast<int>(chords[i].virtualKey));
        app->WriteProfileInt(kRegistrySection,
                             (std::wstring(names[i]) + L"Mods").c_str(),
                             chordMods(chords[i]));
    }
}

ShortcutsDialog::ShortcutsDialog(const ui::MainWindowShortcutSettings &settings, CWnd *parent)
    : CDialogEx(IDD_SHORTCUTS_DIALOG, parent)
    , m_settings(settings)
{
    m_captures[0] = {IDC_SHORTCUT_NEW, &m_settings.newConnection};
    m_captures[1] = {IDC_SHORTCUT_OPEN, &m_settings.openConnections};
    m_captures[2] = {IDC_SHORTCUT_FULL, &m_settings.toggleFullScreen};
}

BOOL ShortcutsDialog::PreTranslateMessage(MSG *msg)
{
    if (msg && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)) {
        const UINT virtualKey = static_cast<UINT>(msg->wParam);
        CaptureEdit *capture = captureEditForFocus();
        if (capture && !isModifierKey(virtualKey)) {
            const bool modifierHeld = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
                || (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0
                || (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            if (!modifierHeld && (virtualKey == VK_BACK || virtualKey == VK_DELETE)) {
                // Bare Backspace/Delete clears the shortcut (disables the action).
                *capture->chord = ui::ShortcutChord();
                refreshEditTexts();
                return TRUE;
            }
            if (virtualKey == VK_ESCAPE)
                return CDialogEx::PreTranslateMessage(msg);  // Esc cancels the dialog
            captureKey(virtualKey);
            return TRUE;
        }
    }

    return CDialogEx::PreTranslateMessage(msg);
}

BOOL ShortcutsDialog::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    refreshEditTexts();
    return TRUE;
}

void ShortcutsDialog::DoDataExchange(CDataExchange *dx)
{
    CDialogEx::DoDataExchange(dx);
}

void ShortcutsDialog::OnOK()
{
    if (!validateAndReport())
        return;
    CDialogEx::OnOK();
}

ShortcutsDialog::CaptureEdit *ShortcutsDialog::captureEditForFocus()
{
    CWnd *focus = GetFocus();
    if (!focus || !focus->GetSafeHwnd())
        return nullptr;
    for (CaptureEdit &capture : m_captures) {
        CWnd *control = GetDlgItem(capture.controlId);
        if (control && control->GetSafeHwnd() == focus->GetSafeHwnd())
            return &capture;
    }
    return nullptr;
}

void ShortcutsDialog::captureKey(UINT virtualKey)
{
    CaptureEdit *capture = captureEditForFocus();
    if (!capture)
        return;

    ui::ShortcutChord chord;
    chord.virtualKey = virtualKey;
    chord.ctrl = (::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    chord.shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    chord.alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    *capture->chord = chord;
    refreshEditTexts();
}

void ShortcutsDialog::refreshEditTexts()
{
    for (const CaptureEdit &capture : m_captures) {
        CWnd *control = GetDlgItem(capture.controlId);
        if (control && capture.chord)
            control->SetWindowText(ui::shortcutChordText(*capture.chord).c_str());
    }
}

bool ShortcutsDialog::validateAndReport()
{
    const ui::ShortcutChord chords[] = {
        m_settings.newConnection, m_settings.openConnections, m_settings.toggleFullScreen,
    };
    const wchar_t *const rowNames[] = {L"New connection", L"Open connections", L"Toggle full screen"};

    for (int i = 0; i < 3; ++i) {
        if (chords[i].virtualKey != 0 && !ui::isValidShortcutChord(chords[i])) {
            CString message;
            message.Format(L"\"%s\" cannot use the shortcut \"%s\".\n"
                           L"Use a key with Ctrl, Shift or Alt, or a function key.",
                           rowNames[i],
                           ui::shortcutChordText(chords[i]).c_str());
            MessageBox(message, L"Shortcuts", MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    for (int i = 0; i < 3; ++i) {
        if (chords[i].virtualKey == 0)
            continue;  // disabled shortcuts never collide
        for (int j = i + 1; j < 3; ++j) {
            if (chords[j].virtualKey != 0 && chords[i] == chords[j]) {
                MessageBox(L"Two actions use the same shortcut.",
                           L"Shortcuts", MB_OK | MB_ICONWARNING);
                return false;
            }
        }
    }
    return true;
}
