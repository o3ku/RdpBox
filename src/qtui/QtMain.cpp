#include "qtui/QtMainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"

#include <QApplication>
#include <QSettings>
#include <QTranslator>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QPair>
#include <QPixmap>
#include <QPolygon>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QStringList>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

namespace
{
std::vector<std::wstring> parseStartupConnections(const QStringList &arguments)
{
    std::vector<std::wstring> startupConnectionNames;
    for (int i = 1; i < arguments.size(); ++i) {
        const std::wstring argument = arguments.at(i).toStdWString();
        if (argument == L"--portable") {
            AppPaths::enablePortableMode();
            continue;
        }

        std::vector<std::wstring> parsedNames;
        if (launch::tryParseConnectionsArgument(argument, parsedNames))
            startupConnectionNames = std::move(parsedNames);
    }
    return startupConnectionNames;
}

}

namespace
{
struct ThemeColors
{
    QColor window, text, title, border, panel, field, hover, select, selectText;
    QColor button, buttonHover, buttonPressed, buttonBorder, muted, rowSep;
    QColor sessionBg, sessionText;
    QColor accent = QColor(0xcb, 0x83, 0x06);
};

const ThemeColors &lightThemeColors()
{
    static const ThemeColors colors = {
        QColor(0xf6, 0xf7, 0xf9),   // window
        QColor(0x1f, 0x23, 0x28),   // text
        QColor(0xff, 0xff, 0xff),   // title
        QColor(0xc9, 0xce, 0xd6),   // border
        QColor(0xff, 0xff, 0xff),   // panel
        QColor(0xff, 0xff, 0xff),   // field
        QColor(0xe8, 0xed, 0xf4),   // hover
        QColor(0xdb, 0xea, 0xfe),   // select
        QColor(0x11, 0x18, 0x27),   // selectText
        QColor(0xff, 0xff, 0xff),   // button
        QColor(0xee, 0xf4, 0xff),   // buttonHover
        QColor(0xdc, 0xea, 0xfe),   // buttonPressed
        QColor(0xb8, 0xc0, 0xcc),   // buttonBorder
        QColor(0x6b, 0x72, 0x80),   // muted
        QColor(0xed, 0xf0, 0xf3),   // rowSep
        QColor(0x11, 0x18, 0x27),   // sessionBg
        QColor(0xcb, 0xd5, 0xe1),   // sessionText
    };
    return colors;
}

const ThemeColors &darkThemeColors()
{
    static const ThemeColors colors = {
        QColor(0x1b, 0x21, 0x29),   // window
        QColor(0xe6, 0xe9, 0xee),   // text
        QColor(0x23, 0x2a, 0x34),   // title
        QColor(0x3a, 0x43, 0x50),   // border
        QColor(0x23, 0x2a, 0x34),   // panel
        QColor(0x2a, 0x32, 0x3d),   // field
        QColor(0x30, 0x39, 0x47),   // hover
        QColor(0x3b, 0x46, 0x57),   // select
        QColor(0xff, 0xff, 0xff),   // selectText
        QColor(0x2a, 0x32, 0x3d),   // button
        QColor(0x35, 0x40, 0x50),   // buttonHover
        QColor(0x2b, 0x35, 0x43),   // buttonPressed
        QColor(0x46, 0x52, 0x64),   // buttonBorder
        QColor(0x9a, 0xa3, 0xad),   // muted
        QColor(0x2e, 0x37, 0x42),   // rowSep
        QColor(0x0f, 0x14, 0x1b),   // sessionBg
        QColor(0xcb, 0xd5, 0xe1),   // sessionText
    };
    return colors;
}

bool windowsAppsUseLightTheme()
{
#ifdef _WIN32
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return true;
    return value != 0;
#else
    // No registry off Windows; follow the platform palette luminance instead.
    const QColor background = QPalette().color(QPalette::Window);
    return (background.red() * 299 + background.green() * 587 + background.blue() * 114) / 1000 >= 128;
#endif
}

void applyThemePalette(QApplication &application, const ThemeColors &colors)
{
    QPalette palette;
    palette.setColor(QPalette::Window, colors.window);
    palette.setColor(QPalette::WindowText, colors.text);
    palette.setColor(QPalette::Base, colors.panel);
    palette.setColor(QPalette::AlternateBase, colors.field);
    palette.setColor(QPalette::Text, colors.text);
    palette.setColor(QPalette::Button, colors.button);
    palette.setColor(QPalette::ButtonText, colors.text);
    palette.setColor(QPalette::Highlight, colors.select);
    palette.setColor(QPalette::HighlightedText, colors.selectText);
    palette.setColor(QPalette::ToolTipBase, colors.panel);
    palette.setColor(QPalette::ToolTipText, colors.text);
    palette.setColor(QPalette::PlaceholderText, colors.muted);
    // Rich-text links (About page repo URL) render with QPalette::Link;
    // unset, dark themes keep the default saturated blue - unreadable.
    const bool darkTheme = colors.text.lightness() > colors.window.lightness();
    const QColor link = darkTheme ? QColor(0x8a, 0xbf, 0xff) : QColor(0x25, 0x63, 0xeb);
    palette.setColor(QPalette::Link, link);
    palette.setColor(QPalette::LinkVisited, link);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.muted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.muted);
    application.setPalette(palette);
}

