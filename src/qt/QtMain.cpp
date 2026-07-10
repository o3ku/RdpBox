#include "qt/QtMainWindow.h"

#include "common/AppPaths.h"
#include "common/ConnectionLaunchArgs.h"
#include "common/PasswordProtection.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QStringList>
#include <QtPlugin>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>

#ifdef QT_STATIC
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

namespace
{
class ScopedComInitialization
{
public:
    ScopedComInitialization()
        : m_initialized(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
    }

    ~ScopedComInitialization()
    {
        if (m_initialized)
            ::CoUninitialize();
    }

    bool initialized() const
    {
        return m_initialized;
    }

private:
    bool m_initialized = false;
};

class ScopedWinsockInitialization
{
public:
    ScopedWinsockInitialization()
        : m_initialized(false)
    {
        WSADATA wsaData = {};
        m_initialized = ::WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    ~ScopedWinsockInitialization()
    {
        if (m_initialized)
            ::WSACleanup();
    }

    bool initialized() const
    {
        return m_initialized;
    }

private:
    bool m_initialized = false;
};

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

bool configurePasswordProtection()
{
    PasswordProtection::setMode(
        AppPaths::isPortableMode()
            ? PasswordProtection::Mode::Portable
            : PasswordProtection::Mode::Dpapi);
    return PasswordProtection::isReady();
}

void applyApplicationStyle(QApplication &application)
{
    application.setStyle(QStringLiteral("Fusion"));

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(9);
    application.setFont(font);

    application.setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: #f6f7f9; color: #1f2328; }"
        "#titleBar { background: #000000; border: 0; }"
        "#captionButton, #closeCaptionButton { background: transparent; border: 0; border-radius: 0; padding: 0; color: #e6e6e6; }"
        "#captionButton:hover { background: #404040; }"
        "#closeCaptionButton:hover { background: #c42b1c; }"
        "#captionTabBar { background: #000000; }"
        "#captionTabBar::tab { background: transparent; color: #aaaaaa; border: 0; border-right: 1px solid #464646; padding: 7px 28px 7px 12px; min-width: 120px; max-width: 220px; height: 20px; }"
        "#captionTabBar::tab:selected { background: #3e300b; color: #f4eacc; }"
        "#captionTabBar::tab:hover:!selected { background: #373737; color: #e6e6e6; }"
        "#captionTabBar::close-button { subcontrol-position: right; margin-right: 7px; width: 12px; height: 12px; }"
        "#captionTabBar::close-button:hover { background: #e81123; }"
        "QLineEdit, QListWidget, QTableWidget { background: #ffffff; border: 1px solid #c9ced6; border-radius: 0; padding: 4px; }"
        "QListWidget::item { border-bottom: 1px solid #edf0f3; padding: 7px 6px; }"
        "QListWidget::item:selected { background: #dbeafe; color: #111827; }"
        "QTableWidget::item:selected { background: #dbeafe; color: #111827; }"
        "QHeaderView::section { background: #f3f3f3; border: 0; border-right: 1px solid #cccccc; border-bottom: 1px solid #cccccc; padding: 4px 6px; }"
        "QPushButton { background: #ffffff; border: 1px solid #b8c0cc; border-radius: 4px; padding: 7px 10px; }"
        "QPushButton:hover { background: #eef4ff; border-color: #7da2d6; }"
        "QPushButton:pressed { background: #dceafe; }"
        "QPushButton:disabled { color: #8a929e; background: #edf0f3; }"
        "#sessionHost { background: #000000; border: 0; }"
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

    ScopedComInitialization com;
    if (!com.initialized())
        return 1;

    ScopedWinsockInitialization winsock;
    if (!winsock.initialized())
        return 1;

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
    const std::vector<std::wstring> startupConnections =
        parseStartupConnections(application.arguments());
    if (!configurePasswordProtection())
        return 1;

    QApplication::setApplicationName(QStringLiteral("RdpBox"));
    QApplication::setApplicationVersion(QString::fromWCharArray(RDPBOX_VERSION));
    QApplication::setOrganizationName(QStringLiteral("RdpBox"));
    const QIcon appIcon(QStringLiteral(":/rdpbox/logo.png"));
    if (!appIcon.isNull())
        QApplication::setWindowIcon(appIcon);
    applyApplicationStyle(application);

    QtMainWindow window(startupConnections);
    window.show();

    const int exitCode = application.exec();
    if (mutex)
        ::CloseHandle(mutex);
    return exitCode;
}
