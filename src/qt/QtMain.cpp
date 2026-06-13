#include "qt/QtMainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QStringList>

#include <windows.h>

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

void applyApplicationStyle(QApplication &application)
{
    application.setStyle(QStringLiteral("Fusion"));

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(9);
    application.setFont(font);

    application.setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: #f6f7f9; color: #1f2328; }"
        "#titleBar { background: #ffffff; border-bottom: 1px solid #c9ced6; }"
        "#captionButton, #closeCaptionButton { background: transparent; border: 0; border-radius: 0; padding: 0; }"
        "#captionButton:hover { background: #e8edf4; }"
        "#closeCaptionButton:hover { background: #c42b1c; }"
        "QLineEdit, QListWidget { background: #ffffff; border: 1px solid #c9ced6; border-radius: 4px; padding: 6px; }"
        "QListWidget::item { border-bottom: 1px solid #edf0f3; padding: 7px 6px; }"
        "QListWidget::item:selected { background: #dbeafe; color: #111827; }"
        "QPushButton { background: #ffffff; border: 1px solid #b8c0cc; border-radius: 4px; padding: 7px 10px; }"
        "QPushButton:hover { background: #eef4ff; border-color: #7da2d6; }"
        "QPushButton:pressed { background: #dceafe; }"
        "QPushButton:disabled { color: #8a929e; background: #edf0f3; }"
        "QTabWidget::pane { border: 0; background: #ffffff; }"
        "QTabBar::tab { background: #e7eaf0; border: 1px solid #c9ced6; padding: 7px 14px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #ffffff; border-bottom-color: #ffffff; }"
        "QStatusBar { background: #eef1f5; border-top: 1px solid #c9ced6; }"
        "#sessionSurface { background: #111827; border: 0; }"
        "#sessionSurface QLabel { color: #cbd5e1; background: transparent; }"
        "#mutedLabel { color: #6b7280; }"));
}
}

int main(int argc, char *argv[])
{
#ifdef RDPBOX_USE_QWINDOWKIT
    QGuiApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
#endif

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

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RdpBox"));
    QApplication::setApplicationVersion(QString::fromWCharArray(RDPBOX_VERSION));
    QApplication::setOrganizationName(QStringLiteral("RdpBox"));
    applyApplicationStyle(application);

    QtMainWindow window(parseStartupConnections(application.arguments()));
    window.show();

    const int exitCode = application.exec();
    if (mutex)
        ::CloseHandle(mutex);
    return exitCode;
}