// QStyleSheetStyle draws its own built-in dark-gray arrows once a stylesheet
// touches QComboBox/QSpinBox (the base style is bypassed). Theme triangles are
// painted at boot into the temp dir; a failed save returns a null path and the
// caller strips the icon rules - url() with a broken path would void the whole
// stylesheet (AtomDataAssistant Theme.h precedent). '#' is stripped from the
// file name: it is a URL fragment delimiter.
// Lucide "x" mark for the closable title tabs (QSS needs a file again).
QString closeImagePath(const QColor &color)
{
    QPixmap pixmap(10, 10);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color);
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(2, 2, 8, 8);
    painter.drawLine(8, 2, 2, 8);
    const QString name = QStringLiteral("rdpbox_close_%1.png").arg(color.name().remove(QLatin1Char('#')));
    QString path = QDir::temp().absoluteFilePath(name);
    if (!pixmap.save(path, "PNG"))
        return QString();
    return path.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString arrowImagePath(const QColor &color, bool up, int canvasHeight = 6)
{
    // canvasHeight > 6 pads the far end (away from the up/down split line) so
    // the triangles hug the middle seam of a QSpinBox instead of drifting to
    // the outer edges of the half-height up/down buttons.
    QPixmap pixmap(10, canvasHeight);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const int top = up ? canvasHeight - 6 : 0;
    QPolygon triangle;
    if (up) {
        triangle << QPoint(5, top) << QPoint(10, top + 6) << QPoint(0, top + 6);
    } else {
        triangle << QPoint(0, top) << QPoint(10, top) << QPoint(5, top + 6);
    }
    painter.drawPolygon(triangle);
    const QString name = QStringLiteral("rdpbox_arrow_%1_%2_%3.png")
                             .arg(color.name().remove(QLatin1Char('#')))
                             .arg(up ? 'u' : 'd')
                             .arg(canvasHeight);
    QString path = QDir::temp().absoluteFilePath(name);
    if (!pixmap.save(path, "PNG"))
        return QString();
    return path.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString themeStyleSheet(const ThemeColors &colors)
{
    QString sheet = QStringLiteral(
        "QMainWindow, QWidget { background: %WINDOW%; color: %TEXT%; }"
        "#titleBar { background: %TITLE%; border-bottom: 1px solid %BORDER%; }"
        "#titleBar QLabel, #connectionsHeader { background: transparent; }"
        "#headerSeparator { background: %BORDER%; }"
        "#captionButton, #closeCaptionButton, #logoButton { background: transparent;"
        " border: 0; border-radius: 0; padding: 0; }"
        "#captionButton:hover { background: %HOVER%; }"
        "#closeCaptionButton:hover { background: #c42b1c; }"
        "#logoButton:hover { background: %HOVER%; }"
        "#logoButton:checked { background: #cb8306; }"
        "#addButton { background: transparent; border: 0; border-radius: 4px; }"
        "#addButton:hover { background: %HOVER%; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { background: %FIELD%;"
        " border: 1px solid %BORDER%; border-radius: 4px; padding: 6px;"
        " selection-background-color: %SELECT%; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox::down-arrow { image: url(%ARROW_DOWN%); }"
        "QComboBox QAbstractItemView { background: %PANEL%; border: 1px solid %BORDER%;"
        " selection-background-color: %SELECT%; selection-color: %SELECTTEXT%; outline: 0; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border;"
        " subcontrol-position: top right; width: 20px; height: 15px; border: 0; background: transparent; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border;"
        " subcontrol-position: bottom right; width: 20px; height: 15px; border: 0; background: transparent; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(%SPIN_UP%);"
        " width: 10px; height: 12px; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(%SPIN_DOWN%);"
        " width: 10px; height: 12px; }"
        "QListWidget { background: %PANEL%; border: 1px solid %BORDER%; outline: 0; }"
        "QListWidget::item { border-bottom: 1px solid %ROWSEP%; }"
        "QListWidget::item:selected { background: %SELECT%; color: %SELECTTEXT%; }"
        "#connectionRow, #connectionRow QLabel { background: transparent; }"
        "QPushButton { background: %BUTTON%; border: 1px solid %BUTTONBORDER%;"
        " border-radius: 4px; padding: 7px 10px; }"
        "QPushButton:hover { background: %BUTTONHOVER%; }"
        "QPushButton:pressed { background: %BUTTONPRESSED%; }"
        "QPushButton:default { border-color: #cb8306; }"
        "QPushButton:disabled { color: %MUTED%; background: %ROWSEP%; }"
        "QMenu { background: %PANEL%; border: 1px solid %BORDER%; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::item:selected { background: %SELECT%; color: %SELECTTEXT%; }"
        "QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }"
        "QTabWidget::pane { border: 0; background: %WINDOW%; }"
        "#titleTabBar { background: transparent; }"
        "#titleTabBar::tab { background: transparent; border: none; padding: 4px 12px;"
        " margin-right: 2px; color: %TEXT%; }"
        "#titleTabBar::tab:hover { background: %HOVER%; }"
        "#titleTabBar::tab:selected { border-bottom: 2px solid #cb8306; }"
        "#titleTabBar::close-button { image: url(%TAB_CLOSE%); background: transparent; border: none; }"
        "#sessionSurface { background: %SESSIONBG%; border: 0; }"
        "#sessionSurface QLabel { color: %SESSIONTEXT%; background: transparent; }"
        "#mutedLabel { color: %MUTED%; }"
        "QSplitter::handle { background: %BORDER%; }"
        "QSplitter::handle:horizontal { width: 1px; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %BUTTONBORDER%; border-radius: 4px;"
        " min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %MUTED%; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: %BUTTONBORDER%; border-radius: 4px;"
        " min-width: 24px; }"
        "QScrollBar::handle:horizontal:hover { background: %MUTED%; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }");
    const QPair<QString, QColor> tokens[] = {
        {QStringLiteral("%WINDOW%"), colors.window},
        {QStringLiteral("%TEXT%"), colors.text},
        {QStringLiteral("%TITLE%"), colors.title},
        {QStringLiteral("%BORDER%"), colors.border},
        {QStringLiteral("%PANEL%"), colors.panel},
        {QStringLiteral("%FIELD%"), colors.field},
        {QStringLiteral("%HOVER%"), colors.hover},
        {QStringLiteral("%SELECT%"), colors.select},
        {QStringLiteral("%SELECTTEXT%"), colors.selectText},
        {QStringLiteral("%BUTTON%"), colors.button},
        {QStringLiteral("%BUTTONHOVER%"), colors.buttonHover},
        {QStringLiteral("%BUTTONPRESSED%"), colors.buttonPressed},
        {QStringLiteral("%BUTTONBORDER%"), colors.buttonBorder},
        {QStringLiteral("%MUTED%"), colors.muted},
        {QStringLiteral("%ROWSEP%"), colors.rowSep},
        {QStringLiteral("%SESSIONBG%"), colors.sessionBg},
        {QStringLiteral("%SESSIONTEXT%"), colors.sessionText},
    };
    for (const auto &token : tokens)
        sheet.replace(token.first, token.second.name());

    struct ArrowIcon
    {
        const char *placeholder;
        const char *rule; // stripped when the PNG cannot be written
        bool up;
        int canvasHeight = -1; // <0: close-icon entry (closeImagePath)
    };
    const ArrowIcon arrows[] = {
        {"%SPIN_UP%",
         "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(%SPIN_UP%);"
         " width: 10px; height: 12px; }", true, 12},
        {"%ARROW_DOWN%", "QComboBox::down-arrow { image: url(%ARROW_DOWN%); }", false, 6},
        {"%SPIN_DOWN%",
         "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(%SPIN_DOWN%);"
         " width: 10px; height: 12px; }", false, 12},
        {"%TAB_CLOSE%",
         "#titleTabBar::close-button { image: url(%TAB_CLOSE%); background: transparent;"
         " border: none; }"},
    };
    for (const ArrowIcon &arrow : arrows) {
        const QString path =
            arrow.canvasHeight >= 0 ? arrowImagePath(colors.text, arrow.up, arrow.canvasHeight)
                                    : closeImagePath(colors.text);
        if (!path.isEmpty())
            sheet.replace(QLatin1String(arrow.placeholder), path);
        else
            sheet.remove(QString::fromLatin1(arrow.rule));
    }
    return sheet;
}

} // namespace

