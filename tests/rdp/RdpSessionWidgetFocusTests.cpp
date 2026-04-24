#include <QKeyEvent>
#include <QTest>

#define private public
#include "rdp/RdpSessionWidget.h"
#undef private

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class RdpSessionWidgetFocusTests : public QObject
{
    Q_OBJECT

private slots:
    void forwardsKeyboardMessagesToEmbeddedWindow();
};

namespace
{
    struct SentMessage
    {
        unsigned int message = 0;
        quintptr wParam = 0;
        qintptr lParam = 0;
    };

    class TestableRdpSessionWidget : public RdpSessionWidget
    {
    public:
        using RdpSessionWidget::RdpSessionWidget;

        QList<SentMessage> sentMessages;
        int focusForwardCount = 0;

        void dispatchKeyPress(QKeyEvent *event)
        {
            keyPressEvent(event);
        }

    protected:
        void setFocusToFreeRdp() override
        {
            focusForwardCount++;
        }

        bool sendMessageToChild(unsigned int message, quintptr wParam, qintptr lParam) override
        {
            sentMessages.append(SentMessage{message, wParam, lParam});
            return true;
        }
    };
}

void RdpSessionWidgetFocusTests::forwardsKeyboardMessagesToEmbeddedWindow()
{
    TestableRdpSessionWidget widget;
    widget.m_childWindow = reinterpret_cast<HWND>(1);

    QKeyEvent event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, 0x1E, 'A', 0, "a", false, 1);

    widget.dispatchKeyPress(&event);

    QCOMPARE(widget.focusForwardCount, 1);
    QCOMPARE(widget.sentMessages.size(), 2);
    QCOMPARE(widget.sentMessages.at(0).message, static_cast<UINT>(WM_KEYDOWN));
    QCOMPARE(widget.sentMessages.at(0).wParam, static_cast<WPARAM>('A'));
    QCOMPARE(widget.sentMessages.at(0).lParam, static_cast<LPARAM>(0x001E0000));
    QCOMPARE(widget.sentMessages.at(1).message, static_cast<UINT>(WM_CHAR));
    QCOMPARE(widget.sentMessages.at(1).wParam, static_cast<WPARAM>('a'));
}

QTEST_MAIN(RdpSessionWidgetFocusTests)

#include "RdpSessionWidgetFocusTests.moc"
