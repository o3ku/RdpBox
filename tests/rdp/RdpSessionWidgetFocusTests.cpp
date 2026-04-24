#include "rdp/RdpSessionWidget.h"

#include <QTest>

class RdpSessionWidgetFocusTests : public QObject
{
    Q_OBJECT

private slots:
    void placeholder();
};

void RdpSessionWidgetFocusTests::placeholder()
{
    QSKIP("Focus verification requires interactive embedded FreeRDP window.");
}

QTEST_MAIN(RdpSessionWidgetFocusTests)

#include "RdpSessionWidgetFocusTests.moc"
