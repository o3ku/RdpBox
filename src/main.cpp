#include "MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setOrganizationName("RdpBox");
    app.setApplicationName("RdpBox");

    QIcon appIcon(":/logo.svg");
    if (appIcon.isNull())
        appIcon = QIcon(":/logo.png");
    QApplication::setWindowIcon(appIcon);

    MainWindow window;
    window.show();
    return app.exec();
}