static QTranslator g_translator;
static QTranslator g_qtBaseTranslator;   // OK/Cancel etc. (QPlatformTheme texts)
static QString g_installedLanguage;

void applyApplicationTheme(QApplication &application)
{
    // UI language: translations/rdpbox_<lang>.qm next to the exe. Runs on
    // every apply; reloads the translator only when the setting changed so
    // the settings dialog can switch languages live.
    QTranslator &translator = g_translator;
    QTranslator &qtBaseTranslator = g_qtBaseTranslator;
    QString &installedLanguage = g_installedLanguage;
    const QString languageSetting = QSettings().value(QStringLiteral("language")).toString();
    if (languageSetting != installedLanguage) {
        QCoreApplication::removeTranslator(&translator);
        QCoreApplication::removeTranslator(&qtBaseTranslator);
        if (!languageSetting.isEmpty()) {
            // Qt's own locale suffix (zh -> zh_CN); app strings come from
            // rdpbox_<lang>.qm, Qt standard texts (QDialogButtonBox/QMessageBox)
            // from qtbase_<locale>.qm.
            const QString locale = languageSetting == QLatin1String("zh")
                                       ? QStringLiteral("zh_CN") : languageSetting;
            if (translator.load(QStringLiteral("rdpbox_") + languageSetting,
                                QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
                application.installTranslator(&translator);
            if (qtBaseTranslator.load(QStringLiteral("qtbase_") + locale,
                                      QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
                application.installTranslator(&qtBaseTranslator);
        }
        installedLanguage = languageSetting;
    }

    // Fusion base: honors the palette (themed arrows/checkboxes) and stays
    // fully consistent with the theme QSS; the native Windows style draws its
    // own arrows/buttons that clash with (or vanish under) the dark theme.
    application.setStyle(QStringLiteral("Fusion"));

#ifdef _WIN32
    QFont font(QStringLiteral("Microsoft YaHei"));
#else
    QFont font(QStringLiteral("Segoe UI"));
#endif
    font.setPointSize(9);
    application.setFont(font);

    const QString themeSetting = QSettings().value(QStringLiteral("theme"), QStringLiteral("system")).toString();
    const ThemeColors &colors =
        themeSetting == QLatin1String("light") ? lightThemeColors()
        : themeSetting == QLatin1String("dark") ? darkThemeColors()
        : (windowsAppsUseLightTheme() ? lightThemeColors() : darkThemeColors());
    applyThemePalette(application, colors);
    application.setStyleSheet(themeStyleSheet(colors));
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // FreeRDP/WinPR assumes the process initialized Winsock (like the MFC app
    // does); without this, getaddrinfo intermittently fails with 10093
    // (WSANOTINITIALISED) depending on whether QtNetwork happened to init it.
    WSADATA wsaData = {};
    const bool wsaInitialized = ::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;

    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, L"RdpBox_SingleInstance");
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::CloseHandle(mutex);
        HWND existing = ::FindWindowW(nullptr, L"RdpBox");
        if (existing) {
            ::SetForegroundWindow(existing);
            if (::IsIconic(existing))
                ::ShowWindow(existing, SW_RESTORE);
        }
        return 0;
    }
#endif

    // No "?" context-help button on any dialog.
    QCoreApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);

    QApplication application(argc, argv);
        QApplication::setApplicationName(QStringLiteral("RdpBox"));
    QApplication::setApplicationVersion(QString::fromWCharArray(RDPBOX_VERSION));
    QApplication::setOrganizationName(QStringLiteral("RdpBox"));

    applyApplicationTheme(application);
    const QIcon appIcon(QStringLiteral(":/rdpbox/logo.png"));
    if (!appIcon.isNull())
        QApplication::setWindowIcon(appIcon);
    QtMainWindow window(parseStartupConnections(application.arguments()));
    window.show();


    const int exitCode = application.exec();
#ifdef _WIN32
    if (mutex)
        ::CloseHandle(mutex);
    if (wsaInitialized)
        ::WSACleanup();
#endif
    return exitCode;
}
